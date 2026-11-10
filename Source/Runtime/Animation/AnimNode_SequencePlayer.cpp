#include "EnginePCH.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifyTypes.h"
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
	// 음수(역재생)와 0(정지된 시간)도 허용합니다. NaN/무한대만 거부합니다.
	if (!std::isfinite(InPlayRate) || !std::isfinite(Length) || Length < 0.0)
	{
		OutError = "Invalid play rate or sequence length";
		return false;
	}

	Sequence = InSequence;
	PlayRate = InPlayRate;
	bLoopAnimation = bInLoopAnimation;
	bPlaying = true;
	bWrappedThisUpdate = false;
	bIncludeStartOnNextAdvance = true;
	// 역재생은 끝에서 시작해야 바로 멈추지 않습니다.
	InternalTimeAccumulator = PreviousTimeAccumulator = PlayRate < 0.0f ? Length : 0.0;
	return true;
}

void FAnimNode_SequencePlayer::SetAccumulatedTime(double NewTime)
{
	InternalTimeAccumulator = std::isfinite(NewTime)
		? std::clamp(NewTime, 0.0, static_cast<double>(GetCurrentAssetLength())) : 0.0;
	// 시간을 직접 옮긴 것(스크러빙·처음으로)은 재생으로 지나간 구간이 아닙니다.
	PreviousTimeAccumulator = InternalTimeAccumulator;
	bWrappedThisUpdate = false;
	bIncludeStartOnNextAdvance = true;
}

bool FAnimNode_SequencePlayer::SetPlayRate(float InPlayRate)
{
	if (!std::isfinite(InPlayRate)) return false;
	PlayRate = InPlayRate;
	return true;
}

float FAnimNode_SequencePlayer::GetCurrentAssetLength() const
{
	return Sequence ? Sequence->GetPlayLength() : 0.0f;
}

void FAnimNode_SequencePlayer::Update(const FAnimationUpdateContext& Context)
{
	// 정지했거나 시간이 흐르지 않은 프레임은 지나간 구간이 없습니다. (Previous == Current)
	PreviousTimeAccumulator = InternalTimeAccumulator;
	bWrappedThisUpdate = false;

	const float DeltaTime = Context.GetDeltaTime();
	const double Length = GetCurrentAssetLength();
	if (!bPlaying || PlayRate == 0.0f || Length <= 0.0 || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;

	double Next = InternalTimeAccumulator + static_cast<double>(DeltaTime) * PlayRate;
	if (bLoopAnimation)
	{
		// 양방향 루프: 끝을 넘으면 앞으로, 0 아래로 가면 끝으로 감쌉니다.
		// std::fmod는 음수를 넣으면 음수를 돌려주므로 Length를 더해 [0, Length)로 맞춥니다.
		if (Next >= Length || Next < 0.0)
		{
			Next = std::fmod(Next, Length);
			if (Next < 0.0) Next += Length;
			bWrappedThisUpdate = true;
		}
	}
	else if (Next >= Length || Next <= 0.0)
	{
		// 루프가 아니면 진행 방향의 끝에서 멈춥니다. (UE: 끝에 도달하면 재생 정지)
		Next = std::clamp(Next, 0.0, Length);
		bPlaying = false;
	}
	InternalTimeAccumulator = Next;

	if (FAnimNotifyQueue* Queue = Context.GetNotifyQueue(); Queue && Sequence)
		CollectNotifies(*Queue, PreviousTimeAccumulator, InternalTimeAccumulator, Length);
	bIncludeStartOnNextAdvance = false;
}

void FAnimNode_SequencePlayer::CollectNotifies(FAnimNotifyQueue& Queue, double From, double To, double Length) const
{
	if (From == To && !bWrappedThisUpdate) return;
	Queue.bTimeAdvanced = true;

	TArray<const FAnimNotifyEvent*> Found;
	if (!bWrappedThisUpdate)
	{
		Sequence->GetAnimNotifiesFromDeltaPositions(From, To, bIncludeStartOnNextAdvance, Found);
	}
	else if (PlayRate > 0.0f)
	{
		// 정방향으로 감쌈: (From, Length] 다음 [0, To]. 0에서 새로 출발하므로 두 번째 구간은 0을 포함합니다.
		Sequence->GetAnimNotifiesFromDeltaPositions(From, Length, bIncludeStartOnNextAdvance, Found);
		Sequence->GetAnimNotifiesFromDeltaPositions(0.0, To, true, Found);
	}
	else
	{
		// 역방향으로 감쌈: [0, From) 다음 [To, Length].
		Sequence->GetAnimNotifiesFromDeltaPositions(From, 0.0, bIncludeStartOnNextAdvance, Found);
		Sequence->GetAnimNotifiesFromDeltaPositions(Length, To, true, Found);
	}

	for (const FAnimNotifyEvent* Notify : Found)
		Queue.Entries.Add(FAnimNotifyQueue::FEntry{ *Notify, Sequence });
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
