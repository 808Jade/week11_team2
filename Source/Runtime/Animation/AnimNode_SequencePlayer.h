#pragma once

#include "Animation/AnimNode_Base.h"
#include "Core/EngineString.h"

class UAnimSequenceBase;

struct FAnimNode_SequencePlayer final : FAnimNode_Base
{
	bool SetSequence(const UAnimSequenceBase* InSequence, bool bInLoopAnimation, float InPlayRate, FString& OutError);
	void SetAccumulatedTime(double NewTime);

	double GetCurrentAssetTime() const { return InternalTimeAccumulator; }
	float GetCurrentAssetLength() const;

	FRootMotionDelta GetRootMotionDelta() const override { return RootMotionDelta; }

	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;

private:
	const UAnimSequenceBase* Sequence = nullptr;
	FRootMotionDelta RootMotionDelta;

	double InternalTimeAccumulator = 0.0;
	float PlayRate = 1.0f;
	bool bLoopAnimation = true;
};
