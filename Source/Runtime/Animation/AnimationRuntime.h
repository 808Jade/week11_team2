#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include <vector>
#include "Math/Transform.h"
#include "Containers/Array.h"

class FAnimationRuntime {
public:

    static FTransform BlendTransforms(const FTransform& A, const FTransform& B, float WeightOfA);

    static void BlendTwoPosesTogether(const TArray<FTransform>& PoseA, const TArray<FTransform>& PoseB, float WeightOfPoseA, TArray<FTransform>& OutPose);
};
