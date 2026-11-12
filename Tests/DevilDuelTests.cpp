#include "EnginePCH.h"
#include "Asset/AssetManager.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/CameraModifier_FOVPunch.h"
#include "Camera/PlayerCameraManager.h"
#include "Core/Windows/WindowsPlatformTime.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/InputSystem.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/GeometryGenerator.h"
#include "Render/RenderCommand.h"
#include "Render/RenderDevice.h"
#include "Render/RenderResourceManager.h"
#include "Render/Renderer.h"
#include "Render/SceneRenderer.h"
#include "Render/Texture2D.h"
#include "Script/LuaScriptManager.h"
#include "Serialization/JsonArchive.h"
#include "UI/UIDrawList.h"
#include "UI/UIRenderer.h"
#include <directxtk/ScreenGrab.h>
#include <wincodec.h>
#include <sol/sol.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace {
    class FTestLog final : public ILogSink {
    public:
        FTestLog();
        ~FTestLog();

    public:
        void OnLog(ELogVerbosity Verbosity, const FString& Message) override;
        bool HasFault() const;

    private:
        bool mFault{};
    };

    FTestLog::FTestLog() {
        FLog::AddSink(this);
    }

    FTestLog::~FTestLog() {
        FLog::RemoveSink(this);
    }

    void FTestLog::OnLog(ELogVerbosity Verbosity, const FString& Message) {
        if (Verbosity == ELogVerbosity::Warning || Verbosity == ELogVerbosity::Error || Message.find("PASS:") != FString::npos) {
            std::fprintf(stderr, "%s\n", Message.c_str());
        }
        mFault = mFault || Verbosity == ELogVerbosity::Error || Message.find("keeping default") != FString::npos || Message.find("Asset load failed") != FString::npos;
    }

    bool FTestLog::HasFault() const {
        return mFault;
    }

    void Check(bool Condition, const char* Message) {
        if (!Condition) {
            throw std::runtime_error{Message};
        }
        std::printf("PASS: %s\n", Message);
    }

    void RunLua(const FString& Code) {
        auto& Lua{FLuaScriptManager::Get().GetState()};
        sol::protected_function_result Result{Lua.safe_script(Code, sol::script_pass_on_error)};
        if (!Result.valid()) {
            const std::string Error{Result.get<std::string>()};
            throw std::runtime_error{Error};
        }
    }

    void Tick(UWorld* World, int Frames) {
        for (int Frame{}; Frame < Frames; ++Frame) {
            World->Tick(1.0f / 60.0f);
            FInputSystem::UpdateInputStates();
        }
    }

    bool LuaCondition(const FString& Condition) {
        auto& Lua{FLuaScriptManager::Get().GetState()};
        sol::protected_function_result Result{Lua.safe_script("return " + Condition, sol::script_pass_on_error)};
        if (!Result.valid()) {
            throw std::runtime_error{Result.get<std::string>()};
        }
        return Result.get<bool>();
    }

    void Until(UWorld* World, const FString& Condition, int MaxFrames = 240) {
        for (int Frame{}; Frame < MaxFrames; ++Frame) {
            if (LuaCondition(Condition)) {
                return;
            }
            Tick(World, 1);
        }
        throw std::runtime_error{"Timed out waiting for " + Condition};
    }

    void Press(UWorld* World, int Key, int Frames = 1) {
        FInputSystem::OnKeyDown(Key);
        Tick(World, Frames);
        FInputSystem::OnKeyUp(Key);
        Tick(World, 1);
    }

    AActor* FindActor(UWorld* World, const FString& Name) {
        for (AActor* Actor : World->GetPersistentLevel()->GetActors()) {
            if (Actor->GetName() == Name) {
                return Actor;
            }
        }
        return nullptr;
    }

    USkeletalMeshComponent* FindMesh(AActor* Actor) {
        for (UActorComponent* Component : Actor->GetComponents()) {
            if (auto* Mesh{Cast<USkeletalMeshComponent>(Component)}) {
                return Mesh;
            }
        }
        return nullptr;
    }

    void TestHitStopClock(UWorld* World, USkeletalMeshComponent* Mesh) {
        const float StartTime{Mesh->GetSingleNodeInstance()->GetCurrentTime()};
        World->StartHitStop(0.125f, 0.0f);
        World->Tick(0.20f);
        Check(std::abs(Mesh->GetSingleNodeInstance()->GetCurrentTime() - StartTime - 0.075f) < 0.0001f, "A long render frame advances only the time after hitstop expires");
        const float FrozenTime{Mesh->GetSingleNodeInstance()->GetCurrentTime()};
        World->StartHitStop(0.12f, 0.0f);
        World->StartHitStop(0.40f, 0.25f);
        World->Tick(0.10f);
        Check(World->GetEffectiveTimeDilation() == 0.0f && Mesh->GetSingleNodeInstance()->GetCurrentTime() == FrozenTime, "A weaker slowdown cannot replace or extend a full impact freeze");
        World->Tick(0.05f);
        Check(World->GetEffectiveTimeDilation() == 1.0f && std::abs(Mesh->GetSingleNodeInstance()->GetCurrentTime() - FrozenTime - 0.03f) < 0.0001f, "The simulation resumes automatically after the exact remaining freeze time");
    }

    void Capture(UWorld* World, FGameViewportClient& Viewport, const wchar_t* Path) {
        FRenderer Renderer{};
        Check(Renderer.Init(), "Initialize offscreen rendering");
        Renderer.InitScenePasses("");
        FUIRenderer UI{};
        UI.Init();
        FSceneRenderOptions Options{};
        Options.RenderingPath = ERenderingPath::Forward;
        Options.DirectionalShadowMapSize = 1024;
        const FSceneView View{Viewport.CalcSceneView(FRect{0.0f, 0.0f, 1600.0f, 900.0f})};
        const auto* Player{FindActor(World, "Duel Player")};
        const auto* Rival{FindActor(World, "Duel Rival")};
        const FVector TowardFighters{(Player->GetActorLocation() + Rival->GetActorLocation()) * 0.5f + FVector{0.0f, 0.0f, 1.0f} - View.ViewLocation};
        Check(TowardFighters.Normalized().Dot(View.ViewForward) > 0.96f, "The active camera frames both fighters");
        FSceneRenderer Scene{World, View, Viewport.GetRenderData()};
        Scene.InitViews(&Renderer, Options);
        Check(Scene.Render(&Renderer, Viewport.GetViewTargets(), Options, [&Viewport, &UI](FScenePassCallbacks& Callbacks) {
            Callbacks.Overlays.GameUI = [&Viewport, &UI]() {
                FUIDrawList DrawList{};
                Viewport.PaintUI(DrawList);
                UI.Render(DrawList, 1600, 900);
            };
        }), "Render arena, animated fighters and HUD");
        Check(SUCCEEDED(DirectX::SaveWICTextureToFile(FRenderDevice::Get()->GetContext(), Viewport.GetColorTarget()->GetRawPtr(), GUID_ContainerFormatPng, Path)), "Save gameplay capture");
    }

    void TestRuntime() {
        auto* Editor{UWorld::CreateWorld(EWorldType::Editor, false)};
        Check(FJsonArchive::LoadWorld(Editor, "Scenes/DevilDuel.scene"), "Load Devil Duel scene");
        const AActor* Street{FindActor(Editor, "Neon Backstreet")};
        Check(Street != nullptr, "Load the downloaded city environment");
        int32 EnvironmentParts{};
        for (const UActorComponent* Component : Street->GetComponents()) {
            if (const auto* StaticMesh{Cast<UStaticMeshComponent>(Component)}) {
                if (!StaticMesh->GetStaticMesh() || StaticMesh->GetStaticMesh()->GetPath().find("DevilDuel/Environment/") == FString::npos) {
                    throw std::runtime_error{"Environment part was replaced with a default mesh"};
                }
                ++EnvironmentParts;
            }
        }
        Check(EnvironmentParts > 350, "The alley contains actual modular architecture and props");
        auto* World{UWorld::GetDuplicatedWorldForPIE(Editor)};
        Check(World != nullptr, "Duplicate duel into a playable world");
        FWorldContext& Context{GEngine->CreateNewWorldContext(EWorldType::PIE)};
        Context.SetCurrentWorld(World);
        Context.GameViewport = MakeShared<FGameViewportClient>();
        Context.GameViewport->Init(Context, GEngine);
        Context.GameViewport->Resize(1600, 900);
        World->BeginPlay();
        GEngine->DestroyWorld(Editor);
        Tick(World, 135);
        auto* PC{World->GetPlayerController()};
        auto* PCM{PC ? PC->GetCameraManager() : nullptr};
        Check(PCM && PCM->HasCameraCache() && !PC->GetPawn(), "A camera manager drives modifiers without moving the gameplay camera through pawn input");
        RunLua("assert(DevilDuel and DevilDuel.Phase == 'Fight', 'The match starts after its countdown')");
        Press(World, 'T');
        RunLua("assert(not DevilDuel.AI, 'T disables CPU for training')");
        AActor* Player{FindActor(World, "Duel Player")};
        AActor* Rival{FindActor(World, "Duel Rival")};
        auto* Mesh{FindMesh(Player)};
        Check(Mesh && FindMesh(Rival) && Mesh->GetAnimInstance(), "Both Devil Jin meshes play compatible animations");
        TestHitStopClock(World, Mesh);
        auto* Step{UAssetManager::GetAssetByPath<UAnimationAsset>("DevilDuel/Motion/Long Step Forward.fbxbin")};
        const FVector RootStart{Player->GetActorLocation()};
        FString RootError{};
        Mesh->SetRootMotionEnabled(true, false);
        Check(Mesh->PlayAnimation(Step, false, 1.0f, RootError), "Play a clip with an extracted root track");
        Mesh->TickComponent(0.25f);
        const FVector Extracted{Mesh->ConsumeRootMotionTranslation()};
        Check(Extracted.X > 0.05f && (Player->GetActorLocation() - RootStart).Length() < 0.001f, "Controller mode exposes real root movement without moving the actor twice");
        Check(Mesh->ConsumeRootMotionTranslation().Length() == 0.0f, "Root movement can only be consumed once");
        Mesh->SetRootMotionEnabled(true);
        Check(Mesh->PlayAnimation(Step, false, 1.0f, RootError), "Replay the same root-motion clip in actor mode");
        Mesh->TickComponent(0.25f);
        Check((Player->GetActorLocation() - RootStart - Extracted).Length() < 0.001f, "Default actor mode applies the same native root movement");
        Player->GetRootComponent()->SetWorldLocation(RootStart, false);
        Mesh->SetRootMotionEnabled(false, false);
        auto* Idle{UAssetManager::GetAssetByPath<UAnimationAsset>("DevilDuel/Motion/Bouncing Fight Idle.fbxbin")};
        Check(Mesh->PlayAnimation(Idle, true, 1.0f, RootError), "Restore the fighter's idle pose after root-motion checks");
        const int32 HandIndex{Mesh->FindBoneIndex("Hand base right")};
        Check(HandIndex >= 0, "The imported fighter retains its hand bones");
        const FVector Nose{Mesh->GetBoneWorldMatrix(Mesh->FindBoneIndex("Head nose left upper")).TransformPosition(FVector{})};
        const FVector Head{Mesh->GetBoneWorldMatrix(Mesh->FindBoneIndex("Head neck upper")).TransformPosition(FVector{})};
        Check(Nose.X - Head.X > 0.08f, "The fighter model faces the opponent");
        const int32 HipIndex{Mesh->FindBoneIndex("Root hips")};
        const float IdleHeight{Mesh->GetBoneWorldMatrix(HipIndex).TransformPosition(FVector{}).Z};
        Check(Mesh->PlayAnimation(Step, false, 0.0f, RootError), "Inspect the beginning of the walking loop");
        const float StepStartHeight{Mesh->GetBoneWorldMatrix(HipIndex).TransformPosition(FVector{}).Z};
        Check(Mesh->PlayAnimation(Step, false, 0.0f, RootError, 0.0f, 1000.0f), "Inspect the end of the walking loop");
        Check(std::abs(Mesh->GetBoneWorldMatrix(HipIndex).TransformPosition(FVector{}).Z - StepStartHeight) < 0.005f, "Walking loop endpoints have matching hip height without a vertical pop");
        auto* GetUp{UAssetManager::GetAssetByPath<UAnimationAsset>("DevilDuel/AS_GetUp.fbxbin")};
        FString PoseError{};
        Check(Mesh->PlayAnimation(GetUp, false, 0.0f, PoseError, 0.0f, 1000.0f), "Load the aligned get-up animation");
        const float GetUpHeight{Mesh->GetBoneWorldMatrix(HipIndex).TransformPosition(FVector{}).Z};
        Check(std::abs(GetUpHeight - IdleHeight) < 0.03f, "The get-up animation returns to the idle floor height");
        const FVector BeforeHand{Mesh->GetBoneWorldMatrix(HandIndex).TransformPosition(FVector{})};
        auto* Animation{UAssetManager::GetAssetByPath<UAnimationAsset>("Imported/Hook Punch/AS_Hook Punch_mixamo.com__Devil Jin.fbxbin")};
        FString Error{};
        Check(Mesh->PlayAnimation(Animation, false, 2.0f, Error, 0.15f), "Start an interruptible animation blend");
        const FVector AfterHand{Mesh->GetBoneWorldMatrix(HandIndex).TransformPosition(FVector{})};
        Check((AfterHand - BeforeHand).Length() < 0.001f, "Animation changes preserve the outgoing pose at blend start");
        const FVector Start{Player->GetActorLocation()};
        Press(World, 'W', 36);
        Check((Player->GetActorLocation() - Start).Length() > 0.45f, "W advances toward the opponent");
        const FVector BeforeStep{Player->GetActorLocation()};
        FInputSystem::OnKeyDown('W');
        Tick(World, 2);
        Check(std::abs(Mesh->GetSingleNodeInstance()->GetPlayRate() - 1.10f) < 0.01f, "Forward stepping uses a natural animation cadence");
        FInputSystem::OnKeyUp('W');
        Tick(World, 1);
        Press(World, 'A', 40);
        Check(std::abs(Player->GetActorLocation().Y - BeforeStep.Y) > 0.15f, "A sidesteps using extracted root motion");
        RunLua("assert(DevilDuel.RootMotion and DevilDuel.RootMotionDistance > 0.4, 'The gameplay controller consumes native animation root motion')");
        Tick(World, 65);
        Capture(World, *Context.GameViewport, L"Build/DevilDuelTests/arena.png");
        RunLua("local M=DevilDuel; M.Fighters[1].X=-0.7; M.Fighters[1].Y=0; M.Fighters[2].X=0.7; M.Fighters[2].Y=0");
        Tick(World, 2);
        Press(World, 'J');
        Until(World, "DevilDuel.Fighters[2].Health == 133");
        RunLua("assert(DevilDuel.Fighters[2].Health == 133, 'Actual J input lands one jab')");
        Check(World->GetEffectiveTimeDilation() == 0.0f, "A confirmed hit completely stops simulation time");
        Check(PCM->FindCameraModifierByClass(UCameraModifier_CameraShake::StaticClass()) && PCM->FindCameraModifierByClass(UCameraModifier_FOVPunch::StaticClass()), "Hits use the existing shake and FOV modifiers");
        Check(PCM->GetCameraCachePOV().FOV < 57.5f, "A light hit produces a visible FOV punch on the rendered camera");
        const float ImpactAnimationTime{Mesh->GetSingleNodeInstance()->GetCurrentTime()};
        auto* RivalMesh{FindMesh(Rival)};
        const float RivalImpactTime{RivalMesh->GetSingleNodeInstance()->GetCurrentTime()};
        const FVector PlayerImpactPosition{Player->GetActorLocation()};
        const FVector RivalImpactPosition{Rival->GetActorLocation()};
        RunLua("FrozenDuelTime = DevilDuel.Time; FrozenAttackAge = DevilDuel.Fighters[1].Age");
        const float ImpactFOV{PCM->GetCameraCachePOV().FOV};
        Tick(World, 5);
        Check(Mesh->GetSingleNodeInstance()->GetCurrentTime() == ImpactAnimationTime && RivalMesh->GetSingleNodeInstance()->GetCurrentTime() == RivalImpactTime, "Both fighters retain the exact impact frame for at least five render frames");
        Check((Player->GetActorLocation() - PlayerImpactPosition).Length() < 0.00001f && (Rival->GetActorLocation() - RivalImpactPosition).Length() < 0.00001f, "Hitstop also freezes root movement and knockback");
        RunLua("assert(DevilDuel.Time == FrozenDuelTime and DevilDuel.Fighters[1].Age == FrozenAttackAge, 'Hitstop preserves the round timer and combat windows')");
        Check(std::abs(PCM->GetCameraCachePOV().FOV - ImpactFOV) > 0.005f, "Camera modifiers continue on real time during hitstop");
        Capture(World, *Context.GameViewport, L"Build/DevilDuelTests/hit.png");
        Tick(World, 65);
        RunLua("assert(DevilDuel.Fighters[2].Health == 133, 'Actual attack cannot hit repeatedly')");
        RunLua("local R=dofile('Scripts/devil_duel_rules.lua'); R.ResetRound(DevilDuel); DevilDuel.Phase='Fight'; DevilDuel.AI=false; DevilDuel.Fighters[1].X=-0.7; DevilDuel.Fighters[2].X=0.7");
        Tick(World, 2);
        Press(World, 'J');
        Until(World, "DevilDuel.Fighters[2].Health < 140");
        Check(World->GetEffectiveTimeDilation() == 0.0f, "Combo input can be buffered during a full impact freeze");
        Press(World, 'J');
        RunLua("assert(DevilDuel.Fighters[1].Attack.Name == 'Jab' and DevilDuel.Fighters[1].Buffer.Button == 'J', 'Frozen frames retain the buffered follow-up')");
        Until(World, "DevilDuel.Fighters[1].Attack and DevilDuel.Fighters[1].Attack.Name == 'Cross'");
        Press(World, 'K');
        Until(World, "DevilDuel.Fighters[2].Combo == 3");
        RunLua("assert(DevilDuel.Fighters[2].Combo == 3, 'Actual J J K inputs perform a three-hit string')");
        Capture(World, *Context.GameViewport, L"Build/DevilDuelTests/combo.png");
        Press(World, VK_TAB);
        RunLua("assert(not DevilDuel.Paused, 'Removed help UI cannot pause or cover combat')");
        RunLua("local R=dofile('Scripts/devil_duel_rules.lua'); R.ResetRound(DevilDuel); DevilDuel.Phase='Fight'; DevilDuel.AI=false; DevilDuel.Fighters[1].X=-0.7; DevilDuel.Fighters[2].X=0.7");
        Tick(World, 2);
        FInputSystem::OnKeyDown('W');
        Press(World, 'K');
        FInputSystem::OnKeyUp('W');
        Until(World, "DevilDuel.Fighters[2].State == 'Air' and DevilDuel.Fighters[2].Z > 0");
        RunLua("assert(DevilDuel.Fighters[2].State == 'Air' and DevilDuel.Fighters[2].Z > 0, 'Actual W+K launches the opponent')");
        Capture(World, *Context.GameViewport, L"Build/DevilDuelTests/launch.png");
        RunLua("local R=dofile('Scripts/devil_duel_rules.lua'); R.ResetRound(DevilDuel); DevilDuel.Phase='Fight'; DevilDuel.AI=false; DevilDuel.Fighters[1].X=-0.7; DevilDuel.Fighters[2].X=0.7; R.Attack(DevilDuel,2,'Jab')");
        Press(World, VK_SHIFT, 28);
        RunLua("assert(DevilDuel.Fighters[1].Health == 140 and DevilDuel.Fighters[1].Guard < 100, 'Actual Shift input guards an incoming attack')");
        RunLua("local R=dofile('Scripts/devil_duel_rules.lua'); R.ResetRound(DevilDuel); DevilDuel.Phase='Fight'; DevilDuel.AI=false; DevilDuel.Fighters[1].X=-0.7; DevilDuel.Fighters[2].X=0.7; DevilDuel.Fighters[2].Guarding=true; DevilDuel.Fighters[2].State='Guard'");
        Tick(World, 2);
        Press(World, 'J');
        Until(World, "DevilDuel.Fighters[2].Guard < 100");
        RunLua("assert(DevilDuel.Fighters[2].Health == 140 and DevilDuel.Fighters[2].Guard < 100, 'Actual guard blocks the jab')");
        Press(World, 'P');
        RunLua("DuelPausedTime = DevilDuel.Time; assert(DevilDuel.Paused)");
        const float PausedAnimationTime{Mesh->GetSingleNodeInstance()->GetCurrentTime()};
        Tick(World, 60);
        Check(Mesh->GetSingleNodeInstance()->GetCurrentTime() == PausedAnimationTime, "Pause freezes native animation playback");
        RunLua("assert(DevilDuel.Time == DuelPausedTime, 'P freezes the round')");
        Press(World, 'P');
        const char* Techniques[]{"Jab", "Cross", "Hook", "Kick", "RoundKick", "Uppercut", "Knee", "Elbow", "FlyingKick", "SpinKick", "DropKick"};
        for (const auto& Technique : Techniques) {
            RunLua("local R=dofile('Scripts/devil_duel_rules.lua'); R.ResetRound(DevilDuel); DevilDuel.Phase='Fight'; DevilDuel.AI=false; DevilDuel.Fighters[1].X=-0.7; DevilDuel.Fighters[2].X=0.7");
            Tick(World, 2);
            RunLua(FString{"assert(dofile('Scripts/devil_duel_rules.lua').Attack(DevilDuel,1,'"} + Technique + "'))");
            Until(World, "DevilDuel.Fighters[2].Health < 140");
            RunLua(FString{"assert(DevilDuel.Fighters[2].Health < 140, 'Native root-motion attack failed: "} + Technique + "')");
            if (FString{Technique} == "Kick") {
                Capture(World, *Context.GameViewport, L"Build/DevilDuelTests/kick.png");
            } else if (FString{Technique} == "FlyingKick") {
                Capture(World, *Context.GameViewport, L"Build/DevilDuelTests/flying-kick.png");
            } else if (FString{Technique} == "SpinKick") {
                Capture(World, *Context.GameViewport, L"Build/DevilDuelTests/spin-kick.png");
            }
            const float FullLength{Mesh->GetSingleNodeInstance()->GetLength()};
            float FurthestTime{Mesh->GetSingleNodeInstance()->GetCurrentTime()};
            for (int Frame{}; Frame < 240 && LuaCondition("DevilDuel.Fighters[1].State == 'Attack'"); ++Frame) {
                FurthestTime = std::max(FurthestTime, Mesh->GetSingleNodeInstance()->GetCurrentTime());
                Tick(World, 1);
            }
            Check(!LuaCondition("DevilDuel.Fighters[1].State == 'Attack'") && FurthestTime >= FullLength - 0.02f, "Uninterrupted attacks reach the final source frame before returning to idle");
        }
        Check(true, "All eleven techniques connect with actual animation root movement");
        Press(World, 'R');
        Tick(World, 180);
        RunLua("assert(DevilDuel.AI and DevilDuel.Wins[1] == 0 and DevilDuel.Wins[2] == 0, 'Restart restores the match and CPU')");
        Tick(World, 900);
        RunLua("assert(DevilDuel.Fighters[1].Health < 140 or DevilDuel.Wins[2] > 0, 'Actual CPU approaches and attacks')");
        Press(World, 'R');
        Tick(World, 130);
        RunLua("DevilDuel.Fighters[2].Health=0");
        Tick(World, 1);
        RunLua("assert(DevilDuel.Phase == 'RoundOver' and DevilDuel.Wins[1] == 1, 'KO awards a round')");
        Until(World, "DevilDuel.Phase == 'Fight' and DevilDuel.Round == 2", 480);
        RunLua("assert(DevilDuel.Phase == 'Fight' and DevilDuel.Round == 2, 'Next round starts automatically'); DevilDuel.Fighters[2].Health=0");
        Until(World, "DevilDuel.Phase == 'MatchOver'", 360);
        RunLua("assert(DevilDuel.Phase == 'MatchOver' and DevilDuel.Wins[1] == 2, 'Two round wins finish the match')");
        Press(World, 'R');
        RunLua("assert(DevilDuel.Phase == 'Ready' and DevilDuel.Wins[1] == 0, 'R starts a rematch')");
        FUIDrawList DrawList{};
        Context.GameViewport->PaintUI(DrawList);
        Check(DrawList.GetIndices().Num() > 100, "HUD produces visible geometry");
        GEngine->DestroyWorld(World);
        RunLua("assert(DevilDuel == nil, 'EndPlay releases shared match state')");
        FInputSystem::ClearAllStates();
    }
}

int main() {
    FTestLog Log{};
    try {
        const HRESULT ComResult{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
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
        Manager.ScanAssets("Assets", {});
        Manager.LoadFontAtlas("Assets/Fonts/Pretendard.json", "Assets/Fonts/Pretendard.png");
        GEngine = NewObject<UGameEngine>();
        RunLua("dofile('Tests/DevilDuelRulesTests.lua')");
        TestRuntime();
        Check(!Log.HasFault(), "No engine or Lua errors during gameplay");
        FEngineLoop Loop{};
        Loop.Exit();
        Check(GEngine == nullptr && GUObjectArray.Num() == 0, "Clean engine shutdown");
        if (SUCCEEDED(ComResult)) {
            CoUninitialize();
        }
        std::printf("PASS: Devil Duel rules, real inputs, animation, camera, HUD, AI and rounds.\n");
        return 0;
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "FAIL: %s\n", Error.what());
        return 1;
    }
}
