#include "EnginePCH.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Editor/Asset/FbxImporter/FbxImporter.h"
#include "Core/Windows/WindowsPlatformTime.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Input/InputSystem.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/GeometryGenerator.h"
#include "Render/RenderCommand.h"
#include "Render/RenderDevice.h"
#include "Render/RenderResourceManager.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Serialization/JsonArchive.h"
#include "UI/UIRenderer.h"
#include "directxtk/ScreenGrab.h"
#include <wincodec.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <set>

namespace {
    class FStageLog final : public ILogSink {
    public:
        FStageLog();
        ~FStageLog();

    public:
        void OnLog(ELogVerbosity Verbosity, const FString& Message) override;
        bool HasError() const;

    private:
        bool mHasError{};
    };

    FStageLog::FStageLog() {
        FLog::AddSink(this);
    }

    FStageLog::~FStageLog() {
        FLog::RemoveSink(this);
    }

    void FStageLog::OnLog(ELogVerbosity Verbosity, const FString& Message) {
        if (Verbosity == ELogVerbosity::Error) {
            mHasError = true;
            std::fprintf(stderr, "%s\n", Message.c_str());
        }
    }

    bool FStageLog::HasError() const {
        return mHasError;
    }

    void Check(bool Condition, const FString& Message) {
        if (!Condition) {
            throw std::runtime_error(Message);
        }
    }

    void ImportLibrary(const FString& SourceDirectory) {
        nlohmann::json Manifest{};
        FString SharedSkeleton{};

        for (const bool RootMotion : {false, true}) {
            const FString Kind{RootMotion ? "RootMotion" : "InPlace"};
            const FString Source{SourceDirectory + (RootMotion ? "/UAL1_Standard_RM.fbx" : "/UAL1_Standard.fbx")};
            const FString Destination{"Assets/LocomotionStage/" + Kind};
            FFbxImportOptions Options{};
            Options.CustomBoneAnimationSampleRate = 30;
            Options.FrameAlignment = EFbxFrameAlignment::Closest;
            Options.SkeletalMeshSkeletonKey = SharedSkeleton;
            Options.AnimationSkeletonKey = SharedSkeleton;
            const FFbxImportResult Result{FFbxAssetLoader::Import(Source, Options, Destination, FFbxImporter::Cook)};
            Check(Result.bSuccess, "Import " + Kind + ": " + Result.Error);

            for (const FString& Warning : Result.Warnings) {
                std::fprintf(stderr, "%s\n", Warning.c_str());
            }

            auto& Entries{Manifest[Kind]};
            Entries = nlohmann::json::array();

            for (const FString& Key : Result.AssetKeys) {
                FFbxAssetData Data{};
                FString Error{};
                Check(FFbxAssetBake::ReadBaked(FAssetUtils::ResolveAssetFilePath(Key), Data, Error), "Read imported asset: " + Error);
                if (Data.Type == EFbxAssetType::Skeleton && SharedSkeleton.empty()) {
                    SharedSkeleton = Key;
                }

                nlohmann::json Entry{{"Key", Key}, {"Object", Data.SourceObject}, {"Type", static_cast<int>(Data.Type)}};

                if (Data.Type == EFbxAssetType::AnimSequence) {
                    auto* Sequence{UAssetManager::GetAssetByPath<UAnimSequenceBase>(Key)};
                    Check(Sequence != nullptr, "Load imported sequence " + Key);
                    const FVector Delta{Sequence->GetRootLocationAtTime(Sequence->GetPlayLength()) - Sequence->GetRootLocationAtTime(0.0)};
                    Entry["Length"] = Sequence->GetPlayLength();
                    Entry["Skeleton"] = Data.SkeletonKey;
                    Entry["RootDelta"] = {Delta.X, Delta.Y, Delta.Z};
                    Entry["HasRootTrack"] = Sequence->HasRootTrack();
                }

                Entries.push_back(std::move(Entry));
            }

            std::printf("IMPORTED %s: %u assets\n", Kind.c_str(), Result.AssetKeys.Num());
        }

        std::ofstream Output{"Assets/LocomotionStage/Library.json", std::ios::binary};
        Output << Manifest.dump(2) << '\n';
        Check(Output.good(), "Write Locomotion Stage library manifest");
    }

