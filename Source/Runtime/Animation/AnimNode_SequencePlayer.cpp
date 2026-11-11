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
	RootMotionDelta = FRootMotionDelta{};
	const float DeltaTime = Context.GetDeltaTime();
	const double Length = GetCurrentAssetLength();
	if (Length <= 0.0 || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) return;
	const double Prev = InternalTimeAccumulator;
	const double Next = InternalTimeAccumulator + static_cast<double>(DeltaTime) * PlayRate;
	InternalTimeAccumulator = bLoopAnimation ? std::fmod(Next, Length) : std::min(Next, Length);

	// From → To 구간의 root 이동과 회전을 From 시점 root가 바라보던 방향 기준으로 구한다
	const auto Extract = [this](double From, double To, FVector& OutTranslation, FQuat& OutRotation)
	{
		const FQuat InvFromRot = Sequence->GetRootRotationAtTime(From).UnitInverse();
		OutTranslation = InvFromRot.RotateVector(Sequence->GetRootLocationAtTime(To) - Sequence->GetRootLocationAtTime(From));
		OutRotation = InvFromRot * Sequence->GetRootRotationAtTime(To);
	};

	FVector LocalTranslation;
	FQuat LocalRotation;
	if (bLoopAnimation && Next >= Length)
	{
		// 루프 경계: (이전 → 끝) 뒤에 (시작 → 현재)를 이어 붙인다. 두 번째 구간은 첫 구간에서 돈 만큼 회전된 방향 기준이다
		FVector Translation1, Translation2;
		FQuat Rotation1, Rotation2;
		Extract(Prev, Length, Translation1, Rotation1);
		Extract(0.0, InternalTimeAccumulator, Translation2, Rotation2);
		LocalTranslation = Translation1 + Rotation1.RotateVector(Translation2);
		LocalRotation = Rotation1 * Rotation2;
	}
	else
		Extract(Prev, InternalTimeAccumulator, LocalTranslation, LocalRotation);

	// Pose의 root는 첫 프레임 회전(LockRot)에 고정되므로, root 기준 값을 컴포넌트 공간 값으로 바꾼다
	const FQuat LockRot = Sequence->GetRootRotationAtTime(0.0);
	RootMotionDelta.Translation = LockRot.RotateVector(LocalTranslation);
	RootMotionDelta.Rotation = LockRot * LocalRotation * LockRot.UnitInverse();
}

void FAnimNode_SequencePlayer::Evaluate(FPoseContext& Output)
{
	if (!Sequence)
	{
		Output.ResetToRefPose();
		return;
	}

	FAnimationPoseData PoseData{ Output.RequiredBones, Output.Pose };
	Sequence->GetAnimationPose(PoseData, FAnimExtractContext{ InternalTimeAccumulator, true });
}
