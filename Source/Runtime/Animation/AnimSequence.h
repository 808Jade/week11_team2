#pragma once

#include "Animation/AnimSequenceData.h"
#include "Render/SkeletalMeshData.h"
#include "UObject/Object.h"
#include "AnimSequence.generated.h"

class USkeleton;

UCLASS()
class UAnimSequence : public UObject {
	GENERATED_BODY()

public:
	UAnimSequence() = default;

public:
	void Build(float InSampleRate, int32 InNumSamples, USkeleton* InSkeleton, TArray<FAnimBoneTrack> InBoneTracks);
	bool Validate(FString& OutError) const;
	static bool ValidateData(float SampleRate, int32 NumSamples, const TArray<FBoneInfo>& RefSkeleton, const TArray<FAnimBoneTrack>& BoneTracks, FString& OutError);

	void SetPath(const FString& Path);
	const FString& GetPath() const;
	uint64 GetRevision() const;

	float GetSampleRate() const;
	int32 GetNumSamples() const;
	float GetDuration() const;

	USkeleton* GetSkeleton() const;
	const TArray<FAnimBoneTrack>& GetBoneTracks() const;

private:
	FString mPath{};
	uint64 mRevision{};
	float mSampleRate{};
	int32 mNumSamples{};
	USkeleton* mSkeleton{};
	TArray<FAnimBoneTrack> mBoneTracks{};
};
