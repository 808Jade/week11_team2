#pragma once

#include "Animation/AnimSequenceData.h"
#include "Render/SkeletalMeshData.h"
#include "UObject/Object.h"
#include "AnimSequence.generated.h"

UCLASS()
class UAnimSequence : public UObject {
	GENERATED_BODY()

public:
	UAnimSequence() = default;

public:
	void Build(float InSampleRate, int32 InNumSamples, TArray<FBoneInfo> InRefSkeleton, TArray<FAnimBoneTrack> InBoneTracks);

	float GetSampleRate() const;
	int32 GetNumSamples() const;
	float GetDuration() const;

	const TArray<FBoneInfo>& GetRefSkeleton() const;
	const TArray<FAnimBoneTrack>& GetBoneTracks() const;

private:
	float mSampleRate{};
	int32 mNumSamples{};
	TArray<FBoneInfo> mRefSkeleton{};
	TArray<FAnimBoneTrack> mBoneTracks{};
};
