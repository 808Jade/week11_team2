#include "EnginePCH.h"
#include "Animation/AnimAssetArchive.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimNode_TwoWayBlend.h"
#include "Animation/AnimationRuntime.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/EngineStatics.h"
#include "Core/Windows/WindowsPlatformTime.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Input/InputSystem.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/GeometryGenerator.h"
#include "Render/RenderCommand.h"
#include "Render/RenderDevice.h"
#include "Render/RenderResourceManager.h"
#include "Render/SkeletalMeshObject.h"
#include "Serialization/JsonArchive.h"
#include "UI/UIDrawList.h"
#include "UObject/UObjectGlobals.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
    class FTestLogSink final : public ILogSink {
    public:
        FTestLogSink();
        ~FTestLogSink();

    public:
        void OnLog(ELogVerbosity Verbosity, const FString& Message) override;
        bool HasScriptFault() const;

    private:
        bool mScriptFault{};
    };

    FTestLogSink::FTestLogSink() {
        FLog::AddSink(this);
    }

    FTestLogSink::~FTestLogSink() {
        FLog::RemoveSink(this);
    }

    void FTestLogSink::OnLog(ELogVerbosity Verbosity, const FString& Message) {
        if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Warning) {
            std::fprintf(stderr, "%s\n", Message.c_str());
        }
        mScriptFault |= Verbosity == ELogVerbosity::Error && Message.find("[Lua]") != FString::npos;
    }

    bool FTestLogSink::HasScriptFault() const {
        return mScriptFault;
    }

    class FTestInstance final : public UAnimInstance {
    public:
        void SetRoot(FAnimNode_Base* Root);
        void SetFlag(bool Value);
        bool GetBoolVariable(const FString& Name) const override;
        bool HasGraphVariable(const FString& Name, bool IsBool) const override;

    private:
        bool mFlag{};
    };

    // 누적 시간과 고정 위치 포즈로 상태 갱신 및 블렌딩을 측정하는 테스트 노드이다.
    class FProbeNode final : public FAnimNode_Base {
    public:
        explicit FProbeNode(float Location);

    public:
        void Initialize(UAnimInstance& Instance) override;
        void Update(const FAnimationUpdateContext& Context) override;
        void Evaluate(FPoseContext& Output) override;
        const FString& GetNodeType() const override;
        float GetTime() const;

    private:
        float mLocation{};
        float mTime{};
    };

    void FTestInstance::SetRoot(FAnimNode_Base* Root) {
        SetRootNode(Root);
    }

    void FTestInstance::SetFlag(bool Value) {
        mFlag = Value;
    }

    bool FTestInstance::GetBoolVariable(const FString& Name) const {
        return Name == "Flag" && mFlag;
    }

    bool FTestInstance::HasGraphVariable(const FString& Name, bool IsBool) const {
        return Name == "Flag" && IsBool;
    }

    FProbeNode::FProbeNode(float Location)
        : mLocation{Location} {
    }

    void FProbeNode::Initialize(UAnimInstance& Instance) {
        FAnimNode_Base::Initialize(Instance);
        mTime = 0.0f;
    }

    void FProbeNode::Update(const FAnimationUpdateContext& Context) {
        mTime += Context.GetDeltaTime();
    }

    void FProbeNode::Evaluate(FPoseContext& Output) {
        Output.mPose.SetNum(1);
        Output.mPose[0].Location = {mLocation, 0.0f, 0.0f};
    }

    const FString& FProbeNode::GetNodeType() const {
        static const FString Type{"Probe"};
        return Type;
    }

    float FProbeNode::GetTime() const {
        return mTime;
    }

    void Check(bool Passed, const FString& Message) {
        if (!Passed) {
            throw std::runtime_error{Message};
        }
        std::printf("PASS %s\n", Message.c_str());
        std::fflush(stdout);
    }

    bool Near(float A, float B) {
        return std::abs(A - B) < 0.0001f;
    }

    void SetFlagRule(FAnimTransitionGraph& Rule) {
        const int32 Getter{Rule.AddNode(EAnimRuleNodeType::GetBool, 0.0f, 0.0f)};
        Rule.FindNode(Getter)->mName = "Flag";
        Rule.SetResult(Getter);
    }

    void TestRulesAndRuntime() {
        // 잘못된 규칙 타입, 비활성 상태 정지, 전환 진행률과 공유 노드의 단일 갱신을 확인한다.
        FAnimTransitionGraph Rule{};
        const int32 Number{Rule.AddNode(EAnimRuleNodeType::Float, 0.0f, 0.0f)};
        const int32 Not{Rule.AddNode(EAnimRuleNodeType::Not, 200.0f, 0.0f)};
        Check(!Rule.Connect(Number, Not, 0), "Rule pins reject incompatible types");
        Rule.SetResult(Not);
        FTestInstance Instance{};
        const FAnimationUpdateContext Context{Instance, 0.1f};
        Check(!FAnimTransitionEvaluator::Evaluate(Rule, Context, nullptr, 0.0f), "Disconnected Not evaluates to an invalid transition");
        UAnimGraph Graph{};
        const int32 Node{Graph.AddNode("StateMachine", "Test Machine", 0.0f, 0.0f)};
        auto* Data{Graph.FindStateMachine(Node)};
        const int32 First{Data->AddState("First", 0.0f, 0.0f, Graph)};
        const int32 Second{Data->AddState("Second", 250.0f, 0.0f, Graph)};
        const int32 Transition{Data->AddTransition(First, Second)};
        SetFlagRule(Data->FindTransition(Transition)->mRule);
        Data->FindTransition(Transition)->mDuration = 0.2f;
        FProbeNode A{0.0f};
        FProbeNode B{10.0f};
        FAnimNode_StateMachine Machine{};
        Machine.Configure(*Data);
        Machine.AddState(*Data->FindState(First), &A);
        Machine.AddState(*Data->FindState(Second), &B);
        Machine.Initialize(Instance);
        Instance.SetRoot(&Machine);
        Instance.UpdateAnimation(0.1f);
        Check(Near(B.GetTime(), 0.0f), "Inactive states stop advancing");
        Instance.SetFlag(true);
        Instance.UpdateAnimation(0.1f);
        Instance.UpdateAnimation(0.1f);
        TArray<FTransform> PoseA{};
        TArray<FTransform> PoseB{};
        TArray<FTransform> Output{};
        PoseA.SetNum(1);
        PoseB.SetNum(1);
        PoseB[0].Location = {10.0f, 0.0f, 0.0f};
        FAnimationRuntime::BlendTwoPosesTogether(PoseA, PoseB, 1.0f - Machine.GetBlendAlpha(), Output);
        Check(Machine.GetTargetState() == Second && Near(Machine.GetBlendAlpha(), 0.5f) && Output.Num() == 1 && Near(Output[0].Location.X, 5.0f), "Transition blends both poses at the expected weight");
        Check(Machine.GetActiveTransition() == Transition && Near(Machine.FindState(Second)->mElapsed, 0.1f), "Live debug identifies the exact transition and target playback time");
        Instance.UpdateAnimation(0.1f);
        Check(Machine.GetCurrentState() == Second && Machine.GetTargetState() == -1, "Transition commits the target state");
        Check(Machine.GetActiveTransition() == -1, "Completed transitions stop appearing as active debug edges");
        FAnimNode_TwoWayBlend SharedBlend{};
        FProbeNode Shared{0.0f};
        SharedBlend.mA.mLinkedNode = &Shared;
        SharedBlend.mB.mLinkedNode = &Shared;
        SharedBlend.mAlpha = 0.5f;
        SharedBlend.Initialize(Instance);
        Instance.SetRoot(&SharedBlend);
        Instance.UpdateAnimation(0.1f);
        Check(Near(Shared.GetTime(), 0.1f), "Shared pose nodes advance once per frame");
    }

    void TestTransitionInterrupt() {
        UAnimGraph Graph{};
        const int32 Node{Graph.AddNode("StateMachine", "Interrupt Machine", 0.0f, 0.0f)};
        auto* Data{Graph.FindStateMachine(Node)};
        const int32 First{Data->AddState("First", 0.0f, 0.0f, Graph)};
        const int32 Second{Data->AddState("Second", 250.0f, 0.0f, Graph)};
        const int32 Third{Data->AddState("Third", 500.0f, 0.0f, Graph)};
        const int32 Enter{Data->AddTransition(First, Second)};
        SetFlagRule(Data->FindTransition(Enter)->mRule);
        Data->FindTransition(Enter)->mDuration = 0.4f;
        const int32 Leave{Data->AddTransition(Second, Third)};
        auto& LeaveRule{Data->FindTransition(Leave)->mRule};
        const int32 Getter{LeaveRule.AddNode(EAnimRuleNodeType::GetBool, 0.0f, 0.0f)};
        const int32 Not{LeaveRule.AddNode(EAnimRuleNodeType::Not, 200.0f, 0.0f)};
        LeaveRule.FindNode(Getter)->mName = "Flag";
        LeaveRule.Connect(Getter, Not, 0);
        LeaveRule.SetResult(Not);
        Data->FindTransition(Leave)->mDuration = 0.4f;

        FTestInstance Instance{};
        FProbeNode A{0.0f};
        FProbeNode B{10.0f};
        FProbeNode C{20.0f};
        FAnimNode_StateMachine Machine{};
        Machine.Configure(*Data);
        Machine.AddState(*Data->FindState(First), &A);
        Machine.AddState(*Data->FindState(Second), &B);
        Machine.AddState(*Data->FindState(Third), &C);
        Machine.Initialize(Instance);
        Instance.SetRoot(&Machine);
        auto Evaluate{[&]() {
            FPoseContext Output{Instance};
            Machine.Evaluate(Output);
            return Output.mPose.Num() == 1 ? Output.mPose[0].Location.X : -1.0f;
        }};

        Instance.SetFlag(true);
        Instance.UpdateAnimation(0.1f);
        Instance.UpdateAnimation(0.1f);
        Check(Machine.GetTargetState() == Second && Near(Evaluate(), 2.5f), "First -> Second is a quarter of the way through its blend");
        Instance.SetFlag(false);
        Instance.UpdateAnimation(0.1f);
        Check(Machine.GetTargetState() == Third && Machine.GetActiveTransition() == Leave, "A transition out of the blend target interrupts the running blend");
        Check(Near(Evaluate(), 2.5f), "The interrupting blend starts from the pose that was on screen");
        Instance.UpdateAnimation(0.1f);
        Check(Near(Evaluate(), 2.5f + (20.0f - 2.5f) * 0.25f), "The interrupting blend moves from the frozen pose toward the new target");
        Instance.UpdateAnimation(0.3f);
        Check(Machine.GetCurrentState() == Third && Machine.GetTargetState() < 0 && Near(Evaluate(), 20.0f), "The interrupting blend commits the new target");
    }

    void TestAnyState() {
        UAnimGraph Graph{};
        const int32 Node{Graph.AddNode("StateMachine", "Any State Machine", 0.0f, 0.0f)};
        auto* Data{Graph.FindStateMachine(Node)};
        const int32 First{Data->AddState("First", 0.0f, 0.0f, Graph)};
        const int32 Second{Data->AddState("Second", 250.0f, 0.0f, Graph)};
        const int32 Hit{Data->AddState("Hit", 500.0f, 0.0f, Graph)};
        const int32 Global{Data->AddTransition(FAnimStateMachineData::AnyStateId, Hit)};
        Check(Global >= 0 && Data->GetStates().size() == 3, "Any State creates a shared transition without a pose state");
        Check(Data->AddTransition(First, FAnimStateMachineData::AnyStateId) < 0 && Data->AddTransition(-2, Hit) < 0 && !Data->SetEntryState(FAnimStateMachineData::AnyStateId), "Any State is an outgoing source only and cannot be the entry state");
        SetFlagRule(Data->FindTransition(Global)->mRule);
        Data->FindTransition(Global)->mDuration = 0.2f;

        for (const int32 Start : {First, Second}) {
            FTestInstance Instance{};
            FProbeNode A{0.0f};
            FProbeNode B{10.0f};
            FProbeNode C{20.0f};
            FAnimNode_StateMachine Machine{};
            Data->SetEntryState(Start);
            Machine.Configure(*Data);
            Machine.AddState(*Data->FindState(First), &A);
            Machine.AddState(*Data->FindState(Second), &B);
            Machine.AddState(*Data->FindState(Hit), &C);
            Machine.Initialize(Instance);
            Instance.SetRoot(&Machine);
            Instance.UpdateAnimation(0.1f);
            Check(Machine.GetCurrentState() == Start && Machine.GetTargetState() < 0, "Any State still requires its transition rule");
            Instance.SetFlag(true);
            Instance.UpdateAnimation(0.1f);
            Check(Machine.GetTargetState() == Hit && Machine.GetActiveTransition() == Global, "The same Any State edge starts from either active source");
            Instance.UpdateAnimation(0.1f);
            Check(Machine.GetCurrentState() == Start && Near(Machine.GetBlendAlpha(), 0.5f) && Near(Machine.FindState(Start)->mElapsed, 0.3f) && Near(C.GetTime(), 0.1f), "Any State blends from the actual source and advances both playback times");
            Instance.UpdateAnimation(0.1f);
            Instance.UpdateAnimation(0.1f);
            Check(Machine.GetCurrentState() == Hit && Machine.GetTargetState() < 0 && Near(C.GetTime(), 0.3f), "A persistent Any State condition does not restart its active target");
        }

        const int32 Local{Data->AddTransition(First, Second)};
        SetFlagRule(Data->FindTransition(Local)->mRule);
        Data->FindTransition(Local)->mDuration = 0.0f;
        Data->SetEntryState(First);
        FTestInstance Instance{};
        FProbeNode A{0.0f};
        FProbeNode B{10.0f};
        FProbeNode C{20.0f};
        FAnimNode_StateMachine Machine{};
        Machine.AddState(*Data->FindState(First), &A);
        Machine.AddState(*Data->FindState(Second), &B);
        Machine.AddState(*Data->FindState(Hit), &C);
        Instance.SetRoot(&Machine);
        Instance.SetFlag(true);
        Data->FindTransition(Local)->mPriority = -1;
        Machine.Configure(*Data);
        Machine.Initialize(Instance);
        Instance.UpdateAnimation(0.1f);
        Check(Machine.GetCurrentState() == Second, "An ordinary transition can outrank an Any State transition");
        Data->FindTransition(Local)->mPriority = 0;
        Machine.Configure(*Data);
        Machine.Initialize(Instance);
        Instance.UpdateAnimation(0.1f);
        Check(Machine.GetTargetState() == Hit, "Equal priorities retain creation order across shared and ordinary transitions");

        Data->FindTransition(Global)->mRule = {};
        auto& Rule{Data->FindTransition(Global)->mRule};
        const int32 Time{Rule.AddNode(EAnimRuleNodeType::StateTime, 0.0f, 0.0f)};
        const int32 Limit{Rule.AddNode(EAnimRuleNodeType::Float, 0.0f, 120.0f)};
        const int32 Compare{Rule.AddNode(EAnimRuleNodeType::Greater, 200.0f, 0.0f)};
        Rule.FindNode(Limit)->mValue = 0.15f;
        Rule.Connect(Time, Compare, 0);
        Rule.Connect(Limit, Compare, 1);
        Rule.SetResult(Compare);
        Instance.SetFlag(false);
        Machine.Configure(*Data);
        Machine.Initialize(Instance);
        Instance.UpdateAnimation(0.1f);
        Check(Machine.GetTargetState() < 0, "Any State time rules wait for the current state's elapsed time");
        Instance.UpdateAnimation(0.1f);
        Check(Machine.GetTargetState() == Hit, "Any State time rules evaluate the current source state");

        Check(Data->SetAnyStatePosition(-240.0f, 190.0f) && !Data->SetAnyStatePosition(std::numeric_limits<float>::infinity(), 0.0f), "Any State layout rejects non-finite coordinates");
        FAnimAssetArchive Save{false};
        Graph.Serialize(Save);
        Check(!Save.HasError() && Save.Save("Build/AnyStateRoundTrip.animgraph"), "Save Any State transitions and layout");
        FAnimAssetArchive Load{true};
        UAnimGraph Restored{};
        Check(Load.Load("Build/AnyStateRoundTrip.animgraph"), "Read Any State asset data");
        Restored.Serialize(Load);
        const auto* RestoredData{Restored.FindStateMachine(Node)};
        Check(!Load.HasError() && RestoredData != nullptr && RestoredData->FindTransition(Global)->mFromState == FAnimStateMachineData::AnyStateId && Near(RestoredData->GetAnyStateX(), -240.0f) && Near(RestoredData->GetAnyStateY(), 190.0f), "Any State source and position survive serialization");
        UAnimGraph Copy{};
        Copy.CopyFrom(Restored);
        auto* CopyData{Copy.FindStateMachine(Node)};
        Check(Near(CopyData->GetAnyStateX(), -240.0f) && CopyData->FindTransition(Global) != nullptr, "Editing copies preserve Any State layout and transitions");
        CopyData->RemoveState(Hit);
        Check(CopyData->FindTransition(Global) == nullptr && RestoredData->FindTransition(Global) != nullptr, "Deleting a target removes its Any State edges only in the edited graph");
        Data->FindTransition(Global)->mToState = FAnimStateMachineData::AnyStateId;
        FAnimAssetArchive Invalid{false};
        Graph.Serialize(Invalid);
        Check(Invalid.HasError(), "Serialization rejects transitions into Any State");
    }

    void TestAssetData() {
        // 중첩 상태 머신의 저장 왕복, 편집 복사본의 독립성과 기존 그래프 로딩을 확인한다.
        auto* Graph{UAssetManager::GetAssetByPath<UAnimGraph>("StateMachineLab/ThirdPerson.animgraph")};
        Check(Graph != nullptr && Graph->FindStateMachine(0) != nullptr, "Load the nested graph using Week11 JSON and asset paths");
        const auto* Root{Graph->FindStateMachine(0)};
        const auto* Movement{Root->FindState(0)->mGraph->FindStateMachine(0)};
        Check(Root->GetStates().size() == 6 && Movement != nullptr && Movement->GetStates().size() == 7, "The sample retains six top-level and seven movement states");
        FAnimAssetArchive Save{false};
        Graph->Serialize(Save);
        Check(!Save.HasError() && Save.Save("Build/StateMachineRoundTrip.animgraph"), "Save nested graphs and transition rules");
        FAnimAssetArchive Load{true};
        UAnimGraph Restored{};
        Check(Load.Load("Build/StateMachineRoundTrip.animgraph"), "Read serialized graph data");
        Restored.Serialize(Load);
        Check(!Load.HasError() && Restored.FindStateMachine(0)->GetTransitions().size() == Root->GetTransitions().size(), "Round-trip preserves transition definitions");
        UAnimGraph Copy{};
        Copy.CopyFrom(Restored);
        Copy.FindStateMachine(0)->FindState(0)->mGraph->FindNode(0)->mName = "Independent";
        Check(Restored.FindStateMachine(0)->FindState(0)->mGraph->FindNode(0)->mName != "Independent", "Editing copies own independent nested graph data");
        Check(UAssetManager::GetAssetByPath<UAnimGraph>("AnimationLab/LocomotionLab.animgraph") != nullptr, "Existing Float-only graphs remain loadable");
        for (const auto& Entry : std::filesystem::directory_iterator{"Assets/StateMachineLab/Gallery"}) {
            const FString Key{"StateMachineLab/Gallery/" + Entry.path().filename().string()};
            Check(UAssetManager::GetAssetByPath<UAnimGraph>(Key) != nullptr, "Load gallery graph " + Key);
        }
    }

    void TestGraphOwnership() {
        // 전역 UObject 정리와 shared_ptr의 내부 그래프 수명이 충돌하지 않고 할당이 회수되는지 확인한다.
        const int32 ObjectCount{GUObjectArray.Num()};
        const uint64 AllocationCount{FEngineStatics::TotalAllocationCount};
        const uint64 AllocationBytes{FEngineStatics::TotalAllocationBytes};
        auto* Graph{NewObject<UAnimGraph>()};
        const int32 MachineId{Graph->AddNode("StateMachine", "Owner", 0.0f, 0.0f)};
        auto* Machine{Graph->FindStateMachine(MachineId)};
        const int32 StateId{Machine->AddState("Child", 0.0f, 0.0f, *Graph)};
        auto Child{Machine->FindState(StateId)->mGraph};
        const int32 NestedId{Child->AddNode("StateMachine", "Nested", 0.0f, 0.0f)};
        Child->FindStateMachine(NestedId)->AddState("Leaf", 0.0f, 0.0f, *Child);
        Check(GUObjectArray.Num() == ObjectCount + 1, "Owned state graphs stay outside the global shutdown sweep");
        Check(Child->GetClass() == UAnimGraph::StaticClass(), "Owned state graphs retain valid UObject metadata");
        delete GUObjectArray[GUObjectArray.Num() - 1];
        Check(GUObjectArray.Num() == ObjectCount && Child->FindStateMachine(NestedId)->GetStates().size() == 1, "The shutdown sweep leaves shared child graphs alive until their owner releases them");
        Child.reset();
        Check(FEngineStatics::TotalAllocationCount == AllocationCount && FEngineStatics::TotalAllocationBytes == AllocationBytes, "Nested graph destruction balances UObject allocations");
    }

    USkeletalMeshComponent* FindMesh(UWorld* World, const FString& Name) {
        for (auto* Actor : World->GetPersistentLevel()->GetActors()) {
            if (Actor->GetName() == Name) {
                for (auto* Component : Actor->GetComponents()) {
                    if (auto* Mesh{Cast<USkeletalMeshComponent>(Component)}) {
                        return Mesh;
                    }
                }
            }
        }
        return nullptr;
    }

    void Tick(UWorld* World, uint32 Frames) {
        for (uint32 Frame{}; Frame < Frames; ++Frame) {
            World->Tick(1.0f / 30.0f);
            FInputSystem::UpdateInputStates();
        }
    }

    void Press(UWorld* World, int Key, uint32 Frames = 1) {
        FInputSystem::OnKeyDown(Key);
        Tick(World, Frames);
        FInputSystem::OnKeyUp(Key);
        Tick(World, 1);
    }

    UWorld* OpenSample(const FString& Path) {
        auto* Editor{UWorld::CreateWorld(EWorldType::Editor, false)};
        Check(FJsonArchive::LoadWorld(Editor, Path), "Load scene " + Path);
        auto* World{UWorld::GetDuplicatedWorldForPIE(Editor)};
        Check(World != nullptr, "Duplicate the sample into PIE");
        FWorldContext& Context{GEngine->CreateNewWorldContext(EWorldType::PIE)};
        Context.SetCurrentWorld(World);
        Context.GameViewport = MakeShared<FGameViewportClient>();
        Context.GameViewport->Init(Context, GEngine);
        Context.GameViewport->Resize(1280, 720);
        World->BeginPlay();
        GEngine->DestroyWorld(Editor);
        return World;
    }

    void TestSceneVersionCompatibility() {
        std::filesystem::create_directories("Build/RebasedScenes");

        for (const FString& Name : {"Demo_AnimationLab", "Demo_Skel_AnimGraph", "Demo_StateMachineLab", "Demo_StateMachineClips", "Demo_Skel_1_ReferencePose"}) {
            auto* World{UWorld::CreateWorld(EWorldType::Editor, false)};
            Check(FJsonArchive::LoadWorld(World, "Scenes/" + Name + ".scene"), "Load existing scene " + Name);
            const FString Output{"Build/RebasedScenes/" + Name + ".scene"};
            Check(FJsonArchive::SaveWorld(World, Output), "Save scene using the current scene version " + Name);
            auto* Restored{UWorld::CreateWorld(EWorldType::Editor, false)};
            Check(FJsonArchive::LoadWorld(Restored, Output), "Reload the current scene version " + Name);
            const auto& Actors{World->GetPersistentLevel()->GetActors()};
            const auto& Copies{Restored->GetPersistentLevel()->GetActors()};
            Check(Actors.Num() == Copies.Num(), "Scene round-trip preserves actor count " + Name);

            for (int32 Index{}; Index < Actors.Num(); ++Index) {
                const auto* Actor{Actors[Index]};
                const auto* Copy{Copies[Index]};
                const FVector Before{Actor->GetActorLocation()};
                const FVector After{Copy->GetActorLocation()};
                Check(Actor->GetName() == Copy->GetName() && Near(Before.X, After.X) && Near(Before.Y, After.Y) && Near(Before.Z, After.Z), "Scene round-trip preserves actor identity and root location " + Actor->GetName());
                const auto* Mesh{FindMesh(World, Actor->GetName())};

                if (Mesh != nullptr) {
                    const auto* CopyMesh{FindMesh(Restored, Actor->GetName())};
                    Check(CopyMesh != nullptr && Mesh->GetAnimGraph() == CopyMesh->GetAnimGraph() && Mesh->GetSkeletalMesh() == CopyMesh->GetSkeletalMesh(), "Scene round-trip preserves graph and mesh bindings " + Actor->GetName());
                }
            }

            GEngine->DestroyWorld(Restored);
            GEngine->DestroyWorld(World);
        }
    }

    void TestMainPlaybackIntegration() {
        auto* Source{UAssetManager::GetAssetByPath<UAnimSequenceBase>("StateMachineLab/Rig/Idle_Loop.fbxbin")};
        Check(Source != nullptr, "Load a skeleton for the playback integration regression");
        FBoneAnimationTrack Track{};
        Track.BoneTreeIndex = 0;
        Track.InternalTrackData.PosKeys.Add(FVector{});
        Track.InternalTrackData.PosKeys.Add(FVector{10.0f, 0.0f, 0.0f});
        Track.InternalTrackData.RotKeys.Add(FQuat::Identity());
        Track.InternalTrackData.RotKeys.Add(FRotator{0.0f, 90.0f, 0.0f}.Quaternion());
        Track.InternalTrackData.ScaleKeys.Add(FVector{1.0f, 1.0f, 1.0f});
        TArray<FBoneAnimationTrack> Tracks{};
        Tracks.Add(std::move(Track));
        std::unique_ptr<UAnimSequence> Sequence{NewObject<UAnimSequence>()};
        FString Error{};
        Check(Sequence->Initialize(Source->GetSkeleton(), 2, 1.0f, std::move(Tracks), Error), "Initialize a root-motion and notify sequence: " + Error);
        FAnimNotifyEvent Point{};
        Point.NotifyName = "RebasePoint";
        Sequence->Notifies.Add(Point);
        FAnimNotifyEvent State{};
        State.NotifyName = "RebaseState";
        State.TriggerTime = 0.25f;
        State.Duration = 0.5f;
        Sequence->Notifies.Add(State);
        FTestInstance Instance{};
        Instance.SetRootMotionEnabled(true);
        FAnimNode_SequencePlayer Player{};
        Check(Player.SetSequence(Sequence.get(), false, 1.0f, Error), "Configure the shared sequence player");
        Player.Initialize(Instance);
        FAnimNotifyQueue Queue{};
        const FAnimationUpdateContext Context{Instance, 0.25f, &Queue};
        Player.Update(Context);
        Check(Near(static_cast<float>(Player.GetAccumulatedTime()), 0.25f) && Near(Player.GetRootMotionDelta().mTranslation.X, 2.5f), "Forward playback extracts root translation");
        Check(Queue.bTimeAdvanced && Queue.Entries.Num() == 2, "Forward playback collects the start notify and active notify state");
        FAnimNode_TwoWayBlend Blend{};
        Blend.mA.mLinkedNode = &Player;
        Instance.SetRoot(&Blend);
        Instance.UpdateAnimation(0.25f);
        Check(Near(Instance.GetRootMotionDelta().mTranslation.X, Player.GetRootMotionDelta().mTranslation.X), "Graph pose links propagate root motion");
        Player.SetPlaying(false);
        Queue = {};
        Player.Update(Context);
        Check(!Queue.bTimeAdvanced && Queue.Entries.IsEmpty() && Near(Player.GetRootMotionDelta().mTranslation.X, 0.0f), "Paused playback produces no movement or notify interval");
        Check(Player.SetPlayRate(-1.0f), "Reverse play rate is supported");
        Player.Restart();
        Player.Update(Context);
        Check(Near(static_cast<float>(Player.GetAccumulatedTime()), 0.75f) && Player.GetRootMotionDelta().mRotation.Z < 0.0f, "Reverse playback starts at the end and extracts reverse root rotation");
        Player.SetPlayRate(1.0f);
        Player.SetLooping(true);
        Player.SetAccumulatedTime(0.9);
        Player.SetPlaying(true);
        Queue = {};
        Player.Update(Context);
        Check(Player.DidWrapThisUpdate() && Near(static_cast<float>(Player.GetAccumulatedTime()), 0.15f) && Queue.Entries.Num() == 1, "Loop wrapping collects the zero-time notify");

        auto* World{UWorld::CreateWorld(EWorldType::Editor, false)};
        Check(FJsonArchive::LoadWorld(World, "Scenes/Demo_StateMachineLab.scene"), "Load a mesh for single-node playback controls");
        auto* Mesh{FindMesh(World, "State Machine Player")};
        Check(Mesh != nullptr, "The scene resolves the playback test mesh");
        const bool Played{Mesh->PlayAnimation(Sequence.get(), false, -1.0f, Error)};
        Check(Played, "Single-node playback retains reverse playback: " + Error);
        auto* Single{Mesh->GetSingleNodeInstance()};
        Check(Single != nullptr && Near(Single->GetCurrentTime(), 1.0f), "The sequence editor API starts reverse playback at the end");
        TArray<EAnimNotifyEventType> Events{};
        Mesh->OnAnimNotify.Add([&Events](USkeletalMeshComponent*, const FAnimNotifyEvent&, EAnimNotifyEventType Type) {
            Events.Add(Type);
        });
        Single->SetPlayRate(1.0f);
        Single->PlayAnim();
        Mesh->TickComponent(0.25f);
        Check(Events.Num() == 2 && Events[0] == EAnimNotifyEventType::Notify && Events[1] == EAnimNotifyEventType::StateBegin, "The component dispatches point and state-begin events");
        Single->SetPlaying(false);
        Mesh->TickComponent(0.1f);
        Check(Events.Num() == 3 && Events[2] == EAnimNotifyEventType::StateEnd && Near(Single->GetCurrentTime(), 0.25f), "Pausing retains the pose time and ends active notify states");
        Single->SetPosition(0.8f);
        Single->SetPlaying(true);
        Mesh->TickComponent(0.1f);
        Check(Near(Single->GetPreviousTime(), 0.8f) && Near(Single->GetCurrentTime(), 0.9f), "Timeline seek and playback share the graph player clock");
        GEngine->DestroyWorld(World);
    }

    void TestMotionYard() {
        // 실제 씬에서 Lua 입력, 인스턴스별 변수와 중첩 상태 전환이 함께 동작하는지 확인한다.
        UWorld* World{OpenSample("Scenes/Demo_StateMachineLab.scene")};
        auto* Player{FindMesh(World, "State Machine Player")};
        auto* Demo{FindMesh(World, "Demo Blend")};
        Check(Player != nullptr && Demo != nullptr && FindMesh(World, "Demo Action") != nullptr && FindMesh(World, "Demo Aim") != nullptr && FindMesh(World, "Demo Jump") != nullptr, "Motion Yard contains the player and four independent demonstrators");
        auto* Graph{Cast<UAnimGraphInstance>(Player->GetAnimInstance())};
        auto* DemoGraph{Cast<UAnimGraphInstance>(Demo->GetAnimInstance())};
        Check(Graph != nullptr && DemoGraph != nullptr && Graph != DemoGraph && !Graph->SetFloatVariable("IsInAir", 1.0f), "Each character owns typed runtime variables");
        auto* Root{dynamic_cast<FAnimNode_StateMachine*>(Graph->GetRootNode())};
        std::vector<FAnimNodePoseInput> States{};
        Root->GetPoseInputs(States);
        auto* Movement{dynamic_cast<FAnimNode_StateMachine*>(States[0].mLink->mLinkedNode)};
        Check(Movement != nullptr, "Build the nested Movement state machine");
        Tick(World, 10);
        Check(DemoGraph->GetFloatVariable("Speed") > 1.0f && Graph->GetFloatVariable("Speed") == 0.0f, "Demonstrator variables do not affect the player");
        const FVector Start{Player->GetOwner()->GetActorLocation()};
        FInputSystem::OnKeyDown('W');
        Tick(World, 30);
        Check(Graph->GetFloatVariable("Speed") > 2.0f && Player->GetOwner()->GetActorLocation().X > Start.X + 1.0f, "Lua input moves the third-person player and drives locomotion");
        FInputSystem::OnKeyUp('W');
        Tick(World, 1);
        Press(World, 'C');
        Tick(World, 12);
        Check(Graph->GetBoolVariable("IsCrouching") && Movement->GetCurrentState() == 1, "C enters the Crouch graph through its rule");
        Press(World, 'C');
        Tick(World, 12);
        Press(World, VK_SPACE);
        Check(Graph->GetBoolVariable("IsInAir"), "Space supplies the airborne input");
        std::set<int32> AirStates{};
        for (uint32 Frame{}; Frame < 50; ++Frame) {
            Tick(World, 1);
            AirStates.insert(Movement->GetCurrentState());
        }
        Check(AirStates.contains(4) && AirStates.contains(5) && AirStates.contains(6) && !Graph->GetBoolVariable("IsInAir"), "Jump Start, Falling and Land are selected by native transition rules");
        Press(World, 'Q');
        Tick(World, 9);
        Check(Root->GetCurrentState() == 1 || Root->GetTargetState() == 1, "Q routes to the action graph");
        Tick(World, 90);
        Check(Root->GetCurrentState() == 0 && !Graph->GetBoolVariable("WantsAction"), "The action completes and returns to Movement");
        Press(World, 'F');
        Tick(World, 18);
        Check(Root->GetCurrentState() == 3, "F enters Dance");
        Press(World, 'F');
        Tick(World, 18);
        Check(Root->GetCurrentState() == 0, "Not IsDancing returns to Movement");
        Press(World, 'Y');
        Tick(World, 18);
        Check(Root->GetCurrentState() == 4, "Y enters Death");
        Press(World, VK_BACK);
        Tick(World, 18);
        Check(Root->GetCurrentState() == 0, "Reset returns from Death");
        Press(World, VK_TAB);
        Tick(World, 18);
        Check(Root->GetCurrentState() == 5, "Tab enters the clip browser graph");
        const float BrowserClip{Graph->GetFloatVariable("BrowserClip")};
        Press(World, '2');
        Check(Graph->GetFloatVariable("BrowserClip") == BrowserClip + 1.0f, "The browser selects the next clip");
        Press(World, VK_TAB);
        Tick(World, 18);
        Press(World, 'B');
        std::set<int32> GuidedStates{};
        for (uint32 Frame{}; Frame < 900; ++Frame) {
            Tick(World, 1);
            GuidedStates.insert(Root->GetCurrentState());
        }
        Check(GuidedStates.contains(0) && GuidedStates.contains(1) && GuidedStates.contains(2) && GuidedStates.contains(3), "The guided cycle demonstrates movement, actions, hit reactions and dance");
        const auto* Vertices{Player->GetMeshObject()->GetCPUSkinnedVertices()};
        Check(Vertices != nullptr && Vertices->Num() > 300 && std::isfinite((*Vertices)[0].Position.Z), "Native graph evaluation produces valid skinned geometry");
        FUIDrawList DrawList{};
        GEngine->GetWorldContextFromWorld(World)->GameViewport->PaintUI(DrawList);
        Check(std::any_of(DrawList.GetVertices().begin(), DrawList.GetVertices().end(), [](const FUIVertex& Vertex) {
            return Vertex.Mode == static_cast<float>(EUIDrawMode::Text);
        }), "The telemetry HUD paints its text");
        auto* Context{GEngine->GetWorldContextFromWorld(World)};
        Press(World, 'G');
        Check(Context->TravelURL == "Scenes/Demo_StateMachineClips.scene", "G requests the clip gallery");
        GEngine->TickWorldTravel(*Context);
        World = Context->World();
        Check(FindMesh(World, "Gallery Player") != nullptr, "Scene travel opens the gallery");
        Press(World, 'G');
        GEngine->TickWorldTravel(*Context);
        World = Context->World();
        Check(FindMesh(World, "State Machine Player") != nullptr, "G returns from the gallery to Motion Yard");
        World->EndPlay();
        GEngine->DestroyWorldContext(World);
        FInputSystem::ClearAllStates();
    }

    void TestGallery() {
        // 갤러리의 모든 클립이 독립된 인스턴스에서 재생되고 CPU 스키닝 결과를 만드는지 확인한다.
        UWorld* World{OpenSample("Scenes/Demo_StateMachineClips.scene")};
        Tick(World, 30);
        uint32 Count{};
        std::set<const UAnimInstance*> Instances{};
        for (auto* Actor : World->GetPersistentLevel()->GetActors()) {
            if (!Actor->GetName().starts_with("Clip ")) {
                continue;
            }
            auto* Mesh{FindMesh(World, Actor->GetName())};
            if (Mesh == nullptr) {
                continue;
            }
            Check(Mesh->GetAnimInstance() != nullptr && Mesh->GetMeshObject()->GetCPUSkinnedVertices() != nullptr, "The gallery animates " + Actor->GetName());
            Instances.insert(Mesh->GetAnimInstance());
            ++Count;
        }
        Check(Count == 43 && Instances.size() == 43, "The gallery has 43 independently animated clip instances");
        World->EndPlay();
        GEngine->DestroyWorldContext(World);
    }
}

int main() {
    FTestLogSink Log{};
    try {
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
        TestRulesAndRuntime();
        TestAnyState();
        TestTransitionInterrupt();
        TestAssetData();
        TestGraphOwnership();
        TestSceneVersionCompatibility();
        TestMainPlaybackIntegration();
        TestMotionYard();
        TestGallery();
        Check(!Log.HasScriptFault(), "All sample Lua scripts run without errors");
        FEngineLoop Loop{};
        Loop.Exit();
        Check(GEngine == nullptr && GUObjectArray.Num() == 0, "The real engine shutdown releases every registered object after the State Machine scenes");
        std::printf("PASS: State Machine port, JSON compatibility, native blending, Lua input, Motion Yard and 43-clip gallery.\n");
        return 0;
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "FAIL: %s\n", Error.what());
        return 1;
    }
}
