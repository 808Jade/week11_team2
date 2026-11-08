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
	UAnimationAsset* GetAnimationAsset() const { return CurrentAsset; }
	void PlayAnim() { SingleNode.SetAccumulatedTime(0.0); }

protected:
	void NativeInitializeAnimation() override { RootNode = &SingleNode; }

private:
	UAnimationAsset* CurrentAsset = nullptr;
	FAnimNode_SequencePlayer SingleNode;
};
