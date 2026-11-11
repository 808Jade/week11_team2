#include "EnginePCH.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/AnimNode_Base.h"

#include <algorithm>

FTransform FAnimationRuntime::BlendTransforms(const FTransform& A, const FTransform& B, float WeightOfA) {
    // 회전은 쿼터니언 구면 보간, 위치와 스케일은 선형 보간을 사용한다.
    const float Alpha{1.0f - WeightOfA};

    return {FMath::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), Alpha).ToFRotator(), A.Location + (B.Location - A.Location) * Alpha, A.Scale + (B.Scale - A.Scale) * Alpha};
}

void FAnimationRuntime::BlendTwoPosesTogether(const TArray<FTransform>& PoseA, const TArray<FTransform>& PoseB, float WeightOfPoseA, TArray<FTransform>& OutPose) {
    const float Weight{std::clamp(WeightOfPoseA, 0.0f, 1.0f)};
    const int32 Count{std::min(PoseA.Num(), PoseB.Num())};

    // A를 출력 배열로 재사용할 수 있으며, B에 없는 나머지 본은 A의 값을 유지한다.
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

FRootMotionDelta FAnimationRuntime::BlendRootMotion(const FRootMotionDelta& A, const FRootMotionDelta& B, float WeightOfA) {
    const float Alpha{1.0f - std::clamp(WeightOfA, 0.0f, 1.0f)};
    return {A.mTranslation + (B.mTranslation - A.mTranslation) * Alpha, FMath::Slerp(A.mRotation, B.mRotation, Alpha),
        A.mRotationPivot + (B.mRotationPivot - A.mRotationPivot) * Alpha};
}
