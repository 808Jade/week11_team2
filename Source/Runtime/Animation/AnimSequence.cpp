#include "EnginePCH.h"
#include "Animation/AnimSequence.h"

#include <utility>

void UAnimSequence::Build(float InSampleRate, int32 InNumSamples, TArray<FBoneInfo> InRefSkeleton, TArray<FAnimBoneTrack> InBoneTracks) {
	mSampleRate = InSampleRate;
	mNumSamples = InNumSamples;
	mRefSkeleton = std::move(InRefSkeleton);
	mBoneTracks = std::move(InBoneTracks);
}

float UAnimSequence::GetSampleRate() const {
	return mSampleRate;
}

int32 UAnimSequence::GetNumSamples() const {
	return mNumSamples;
}

float UAnimSequence::GetDuration() const {
	return mNumSamples > 1 ? static_cast<float>((static_cast<double>(mNumSamples) - 1.0) / static_cast<double>(mSampleRate)) : 0.0f;
}

const TArray<FBoneInfo>& UAnimSequence::GetRefSkeleton() const {
	return mRefSkeleton;
}

const TArray<FAnimBoneTrack>& UAnimSequence::GetBoneTracks() const {
	return mBoneTracks;
}