    void Tick(UWorld* World, uint32 Frames) {
        for (uint32 Frame{}; Frame < Frames; ++Frame) {
            World->Tick(1.0f / 30.0f);
            FInputSystem::UpdateInputStates();
        }
    }

    void Press(UWorld* World, int32 Key, uint32 Frames = 1) {
        FInputSystem::OnKeyDown(Key);
        Tick(World, Frames);
        FInputSystem::OnKeyUp(Key);
        Tick(World, 1);
    }

    USkeletalMeshComponent* FindMesh(UWorld* World, const FString& Name) {
        for (AActor* Actor : World->GetPersistentLevel()->GetActors()) {
            if (Actor->GetName() == Name) {
                for (UActorComponent* Component : Actor->GetComponents()) {
                    if (auto* Mesh{Cast<USkeletalMeshComponent>(Component)}) {
                        return Mesh;
                    }
                }
            }
        }

        return nullptr;
    }

    FAnimNode_StateMachine* StateMachine(USkeletalMeshComponent* Mesh) {
        return dynamic_cast<FAnimNode_StateMachine*>(Mesh->GetAnimInstance()->GetRootNode());
    }

    void Capture(UWorld* World, const FString& Name) {
        auto* Viewport{GEngine->GetWorldContextFromWorld(World)->GameViewport.get()};
        const FRect Rect{0.0f, 0.0f, 1600.0f, 900.0f};
        FSceneView View{Viewport->CalcSceneView(Rect)};
        View.bGameView = true;
        const FVector Focus{FindMesh(World, "Stage Player")->GetOwner()->GetActorLocation() + FVector{0.0f, 0.0f, 1.25f}};
        Check(View.ViewForward.Dot((Focus - View.ViewLocation).Normalized()) > 0.98f, "The third-person camera frames its character instead of the sky");
        FRenderer Renderer{};
        Check(Renderer.Init(), "Initialize the offscreen renderer");
        Renderer.InitScenePasses("Assets/SkySphere/Sky.hdr");
        FSceneRenderOptions Options{};
        Options.DirectionalShadowMapSize = 2048;
        Options.bEnableFXAA = true;
        FUIRenderer UI{};
        UI.Init();
        FSceneRenderer Scene{World, View, Viewport->GetRenderData()};
        Scene.InitViews(&Renderer, Options);
        Check(Scene.Render(&Renderer, Viewport->GetViewTargets(), Options, [&](FScenePassCallbacks& Passes) {
            Passes.Overlays.GameUI = [&]() {
                FUIDrawList DrawList{};
                Viewport->PaintUI(DrawList);
                UI.Render(DrawList, Viewport->GetWidth(), Viewport->GetHeight());
            };
        }), "Render the stage and HUD");
        const std::filesystem::path Output{"Build/LocomotionStage/" + Name + ".png"};
        Check(SUCCEEDED(DirectX::SaveWICTextureToFile(FRenderDevice::Get()->GetContext(), Viewport->GetColorTarget()->GetRawPtr(), GUID_ContainerFormatPng, Output.c_str())), "Save stage capture " + Name);
        std::printf("CAPTURE %s\n", Output.string().c_str());
    }

