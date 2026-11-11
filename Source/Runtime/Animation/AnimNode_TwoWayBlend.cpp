#include "EnginePCH.h"
#include "Animation/AnimNode_TwoWayBlend.h"
#include "Animation/AnimationRuntime.h"

#include <algorithm>
#include <cmath>

void FAnimNode_TwoWayBlend::Update(const FAnimationUpdateContext& Context) {
    mAlpha = mAlphaInput.Evaluate(Context, mAlpha);

    mBlendAlpha = std::isfinite(mAlpha) ? std::clamp(mAlpha, 0.0f, 1.0f) : 0.0f;

    // 결과에 기여하는 입력만 갱신해 가중치가 0인 경로의 재생을 진행하지 않는다.
    if (mBlendAlpha < 1.0f - RelevantWeightThreshold) {
        mA.Update(Context);
    }

    if (mBlendAlpha > RelevantWeightThreshold) {
        mB.Update(Context);
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

    // 두 입력이 실제로 기여할 때만 임시 포즈를 만들고, 방문 기록에도 같은 가중치를 전달한다.
    FPoseContext PoseA{Output.GetAnimInstance()};
    FPoseContext PoseB{Output.GetAnimInstance()};

    const float WeightA{1.0f - mBlendAlpha};
    mA.Evaluate(PoseA, WeightA);
    mB.Evaluate(PoseB, 1.0f - WeightA);

    FAnimationRuntime::BlendTwoPosesTogether(PoseA.mPose, PoseB.mPose, WeightA, Output.mPose);
}

void FAnimNode_TwoWayBlend::GetValueInputs(std::vector<FAnimNodeValueInput>& OutInputs) {
    OutInputs.push_back({"Alpha", &mAlphaInput});
}

const FString& FAnimNode_TwoWayBlend::GetNodeType() const {
    static const FString Type{"Two-Way Blend"};

    return Type;
}

void FAnimNode_TwoWayBlend::GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs) {
    OutInputs.push_back({"A", &mA});
    OutInputs.push_back({"B", &mB});
}

void FAnimNode_TwoWayBlend::GetDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    FAnimNodeValue Alpha{};

    Alpha.mName = "Alpha";
    Alpha.mValue = FAnimNodeFloatValue{&mAlpha, 0.0f, 1.0f};
    OutValues.push_back(std::move(Alpha));
}

float FAnimNode_TwoWayBlend::GetBlendAlpha() const {
    return mBlendAlpha;
}

FRootMotionDelta FAnimNode_TwoWayBlend::GetRootMotionDelta() const {
    const FRootMotionDelta A{mA.mLinkedNode != nullptr && mBlendAlpha < 1.0f - RelevantWeightThreshold ? mA.mLinkedNode->GetRootMotionDelta() : FRootMotionDelta{}};
    const FRootMotionDelta B{mB.mLinkedNode != nullptr && mBlendAlpha > RelevantWeightThreshold ? mB.mLinkedNode->GetRootMotionDelta() : FRootMotionDelta{}};
    return FAnimationRuntime::BlendRootMotion(A, B, 1.0f - mBlendAlpha);
}
