#include "EnginePCH.h"
#include "Animation/AnimNode_TwoWayBlend.h"
#include "Animation/AnimationRuntime.h"

#include <algorithm>
#include <cmath>

void FAnimNode_TwoWayBlend::Update(const FAnimationUpdateContext& Context) {
    if (mAlphaBinding != nullptr) {
        mAlpha = *mAlphaBinding;
    }

    mBlendAlpha = std::isfinite(mAlpha) ? std::clamp(mAlpha, 0.0f, 1.0f) : 0.0f;

    if (mBlendAlpha < 1.0f - RelevantWeightThreshold) {
        mA.Update(Context.FractionalWeight(1.0f - mBlendAlpha));
    }

    if (mBlendAlpha > RelevantWeightThreshold) {
        mB.Update(Context.FractionalWeight(mBlendAlpha));
    }
}

void FAnimNode_TwoWayBlend::Evaluate(FPoseContext& Output) {
    if (mBlendAlpha <= RelevantWeightThreshold) {
        mA.Evaluate(Output);

        return;
    }

    if (mBlendAlpha >= 1.0f - RelevantWeightThreshold) {
        mB.Evaluate(Output);

        return;
    }

    FPoseContext PoseA{Output.GetAnimInstance()};
    FPoseContext PoseB{Output.GetAnimInstance()};

    mA.Evaluate(PoseA);
    mB.Evaluate(PoseB);

    FAnimationRuntime::BlendTwoPosesTogether(PoseA.mPose, PoseB.mPose, 1.0f - mBlendAlpha, Output.mPose);
}

const char* FAnimNode_TwoWayBlend::GetNodeType() const {
    return "Two-Way Blend";
}

void FAnimNode_TwoWayBlend::GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs) {
    OutInputs.push_back({"A", &mA, 1.0f - mBlendAlpha});
    OutInputs.push_back({"B", &mB, mBlendAlpha});
}

void FAnimNode_TwoWayBlend::GetDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    FAnimNodeValue Alpha{};

    Alpha.mName = "Alpha";
    Alpha.mType = EAnimNodeValueType::Float;
    Alpha.mFloat = &mAlpha;
    Alpha.mMin = 0.0f;
    Alpha.mMax = 1.0f;
    Alpha.mBinding = mAlphaBinding != nullptr ? mAlphaBindingName : FString{};
    Alpha.mBindingParameter = "Alpha";
    OutValues.push_back(std::move(Alpha));
}

float FAnimNode_TwoWayBlend::GetBlendAlpha() const {
    return mBlendAlpha;
}
