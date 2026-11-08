#pragma once

#include "UObject/Object.h"
#include "Animation/AnimTrackTypes.h"
#include "AnimDataModel.generated.h"

UCLASS()
class UAnimDataModel : public UObject
{
	GENERATED_BODY()

public:
	~UAnimDataModel() override;
	const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const { return BoneAnimationTracks; }
	float GetFrameRate() const { return FrameRate; }
	int32 GetNumberOfKeys() const { return NumberOfKeys; }
	int32 GetNumberOfFrames() const { return NumberOfKeys > 0 ? NumberOfKeys - 1 : 0; }
	float GetPlayLength() const { return FrameRate > 0.0f ? GetNumberOfFrames() / FrameRate : 0.0f; }

private:
	friend class UAnimSequence;
	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	int32 NumberOfKeys = 0;
	float FrameRate = 0.0f;
};
