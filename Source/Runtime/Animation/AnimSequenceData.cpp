#include "EnginePCH.h"
#include "Animation/AnimSequenceData.h"

#include <utility>

FAnimBoneTrack::FAnimBoneTrack(int32 InBoneIndex, TArray<FVector> InPositionKeys, TArray<FQuat> InRotationKeys, TArray<FVector> InScaleKeys)
	: mBoneIndex{ InBoneIndex },
	  mPositionKeys{ std::move(InPositionKeys) },
	  mRotationKeys{ std::move(InRotationKeys) },
	  mScaleKeys{ std::move(InScaleKeys) } {
}

int32 FAnimBoneTrack::GetBoneIndex() const {
	return mBoneIndex;
}

const TArray<FVector>& FAnimBoneTrack::GetPositionKeys() const {
	return mPositionKeys;
}

const TArray<FQuat>& FAnimBoneTrack::GetRotationKeys() const {
	return mRotationKeys;
}

const TArray<FVector>& FAnimBoneTrack::GetScaleKeys() const {
	return mScaleKeys;
}
