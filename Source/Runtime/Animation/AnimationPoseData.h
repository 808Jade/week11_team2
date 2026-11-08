#pragma once

#include "Animation/BoneContainer.h"

struct FAnimationPoseData
{
	const FBoneContainer& RequiredBones;
	TArray<FTransform>& Pose;
};
