#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimSingleNodeInstance.generated.h"

// Windows.h의 GetCurrentTime 매크로가 UE와 같은 함수 이름을 바꿔 버리지 않게
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

class UAnimationAsset;

UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	bool SetAnimationAsset(UAnimationAsset* NewAsset, bool bLooping, float PlayRate, FString& OutError);
	UAnimationAsset* GetAnimationAsset() const { return CurrentAsset; }

	// 처음부터 재생합니다. 역재생이면 끝에서 시작합니다.
	void PlayAnim();

	////////////////////////////////////////////////////////////
	// 재생 제어 (UE UAnimSingleNodeInstance와 같은 이름)
	////////////////////////////////////////////////////////////

	void SetPlaying(bool bIsPlaying) { SingleNode.SetPlaying(bIsPlaying); }
	bool IsPlaying() const { return SingleNode.IsPlaying(); }

	// 현재 시간(초)을 옮깁니다. [0, Length]로 제한합니다. 건너뛴 구간의 Notify는 발동하지 않고, 활성 NotifyState는 끝냅니다.
	void SetPosition(float InPosition)
	{
		EndAllNotifyStates();
		SingleNode.SetAccumulatedTime(InPosition);
	}
	float GetCurrentTime() const { return static_cast<float>(SingleNode.GetCurrentAssetTime()); }
	float GetPreviousTime() const { return static_cast<float>(SingleNode.GetPreviousAssetTime()); }
	float GetLength() const { return SingleNode.GetCurrentAssetLength(); }

	// 음수면 역재생입니다. (UE도 PlayRate 부호로 역재생합니다)
	bool SetPlayRate(float InPlayRate) { return SingleNode.SetPlayRate(InPlayRate); }
	float GetPlayRate() const { return SingleNode.GetPlayRate(); }

	void SetLooping(bool bIsLooping) { SingleNode.SetLooping(bIsLooping); }
	bool IsLooping() const { return SingleNode.IsLooping(); }

	bool DidWrapThisUpdate() const { return SingleNode.DidWrapThisUpdate(); }

	// root 본의 이동을 Pose에서 빼서 Actor 이동으로 넘길지. (USkeletalMeshComponent::bEnableRootMotion이 매 Tick 넘깁니다)
	void SetRootMotionEnabled(bool bEnabled) { SingleNode.SetRootMotionEnabled(bEnabled); }
	bool IsRootMotionEnabled() const { return SingleNode.IsRootMotionEnabled(); }

protected:
	void NativeInitializeAnimation() override { RootNode = &SingleNode; }

private:
	UAnimationAsset* CurrentAsset = nullptr;
	FAnimNode_SequencePlayer SingleNode;
};
