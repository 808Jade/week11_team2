#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"

UAnimInstance::~UAnimInstance() {
    if (auto* Component{GetSkelMeshComponent()}; Component && Component->mAnimScriptInstance == this) {
        Component->mAnimScriptInstance = nullptr;
    }
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const {
    return Cast<USkeletalMeshComponent>(GetOuter());
}

bool UAnimInstance::InitializeAnimation(FString& OutError) {
    const USkeletalMeshComponent* Component{GetSkelMeshComponent()};
    const USkeletalMesh* Mesh{Component ? Component->GetSkeletalMesh() : nullptr};
    if (!Mesh) {
        OutError = "Set a skeletal mesh before initializing animation";
        return false;
    }
    if (!mRequiredBones.InitializeTo(*Mesh, OutError)) {
        return false;
    }

    mHasLastOwnerLocation = false;
    mOwnerVelocity = {};
    mDebugVisits.clear();
    mCurrentDebugVisit = -1;
    mUpdatedNodes.clear();
    NativeInitializeAnimation();

    if (mRootNode) {
        mRootNode->Initialize(*this);
    }
    return true;
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds) {
    // 컴포넌트의 월드 위치 변화로 그래프의 속도 변수에 사용할 이동 속도를 구한다.
    if (const auto* Component{GetSkelMeshComponent()}) {
        const FVector Location{Component->GetWorldLocation()};
        if (mHasLastOwnerLocation && DeltaSeconds > 0.0f) {
            mOwnerVelocity = (Location - mLastOwnerLocation) * (1.0f / DeltaSeconds);
        }
        mLastOwnerLocation = Location;
        mHasLastOwnerLocation = true;
    }

    mDebugVisits.clear();
    mCurrentDebugVisit = -1;
    mUpdatedNodes.clear();
    NativeUpdateAnimation(DeltaSeconds);

    FAnimNotifyQueue Queue{};

    if (mRootNode) {
        const FAnimationUpdateContext Context{*this, DeltaSeconds, &Queue};
        UpdateNode(*mRootNode, Context);
    }

    TriggerAnimNotifies(Queue);
}

void UAnimInstance::UpdateNode(FAnimNode_Base& Node, const FAnimationUpdateContext& Context) {
    // 호출 전에 등록해야 공유 경로에서 같은 노드의 재생 시간이 두 번 증가하지 않는다.
    if (mUpdatedNodes.emplace(&Node, true).second) {
        Node.Update(Context);
    }
}

void UAnimInstance::ResetNodeUpdate(FAnimNode_Base& Node) {
    // 상태 재진입으로 초기화된 노드는 같은 프레임에도 초기 포즈를 갱신할 수 있게 한다.
    mUpdatedNodes.erase(&Node);
}

bool UAnimInstance::EvaluateAnimation(TArray<FTransform>& OutPose) {
    mDebugVisits.clear();
    mCurrentDebugVisit = -1;

    if (!mRootNode || !mRequiredBones.IsValid()) {
        return false;
    }

    // 이전 결과 배열의 저장 공간을 넘겨 매 평가 시 본 배열을 새로 할당하지 않게 한다.
    FPoseContext Context{*this, std::move(OutPose)};
    BeginDebugVisit(*mRootNode, nullptr, 1.0f);
    mRootNode->Evaluate(Context);
    EndDebugVisit();
    OutPose = std::move(Context.mPose);
    return true;
}

void UAnimInstance::GetReferencePose(TArray<FTransform>& OutPose) const {
    mRequiredBones.FillWithRefPose(OutPose);
}

void UAnimInstance::EvaluateSequence(const UAnimSequenceBase& Sequence, double Time, TArray<FTransform>& OutPose) const {
    FAnimationPoseData PoseData{mRequiredBones, OutPose};
    Sequence.GetAnimationPose(PoseData, FAnimExtractContext{Time, mRootMotionEnabled});
}

FAnimNode_Base* UAnimInstance::GetRootNode() const {
    return mRootNode;
}

const std::vector<FAnimNodeVisit>& UAnimInstance::GetDebugVisits() const {
    return mDebugVisits;
}

void UAnimInstance::BeginDebugVisit(const FAnimNode_Base& Node, const FPoseLink* Link, float Weight) {
    mDebugVisits.push_back({&Node, Link, Weight, mCurrentDebugVisit});
    mCurrentDebugVisit = static_cast<int32>(mDebugVisits.size() - 1);
}

void UAnimInstance::EndDebugVisit() {
    mCurrentDebugVisit = mDebugVisits[static_cast<std::size_t>(mCurrentDebugVisit)].mParentIndex;
}

void UAnimInstance::RecordDebugSample(const FAnimNode_Base& Node, int32 SampleIndex, float Weight) {
    if (mCurrentDebugVisit >= 0) {
        mDebugVisits.push_back({&Node, nullptr, Weight, mCurrentDebugVisit, SampleIndex});
    }
}

const FVector& UAnimInstance::GetOwnerVelocity() const {
    return mOwnerVelocity;
}

void UAnimInstance::GetDebugVariables(std::vector<FAnimNodeValue>&) {
}

float UAnimInstance::GetFloatVariable(const FString&) const {
    return 0.0f;
}

bool UAnimInstance::GetBoolVariable(const FString&) const {
    return false;
}

bool UAnimInstance::HasGraphVariable(const FString&, bool) const {
    return false;
}

bool UAnimInstance::GetNodeLayout(const FAnimNode_Base*, float&, float&) const {
    return false;
}

void UAnimInstance::NativeInitializeAnimation() {
}

void UAnimInstance::NativeUpdateAnimation(float) {
}

const FBoneContainer& UAnimInstance::GetRequiredBones() const {
    return mRequiredBones;
}

void UAnimInstance::SetRootNode(FAnimNode_Base* RootNode) {
    mRootNode = RootNode;
}

FRootMotionDelta UAnimInstance::GetRootMotionDelta() const {
    return mRootNode != nullptr ? mRootNode->GetRootMotionDelta() : FRootMotionDelta{};
}

void UAnimInstance::SetRootMotionEnabled(bool Enabled) {
    mRootMotionEnabled = Enabled;
}

bool UAnimInstance::IsRootMotionEnabled() const {
    return mRootMotionEnabled;
}

FRootMotionDelta UAnimInstance::ExtractSequenceEvents(const UAnimSequenceBase& Sequence, double From, double To, float PlayRate, bool Wrapped, bool IncludeStart, const FAnimationUpdateContext& Context) const {
    FRootMotionDelta Result{};
    const double Length{Sequence.GetPlayLength()};

    if (Length <= 0.0 || (From == To && !Wrapped)) {
        return Result;
    }

    if (mRootMotionEnabled && Sequence.HasRootTrack()) {
        const auto Extract{[&Sequence](double Start, double End, FVector& Translation, FQuat& Rotation) {
            const FQuat Inverse{Sequence.GetRootRotationAtTime(Start).UnitInverse()};
            Translation = Inverse.RotateVector(Sequence.GetRootLocationAtTime(End) - Sequence.GetRootLocationAtTime(Start));
            Rotation = Inverse * Sequence.GetRootRotationAtTime(End);
        }};

        FVector Translation{};
        FQuat Rotation{FQuat::Identity()};

        if (Wrapped) {
            const bool Forward{PlayRate > 0.0f};
            FVector FirstTranslation{};
            FVector SecondTranslation{};
            FQuat FirstRotation{FQuat::Identity()};
            FQuat SecondRotation{FQuat::Identity()};
            Extract(From, Forward ? Length : 0.0, FirstTranslation, FirstRotation);
            Extract(Forward ? 0.0 : Length, To, SecondTranslation, SecondRotation);
            Translation = FirstTranslation + FirstRotation.RotateVector(SecondTranslation);
            Rotation = FirstRotation * SecondRotation;
        } else {
            Extract(From, To, Translation, Rotation);
        }

        const FQuat LockRotation{Sequence.GetRootRotationAtTime(0.0)};
        Result.mTranslation = LockRotation.RotateVector(Translation);
        Result.mRotation = LockRotation * Rotation * LockRotation.UnitInverse();
        Result.mRotationPivot = Sequence.GetRootLocationAtTime(0.0);
    }

    if (FAnimNotifyQueue* Queue{Context.GetNotifyQueue()}; Queue != nullptr) {
        Queue->bTimeAdvanced = true;
        TArray<const FAnimNotifyEvent*> Found{};

        if (!Wrapped) {
            Sequence.GetAnimNotifiesFromDeltaPositions(From, To, IncludeStart, Found);
        } else if (PlayRate > 0.0f) {
            Sequence.GetAnimNotifiesFromDeltaPositions(From, Length, IncludeStart, Found);
            Sequence.GetAnimNotifiesFromDeltaPositions(0.0, To, true, Found);
        } else {
            Sequence.GetAnimNotifiesFromDeltaPositions(From, 0.0, IncludeStart, Found);
            Sequence.GetAnimNotifiesFromDeltaPositions(Length, To, true, Found);
        }

        for (const FAnimNotifyEvent* Notify : Found) {
            Queue->Entries.Add(FAnimNotifyQueue::FEntry{*Notify, &Sequence});
        }
    }

    return Result;
}

void UAnimInstance::TriggerAnimNotifies(const FAnimNotifyQueue& Queue) {
    if (!Queue.bTimeAdvanced) {
        EndAllNotifyStates();
        return;
    }

    const auto SameNotify{[](const FAnimNotifyQueue::FEntry& A, const FAnimNotifyQueue::FEntry& B) {
        return A.Sequence == B.Sequence && A.Event == B.Event;
    }};
    TArray<FAnimNotifyQueue::FEntry> NewActiveStates{};

    for (const FAnimNotifyQueue::FEntry& Entry : Queue.Entries) {
        if (!Entry.Event.IsNotifyState()) {
            mPendingNotifies.Add(FAnimNotifyDispatch{Entry.Event, EAnimNotifyEventType::Notify, Entry.Sequence});
            continue;
        }

        bool Duplicate{};

        for (const FAnimNotifyQueue::FEntry& Added : NewActiveStates) {
            Duplicate = Duplicate || SameNotify(Added, Entry);
        }

        if (!Duplicate) {
            NewActiveStates.Add(Entry);
        }
    }

    for (const FAnimNotifyQueue::FEntry& Old : mActiveNotifyStates) {
        bool StillActive{};

        for (const FAnimNotifyQueue::FEntry& New : NewActiveStates) {
            StillActive = StillActive || SameNotify(Old, New);
        }

        if (!StillActive) {
            mPendingNotifies.Add(FAnimNotifyDispatch{Old.Event, EAnimNotifyEventType::StateEnd, Old.Sequence});
        }
    }

    for (const FAnimNotifyQueue::FEntry& New : NewActiveStates) {
        bool WasActive{};

        for (const FAnimNotifyQueue::FEntry& Old : mActiveNotifyStates) {
            WasActive = WasActive || SameNotify(Old, New);
        }

        mPendingNotifies.Add(FAnimNotifyDispatch{New.Event, WasActive ? EAnimNotifyEventType::StateTick : EAnimNotifyEventType::StateBegin, New.Sequence});
    }

    mActiveNotifyStates = std::move(NewActiveStates);
}

void UAnimInstance::EndAllNotifyStates() {
    for (const FAnimNotifyQueue::FEntry& Old : mActiveNotifyStates) {
        mPendingNotifies.Add(FAnimNotifyDispatch{Old.Event, EAnimNotifyEventType::StateEnd, Old.Sequence});
    }

    mActiveNotifyStates.Empty();
}

TArray<FAnimNotifyDispatch> UAnimInstance::TakePendingNotifies() {
    TArray<FAnimNotifyDispatch> Result{};
    Result.Swap(mPendingNotifies);
    return Result;
}
