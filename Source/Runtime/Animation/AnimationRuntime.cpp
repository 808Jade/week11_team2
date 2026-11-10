#include "EnginePCH.h"
#include "Animation/AnimationRuntime.h"

#include <algorithm>

FTransform FAnimationRuntime::BlendTransforms(const FTransform& A, const FTransform& B, float WeightOfA) {
    const float Alpha{1.0f - WeightOfA};

    return {FMath::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), Alpha).ToFRotator(), A.Location + (B.Location - A.Location) * Alpha, A.Scale + (B.Scale - A.Scale) * Alpha};
}

void FAnimationRuntime::BlendTwoPosesTogether(const TArray<FTransform>& PoseA, const TArray<FTransform>& PoseB, float WeightOfPoseA, TArray<FTransform>& OutPose) {
    const float Weight{std::clamp(WeightOfPoseA, 0.0f, 1.0f)};
    const int32 Count{std::min(PoseA.Num(), PoseB.Num())};

    if (&OutPose != &PoseA) {
        OutPose.SetNum(PoseA.Num());

        for (int32 Bone{Count}; Bone < PoseA.Num(); ++Bone) {
            OutPose[Bone] = PoseA[Bone];
        }
    }

    for (int32 Bone{}; Bone < Count; ++Bone) {
        OutPose[Bone] = BlendTransforms(PoseA[Bone], PoseB[Bone], Weight);
    }
}
