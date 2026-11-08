#pragma once

#include "Animation/AnimationAsset.h"
#include "AnimSequenceBase.generated.h"

class UAnimDataModel;
struct FAnimationPoseData;

UCLASS()
class UAnimSequenceBase : public UAnimationAsset
{
	GENERATED_BODY()

public:
	~UAnimSequenceBase() override;
	const UAnimDataModel* GetDataModel() const { return DataModel; }
	float GetPlayLength() const override;
	virtual void GetAnimationPose(FAnimationPoseData& OutPoseData,
		const FAnimExtractContext& ExtractionContext) const = 0;

protected:
	void SetDataModel(UAnimDataModel* InDataModel) { DataModel = InDataModel; }

private:
	friend class UAnimDataModel;
	UAnimDataModel* DataModel = nullptr;
};