    void VerifyStage(bool CaptureImages, bool PreviewOnly = false) {
        std::ifstream File{"Assets/LocomotionStage/Library.json"};
        const nlohmann::json Library{nlohmann::json::parse(File)};
        const USkeleton* SharedSkeleton{};

        for (const FString& Kind : {"InPlace", "RootMotion"}) {
            uint32 Count{};

            for (const auto& Entry : Library.at(Kind)) {
                if (Entry.at("Type").get<int>() != static_cast<int>(EFbxAssetType::AnimSequence)) {
                    continue;
                }

                const FString Key{Entry.at("Key").get<FString>()};
                const auto* Sequence{UAssetManager::GetAssetByPath<UAnimSequenceBase>(Key)};
                Check(Sequence != nullptr && Sequence->GetPlayLength() > 0.0f, "Load sequence " + Key);

                if (SharedSkeleton == nullptr) {
                    SharedSkeleton = Sequence->GetSkeleton();
                }

                Check(Sequence->GetSkeleton() == SharedSkeleton, "Both source libraries share the same runtime skeleton");
                ++Count;
            }

            Check(Count == 43, "All 43 clips are available in " + Kind);
        }

        std::printf("PASS 86 imported sequences share one skeleton.\n");
        auto* Editor{UWorld::CreateWorld(EWorldType::Editor, false)};
        Check(FJsonArchive::LoadWorld(Editor, "Scenes/Demo_LocomotionStage.scene"), "Load the scene v5 Locomotion Stage");
        Check(FJsonArchive::SaveWorld(Editor, "Build/LocomotionStage/RoundTrip.scene"), "Save the stage with current scene serialization");
        auto* Restored{UWorld::CreateWorld(EWorldType::Editor, false)};
        Check(FJsonArchive::LoadWorld(Restored, "Build/LocomotionStage/RoundTrip.scene"), "Reload the scene v5 Locomotion Stage");
        Check(Editor->GetPersistentLevel()->GetActors().Num() == Restored->GetPersistentLevel()->GetActors().Num(), "Scene round-trip preserves actor identities");
        GEngine->DestroyWorld(Restored);
        auto* World{UWorld::GetDuplicatedWorldForPIE(Editor)};
        Check(World != nullptr, "Create the stage PIE world");
        FWorldContext& Context{GEngine->CreateNewWorldContext(EWorldType::PIE)};
        Context.SetCurrentWorld(World);
        Context.GameViewport = MakeShared<FGameViewportClient>();
        Context.GameViewport->Init(Context, GEngine);
        Context.GameViewport->Resize(1600, 900);
        World->BeginPlay();
        GEngine->DestroyWorld(Editor);
        Tick(World, 8);
        auto* Hero{FindMesh(World, "Stage Player")};
        auto* RootDemo{FindMesh(World, "Root Demonstrator")};
        Check(Hero != nullptr && Hero->GetAnimInstance() != nullptr && RootDemo != nullptr, "The hero and independent demonstrators initialize");

        if (PreviewOnly) {
            for (const int32 Zone : {1, 2, 3, 4, 5}) {
                Press(World, '0' + Zone, 8);
                Capture(World, "Preview_" + std::to_string(Zone));
            }

            World->EndPlay();
            GEngine->DestroyWorldContext(World);
            FInputSystem::ClearAllStates();
            return;
        }

        if (CaptureImages) {
            Capture(World, "01_Flow");
        }

        AActor* HeroActor{Hero->GetOwner()};
        const float StartX{HeroActor->GetActorLocation().X};
        Press(World, 'W', 24);
        Check(HeroActor->GetActorLocation().X > StartX + 1.5f, "WASD moves on the traced stage floor");
        Check(HeroActor->GetActorLocation().Z > -0.1f, "The stage floor supports the controller");
        Press(World, 'C', 12);
        Check(StateMachine(Hero)->GetCurrentState() == 0, "Crouch remains in the nested locomotion machine");
        auto* Movement{dynamic_cast<FAnimNode_StateMachine*>(StateMachine(Hero)->FindState(0)->mPose.mLinkedNode)};
        Check(Movement != nullptr && Movement->GetCurrentState() == 1, "C selects crouch through native graph rules");
        Press(World, 'C', 12);
        Press(World, 'T', 16);
        Check(Movement->GetCurrentState() == 2, "T enters the pitch-driven aim blend");
        Press(World, 'T', 12);
        Press(World, VK_SPACE, 3);
        std::set<int32> AirStates{};

        for (uint32 Frame{}; Frame < 45; ++Frame) {
            Tick(World, 1);
            AirStates.insert(Movement->GetCurrentState());
        }

        Check(AirStates.contains(4) && AirStates.contains(5) && AirStates.contains(6), "Jump Start, Falling and Land respond to native transition rules");
        Tick(World, 15);
        Press(World, 'Q', 7);
        Check(StateMachine(Hero)->GetCurrentState() == 1, "Roll enters the native action state");
        Press(World, 'H', 8);
        Check(StateMachine(Hero)->GetCurrentState() == 2, "Any State hit reactions interrupt an action");
        Tick(World, 65);
        Press(World, 'U', 75);
        Check(StateMachine(Hero)->GetCurrentState() == 6, "Sitting uses its own nested lifecycle");
        auto* Seat{dynamic_cast<FAnimNode_StateMachine*>(StateMachine(Hero)->FindState(6)->mPose.mLinkedNode)};
        Check(Seat != nullptr && Seat->GetCurrentState() == 1, "Sit Enter completes into Seated Idle");
        Press(World, 'U', 75);
        Check(StateMachine(Hero)->GetCurrentState() == 0, "Sit Exit returns to locomotion");
        std::printf("PASS movement, crouch, aim, jump, Any State interruption, and seated lifecycle.\n");

        Press(World, '4', 12);
        const FVector RootStart{HeroActor->GetActorLocation()};
        Press(World, 'W', 30);
        const FVector RootEnd{HeroActor->GetActorLocation()};
        Check(RootEnd.X > RootStart.X + 0.7f, "RM clips move the actor through native root-motion extraction");
        Check(std::abs(RootEnd.Y - RootStart.Y) < 0.3f, "The mesh forward axis matches the locomotion direction");

        if (CaptureImages) {
            Capture(World, "02_RootMotion");
        }

        Press(World, '5', 8);
        auto* Player{Hero->GetSingleNodeInstance()};
        Check(Player != nullptr, "Clip Lab switches to single-sequence playback");
        Press(World, 'P', 2);
        const float PausedTime{Player->GetCurrentTime()};
        Tick(World, 12);
        Check(std::abs(Player->GetCurrentTime() - PausedTime) < 0.0001f, "Pause freezes the actual sequence clock");
        Press(World, 'N', 2);
        Check(Player->GetPlayRate() < 0.0f, "Reverse controls the native sequence player");
        Press(World, 'L', 1);
        Check(Player->GetCurrentTime() > PausedTime, "Timeline scrubbing works while paused");
        Press(World, 'P', 8);
        Press(World, 'O', 1);
        Check(std::abs(Player->GetPlayRate()) > 1.0f, "Playback speed is adjustable without reimport");
        std::set<FString> Browsed{};

        for (uint32 Index{}; Index < 43; ++Index) {
            Press(World, 'E', 1);
            Player = Hero->GetSingleNodeInstance();
            Check(Player != nullptr && Player->GetAnimationAsset() != nullptr, "The clip browser loads its selected clip");
            Browsed.insert(Player->GetAnimationAsset()->GetPath());
        }

        Check(Browsed.size() == 43, "All 43 clips are reachable through the actual Lua input controls");
        Press(World, 'M', 8);
        Check(Hero->GetSingleNodeInstance()->IsRootMotionEnabled(), "Clip Lab can compare the root-motion library");

        if (CaptureImages) {
            Capture(World, "03_ClipLab");
        }

        Press(World, '2', 8);

        Press(World, 'W', 70);
        Check(HeroActor->GetActorLocation().X > 5.5f && HeroActor->GetActorLocation().Z > 1.0f, "The controller climbs the actual sloped ramp");
        Press(World, 'W', 12);
        FInputSystem::OnKeyDown(VK_SHIFT);
        FInputSystem::OnKeyDown('W');
        Press(World, VK_SPACE, 26);
        FInputSystem::OnKeyUp('W');
        FInputSystem::OnKeyUp(VK_SHIFT);
        Tick(World, 16);
        Check(HeroActor->GetActorLocation().X > 11.5f && HeroActor->GetActorLocation().Z > 1.0f, "Sprint jumping clears the physical course gap");
        Press(World, '1', 4);
        HeroActor->GetRootComponent()->SetWorldLocation(FVector{-3.2f, 12.0f, 0.025f}, false);
        Tick(World, 4);
        Press(World, 'W', 12);
        Check(HeroActor->GetActorLocation().X < -2.55f, "Standing movement respects the low tunnel clearance");
        Press(World, 'C', 8);
        Press(World, 'W', 32);
        Check(HeroActor->GetActorLocation().X > -1.5f, "Crouch movement passes beneath the low tunnel");
        Press(World, 'C', 8);
        auto* TunnelMovement{dynamic_cast<FAnimNode_StateMachine*>(StateMachine(Hero)->FindState(0)->mPose.mLinkedNode)};
        Check(TunnelMovement->GetCurrentState() == 1, "The controller cannot stand through the tunnel roof");
        Press(World, '1', 4);
        HeroActor->GetRootComponent()->SetWorldLocation(FVector{4.2f, 13.0f, 0.025f}, false);
        Tick(World, 4);
        Press(World, 'W', 24);
        auto* PoolMovement{dynamic_cast<FAnimNode_StateMachine*>(StateMachine(Hero)->FindState(0)->mPose.mLinkedNode)};
        Check(PoolMovement->GetCurrentState() == 3 && HeroActor->GetActorLocation().Z < -0.3f, "Entering the basin selects the native swim blend");
        std::printf("POOL ENTER x=%.3f y=%.3f z=%.3f\n", HeroActor->GetActorLocation().X, HeroActor->GetActorLocation().Y, HeroActor->GetActorLocation().Z);
        Press(World, 'S', 32);
        Tick(World, 12);
        std::printf("POOL EXIT x=%.3f y=%.3f z=%.3f state=%d\n", HeroActor->GetActorLocation().X, HeroActor->GetActorLocation().Y, HeroActor->GetActorLocation().Z, PoolMovement->GetCurrentState());
        Check(PoolMovement->GetCurrentState() == 0 && HeroActor->GetActorLocation().Z > -0.1f, "Leaving the basin returns to grounded locomotion");
        Press(World, '2', 8);

        if (CaptureImages) {
            Capture(World, "04_Traverse");
            Press(World, 'I', 2);
            Capture(World, "05_Help");
            Press(World, 'I', 2);
        }

        FUIDrawList DrawList{};
        Context.GameViewport->PaintUI(DrawList);
        Check(DrawList.GetVertices().size() > 200, "The live HUD paints telemetry and controls");
        const FVector DemoBefore{RootDemo->GetOwner()->GetActorLocation()};
        Tick(World, 8);
        Check(std::abs(RootDemo->GetOwner()->GetActorLocation().X - DemoBefore.X) > 0.1f, "The root-motion demonstrator advances independently");
        Press(World, '1', 4);
        Press(World, 'B', 2);
        std::set<int32> GuidedStates{};

        for (uint32 Frame{}; Frame < 750; ++Frame) {
            Tick(World, 1);
            GuidedStates.insert(StateMachine(Hero)->GetCurrentState());
        }

        Check(GuidedStates.contains(0) && GuidedStates.contains(1) && GuidedStates.contains(2) && GuidedStates.contains(3), "The 25-second guided cycle demonstrates movement, actions, hits and dance");
        World->EndPlay();
        GEngine->DestroyWorldContext(World);
        FInputSystem::ClearAllStates();
        std::printf("PASS scene v5, real Lua input, clip browsing, playback controls, root motion and HUD.\n");
    }
}

