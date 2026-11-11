#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include <vector>
#include "Math/Transform.h"
#include "Containers/Array.h"

// 동일한 본 인덱스의 로컬 포즈를 블렌딩한다. 가중치 인자는 첫 번째 포즈 A의 비율이다.
struct FRootMotionDelta;

class FAnimationRuntime {
public:

    // A의 비율을 받아 회전은 구면 보간하고 위치 및 스케일은 선형 보간한다.
    static FRootMotionDelta BlendRootMotion(const FRootMotionDelta& A, const FRootMotionDelta& B, float WeightOfA);

    static FTransform BlendTransforms(const FTransform& A, const FTransform& B, float WeightOfA);

    // 동일 본 인덱스끼리 A의 가중치로 혼합하며 출력 배열로 A를 재사용할 수 있다.
    static void BlendTwoPosesTogether(const TArray<FTransform>& PoseA, const TArray<FTransform>& PoseB, float WeightOfPoseA, TArray<FTransform>& OutPose);
};
