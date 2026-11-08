#pragma once

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimDataModel.h"
#include "AnimSequence.generated.h"

UCLASS()
class UAnimSequence : public UAnimSequenceBase
{
	GENERATED_BODY()

public:
	bool Initialize(const USkeleton* InSkeleton, int32 InNumberOfKeys, float InFrameRate,
		TArray<FBoneAnimationTrack> InTracks, FString& OutError);
	void GetAnimationPose(FAnimationPoseData& OutPoseData,
		const FAnimExtractContext& ExtractionContext) const override;
};
