#pragma once

#include "Animation/AnimNode_Base.h"
#include "Core/EngineString.h"

class UAnimSequenceBase;

// 애니메이션 시퀀스 하나를 시간에 따라 재생하는 노드. (UE FAnimNode_SequencePlayer)
// 재생 상태는 "현재 시간" 하나로 표현하고, 매 Update마다 DeltaTime × PlayRate만큼 시간을 옮깁니다.
// PlayRate가 음수면 시간이 거꾸로 흐르므로 역재생이 됩니다.
struct FAnimNode_SequencePlayer final : FAnimNode_Base
{
	// 시퀀스를 지정하고 처음부터 재생합니다. 역재생(PlayRate < 0)이면 끝에서 시작합니다.
	bool SetSequence(const UAnimSequenceBase* InSequence, bool bInLoopAnimation, float InPlayRate, FString& OutError);

	// 현재 시간을 옮깁니다. [0, Length]로 제한합니다. 이전 시간도 같이 옮기므로 스크러빙은 Notify 구간으로 치지 않습니다.
	void SetAccumulatedTime(double NewTime);

	////////////////////////////////////////////////////////////
	// 재생 제어 (Timeline 버튼)
	////////////////////////////////////////////////////////////

	// 재생/정지. 정지해도 현재 시간은 그대로 유지됩니다.
	void SetPlaying(bool bInPlaying) { bPlaying = bInPlaying; }

	// 배속. 음수면 역재생, 0이면 시간이 흐르지 않습니다. 유한한 값만 받습니다.
	bool SetPlayRate(float InPlayRate);

	void SetLooping(bool bInLooping) { bLoopAnimation = bInLooping; }

	// root 본(Skeleton 0번)의 이동·회전을 Pose에서 빼서 RootMotionDelta로 내보낼지. 기본은 끔(UE의 Enable Root Motion과 같다).
	// 끄면 root 본도 다른 본처럼 Pose 안에서 움직입니다. 골반이 root인 Skeleton(Mixamo 등)은 켜면 몸 전체가 흔들립니다.
	void SetRootMotionEnabled(bool bInEnabled) { bRootMotionEnabled = bInEnabled; }
	bool IsRootMotionEnabled() const { return bRootMotionEnabled; }

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const UAnimSequenceBase* GetSequence() const { return Sequence; }
	double GetCurrentAssetTime() const { return InternalTimeAccumulator; }
	// 직전 Update 전의 시간. 이번 프레임에 지나간 구간 [Previous → Current]으로 Notify를 판정할 때 씁니다.
	double GetPreviousAssetTime() const { return PreviousTimeAccumulator; }
	float GetCurrentAssetLength() const;
	float GetPlayRate() const { return PlayRate; }
	bool IsPlaying() const { return bPlaying; }
	bool IsLooping() const { return bLoopAnimation; }
	// 이번 Update에서 루프 경계를 넘어 시간이 감싸졌는지. (Notify 구간을 둘로 나눠야 하는 경우)
	bool DidWrapThisUpdate() const { return bWrappedThisUpdate; }

	// 이번 Update에 지나간 구간의 root 이동량. 정지·배속 0이거나 Root Motion이 꺼져 있거나 root 트랙이 없으면 0입니다.
	FRootMotionDelta GetRootMotionDelta() const override { return RootMotionDelta; }

	void Update(const FAnimationUpdateContext& Context) override;
	void Evaluate(FPoseContext& Output) override;

private:
	// 이번 Update에 지나간 구간의 Notify를 Queue에 넣습니다. 루프로 감싸졌으면 구간을 둘로 나눕니다.
	void CollectNotifies(FAnimNotifyQueue& Queue, double From, double To, double Length) const;
	// 이번 Update에 지나간 구간(Prev → Current)의 root 이동량을 RootMotionDelta에 넣습니다. 역재생·루프 감쌈도 처리합니다.
	void ExtractRootMotion(double Prev, double Current, double Length);

	const UAnimSequenceBase* Sequence = nullptr;
	FRootMotionDelta RootMotionDelta;

	double InternalTimeAccumulator = 0.0;
	double PreviousTimeAccumulator = 0.0;
	float PlayRate = 1.0f;
	bool bLoopAnimation = true;
	bool bPlaying = true;
	bool bRootMotionEnabled = false;
	bool bWrappedThisUpdate = false;
	// 시간을 직접 정한 뒤(재생 시작·스크러빙·처음으로) 아직 한 번도 진행하지 않았는지.
	// 그 첫 구간은 출발 시점도 포함해서, 0초나 멈춘 자리에 있는 Notify도 재생을 시작하면 발동합니다.
	bool bIncludeStartOnNextAdvance = true;
};
