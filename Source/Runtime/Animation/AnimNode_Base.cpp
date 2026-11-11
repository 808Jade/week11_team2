#include "EnginePCH.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimInstance.h"

FAnimationUpdateContext::FAnimationUpdateContext(UAnimInstance& AnimInstance, float DeltaTime, FAnimNotifyQueue* NotifyQueue)
    : mAnimInstance{&AnimInstance},
      mDeltaTime{DeltaTime},
      mNotifyQueue{NotifyQueue} {
}

UAnimInstance& FAnimationUpdateContext::GetAnimInstance() const {
    return *mAnimInstance;
}

float FAnimationUpdateContext::GetDeltaTime() const {
    return mDeltaTime;
}

FPoseContext::FPoseContext(UAnimInstance& AnimInstance)
    : mAnimInstance{&AnimInstance} {
    ResetToReferencePose();
}

FPoseContext::FPoseContext(UAnimInstance& AnimInstance, TArray<FTransform>&& ReusablePose)
    : mPose{std::move(ReusablePose)},
      mAnimInstance{&AnimInstance} {
    ResetToReferencePose();
}

UAnimInstance& FPoseContext::GetAnimInstance() const {
    return *mAnimInstance;
}

void FPoseContext::ResetToReferencePose() {
    mAnimInstance->GetReferencePose(mPose);
}

void FPoseLink::Initialize(UAnimInstance& AnimInstance) {
    if (mLinkedNode != nullptr) {
        mLinkedNode->Initialize(AnimInstance);
    }
}

void FPoseLink::Update(const FAnimationUpdateContext& Context) {
    if (mLinkedNode != nullptr) {
        // 여러 입력이 같은 노드를 참조해도 인스턴스를 거쳐 프레임당 한 번만 갱신한다.
        Context.GetAnimInstance().UpdateNode(*mLinkedNode, Context);
    }
}

void FPoseLink::Evaluate(FPoseContext& Output, float Weight) {
    if (mLinkedNode != nullptr) {
        // 같은 노드를 여러 경로로 평가할 수 있으므로 방문 기록은 연결별로 남긴다.
        UAnimInstance& AnimInstance{Output.GetAnimInstance()};
        AnimInstance.BeginDebugVisit(*mLinkedNode, this, Weight);
        mLinkedNode->Evaluate(Output);
        AnimInstance.EndDebugVisit();
    } else {
        Output.ResetToReferencePose();
    }
}

void FAnimNode_Base::Initialize(UAnimInstance& AnimInstance) {
    AnimInstance.ResetNodeUpdate(*this);
    std::vector<FAnimNodePoseInput> Inputs{};

    GetPoseInputs(Inputs);

    for (const FAnimNodePoseInput& Input : Inputs) {
        Input.mLink->Initialize(AnimInstance);
    }
}

void FAnimNode_Base::Update(const FAnimationUpdateContext&) {
}

void FAnimNode_Base::Evaluate(FPoseContext&) {
}

void FAnimNode_Base::GetPoseInputs(std::vector<FAnimNodePoseInput>&) {
}

void FAnimNode_Base::GetDebugValues(std::vector<FAnimNodeValue>&) {
}

void FAnimNode_Base::GetValueInputs(std::vector<FAnimNodeValueInput>&) {
}

FAnimNotifyQueue* FAnimationUpdateContext::GetNotifyQueue() const {
    return mNotifyQueue;
}

FRootMotionDelta FAnimNode_Base::GetRootMotionDelta() const {
    return {};
}
