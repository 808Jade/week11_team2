#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimSingleNodeInstance.generated.h"

class UAnimationAsset;

UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	bool SetAnimationAsset(UAnimationAsset* NewAsset, bool bLooping, float PlayRate, FString& OutError);
	UAnimationAsset* GetAnimationAsset() const;
	void PlayAnim();

protected:
	void NativeInitializeAnimation() override;

private:
	UAnimationAsset* mCurrentAsset{};
	FAnimNode_SequencePlayer mSingleNode{};
};