int main(int ArgCount, char** ArgValues) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    FStageLog Log{};
    try {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        FPlatformTime::InitTiming();
        FRenderDevice Device{};
        RenderCommand::Init(&Device);
        FRenderResourceManager::Init();
        FGeometryGenerator::CreateDefaultMeshDatas();
        auto& Manager{UAssetManager::Get()};
        Manager.CreateDefaultTextures();
        Manager.CreateDefaultMaterial();
        Manager.CreateDefaultMeshes();
        Manager.CreateBillboardMaterial();
        Manager.CreateEditorArrowMaterial();
        Manager.LoadFontAtlas("Assets/Fonts/Pretendard.json", "Assets/Fonts/Pretendard.png");
        GEngine = NewObject<UGameEngine>();
        if (ArgCount >= 3 && FString{ArgValues[1]} == "--import") {
            ImportLibrary(ArgValues[2]);
        } else {
            VerifyStage(ArgCount >= 2 && FString{ArgValues[1]} == "--capture", ArgCount >= 2 && FString{ArgValues[1]} == "--preview");
        }
        Check(!Log.HasError(), "The stage runs without engine or Lua errors");
        FEngineLoop Loop{};
        Loop.Exit();
        Check(GUObjectArray.IsEmpty(), "The stage shuts down without registered object leaks");
        CoUninitialize();
        std::printf("PASS: Locomotion Stage.\n");
        return 0;
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "FAIL: %s\n", Error.what());
        return 1;
    }
}
