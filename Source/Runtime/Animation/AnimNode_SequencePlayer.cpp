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
	RootMotionDelta = FRootMotionDelta{};

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

	if (bRootMotionEnabled && Sequence && Sequence->HasRootTrack())
		ExtractRootMotion(PreviousTimeAccumulator, InternalTimeAccumulator, Length);

	if (FAnimNotifyQueue* Queue = Context.GetNotifyQueue(); Queue && Sequence)
		CollectNotifies(*Queue, PreviousTimeAccumulator, InternalTimeAccumulator, Length);
	bIncludeStartOnNextAdvance = false;
}

void FAnimNode_SequencePlayer::ExtractRootMotion(double Prev, double Current, double Length)
{
	// From → To 구간의 root 이동과 회전을 From 시점 root가 바라보던 방향 기준으로 구한다
	const auto Extract = [this](double From, double To, FVector& OutTranslation, FQuat& OutRotation)
	{
		const FQuat InvFromRot = Sequence->GetRootRotationAtTime(From).UnitInverse();
		OutTranslation = InvFromRot.RotateVector(Sequence->GetRootLocationAtTime(To) - Sequence->GetRootLocationAtTime(From));
		OutRotation = InvFromRot * Sequence->GetRootRotationAtTime(To);
	};

	FVector LocalTranslation;
	FQuat LocalRotation;
	if (bWrappedThisUpdate)
	{
		// 루프 경계: 진행 방향의 끝까지 간 구간 뒤에 반대쪽 끝에서 현재까지의 구간을 이어 붙인다.
		// 두 번째 구간은 첫 구간에서 돈 만큼 회전된 방향 기준이다.
		//   정방향 (Prev → Length) + (0 → Current), 역방향 (Prev → 0) + (Length → Current)
		const bool bForward = PlayRate > 0.0f;
		FVector Translation1, Translation2;
		FQuat Rotation1, Rotation2;
		Extract(Prev, bForward ? Length : 0.0, Translation1, Rotation1);
		Extract(bForward ? 0.0 : Length, Current, Translation2, Rotation2);
		LocalTranslation = Translation1 + Rotation1.RotateVector(Translation2);
		LocalRotation = Rotation1 * Rotation2;
	}
	else
		Extract(Prev, Current, LocalTranslation, LocalRotation);

	// Pose의 root는 첫 프레임 회전(LockRot)에 고정되므로, root 기준 값을 컴포넌트 공간 값으로 바꾼다
	const FQuat LockRot = Sequence->GetRootRotationAtTime(0.0);
	RootMotionDelta.Translation = LockRot.RotateVector(LocalTranslation);
	RootMotionDelta.Rotation = LockRot * LocalRotation * LockRot.UnitInverse();
	RootMotionDelta.RotationPivot = Sequence->GetRootLocationAtTime(0.0);
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
	Sequence->GetAnimationPose(PoseData, FAnimExtractContext{ InternalTimeAccumulator, bRootMotionEnabled });
}
