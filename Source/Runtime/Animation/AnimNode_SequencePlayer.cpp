#include "EnginePCH.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequenceBase.h"
#include <algorithm>
#include <cmath>

bool FAnimNode_SequencePlayer::SetSequence(const UAnimSequenceBase* InSequence,
	bool bInLoopAnimation, float InPlayRate, FString& OutError)
{
	if (!InSequence || !InSequence->GetSkeleton())
	{
		OutError = "Set an initialized animation sequence first";
		return false;
	}
	const double Length = InSequence->GetPlayLength();
	if (!std::isfinite(InPlayRate) || InPlayRate <= 0.0f || !std::isfinite(Length) || Length < 0.0)
	{
		OutError = "Invalid play rate or sequence length";
		return false;
	}

	Sequence = InSequence;
	InternalTimeAccumulator = 0.0;
	PlayRate = InPlayRate;
	bLoopAnimation = bInLoopAnimation;
	return true;
}

void FAnimNode_SequencePlayer::SetAccumulatedTime(double NewTime)
{
	InternalTimeAccumulator = std::isfinite(NewTime)
		? std::clamp(NewTime, 0.0, static_cast<double>(GetCurrentAssetLength())) : 0.0;
}

float FAnimNode_SequencePlayer::GetCurrentAssetLength() const
{
	return Sequence ? Sequence->GetPlayLength() : 0.0f;
}

void FAnimNode_SequencePlayer::Update(const FAnimationUpdateContext& Context)
{
	RootMotionDelta = FVector(0, 0, 0);
	const float DeltaTime = Context.GetDeltaTime();
	const double Length = GetCurrentAssetLength();
	if (Length <= 0.0 || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const double Prev = InternalTimeAccumulator;
	const double Next = InternalTimeAccumulator + static_cast<double>(DeltaTime) * PlayRate;
	InternalTimeAccumulator = bLoopAnimation ? std::fmod(Next, Length) : std::min(Next, Length);

	const FVector PrevRoot = Sequence->GetRootLocationAtTime(Prev);
	const FVector CurrRoot = Sequence->GetRootLocationAtTime(InternalTimeAccumulator);
	if (bLoopAnimation && Next >= Length)
	{
		// 루프 경계를 넘은 프레임: (끝 - 이전) + (현재 - 시작)으로 나눠서 더한다
		const FVector StartRoot = Sequence->GetRootLocationAtTime(0.0);
		const FVector EndRoot = Sequence->GetRootLocationAtTime(Length);
		RootMotionDelta = (EndRoot - PrevRoot) + (CurrRoot - StartRoot);
	}
	else
		RootMotionDelta = CurrRoot - PrevRoot;
}

void FAnimNode_SequencePlayer::Evaluate(FPoseContext& Output)
{
	if (!Sequence)
	{
		Output.ResetToRefPose();
		return;
	}

	FAnimationPoseData PoseData{ Output.RequiredBones, Output.Pose };
	Sequence->GetAnimationPose(PoseData, FAnimExtractContext{ InternalTimeAccumulator });
}
