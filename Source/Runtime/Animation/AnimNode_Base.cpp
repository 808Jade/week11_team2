#include "EnginePCH.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimInstance.h"

FAnimationUpdateContext::FAnimationUpdateContext(UAnimInstance& AnimInstance, float DeltaTime, uint64 UpdateCounter)
    : mAnimInstance{&AnimInstance},
      mDeltaTime{DeltaTime},
      mUpdateCounter{UpdateCounter} {
}

FAnimationUpdateContext FAnimationUpdateContext::FractionalWeight(float Multiplier) const {
    FAnimationUpdateContext Result{*this};

    Result.mWeight *= Multiplier;

    return Result;
}

UAnimInstance& FAnimationUpdateContext::GetAnimInstance() const {
    return *mAnimInstance;
}

float FAnimationUpdateContext::GetDeltaTime() const {
    return mDeltaTime;
}

float FAnimationUpdateContext::GetFinalBlendWeight() const {
    return mWeight;
}

uint64 FAnimationUpdateContext::GetUpdateCounter() const {
    return mUpdateCounter;
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
    mLastUpdateCounter = 0;
    mLastWeight = 0.0f;

    if (mLinkedNode != nullptr) {
        mLinkedNode->Initialize(AnimInstance);
    }
}

void FPoseLink::Update(const FAnimationUpdateContext& Context) {
    if (mLinkedNode != nullptr) {
        if (mLastUpdateCounter != Context.GetUpdateCounter()) {
            mLastUpdateCounter = Context.GetUpdateCounter();
            mLastWeight = 0.0f;
        }

        mLastWeight += Context.GetFinalBlendWeight();
        mLinkedNode->RecordUpdate(Context);
        mLinkedNode->Update(Context);
    }
}

void FPoseLink::Evaluate(FPoseContext& Output) {
    if (mLinkedNode != nullptr) {
        mLinkedNode->Evaluate(Output);
    } else {
        Output.ResetToReferencePose();
    }
}

float FPoseLink::GetDebugWeight(uint64 UpdateCounter) const {
    return mLastUpdateCounter == UpdateCounter ? mLastWeight : 0.0f;
}

void FAnimNode_Base::Initialize(UAnimInstance& AnimInstance) {
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

float FAnimNode_Base::GetDebugWeight(uint64 UpdateCounter) const {
    return mLastUpdateCounter == UpdateCounter ? mLastWeight : 0.0f;
}

void FAnimNode_Base::RecordUpdate(const FAnimationUpdateContext& Context) {

    if (mLastUpdateCounter == Context.GetUpdateCounter()) {
        mLastWeight += Context.GetFinalBlendWeight();
    } else {
        mLastWeight = Context.GetFinalBlendWeight();
        mLastUpdateCounter = Context.GetUpdateCounter();
    }
}
