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

	FVector GetRootLocationAtTime(double Time) const override;

private:
	// 시간을 앞뒤 키 인덱스와 보간 비율로 바꾼다 (GetAnimationPose와 같은 규칙)
	void GetKeyFrame(double Time, int32& OutFirst, int32& OutSecond, float& OutAlpha) const;
	const FRawAnimSequenceTrack* FindRootTrack() const;
};
