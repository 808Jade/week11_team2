#pragma once

#include "UObject/Object.h"
#include "Animation/BoneContainer.h"
#include "Animation/AnimNotifyTypes.h"
#include "AnimInstance.generated.h"

class USkeletalMeshComponent;
struct FAnimNode_Base;

UCLASS()
class UAnimInstance : public UObject
{
	GENERATED_BODY()

public:
	~UAnimInstance() override;
	void UpdateAnimation(float DeltaSeconds);
	bool EvaluateAnimation(TArray<FTransform>& OutPose);

protected:
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}
	USkeletalMeshComponent* GetSkelMeshComponent() const;
	const FBoneContainer& GetRequiredBones() const { return RequiredBones; }
	FAnimNode_Base* RootNode = nullptr;

	// 활성 NotifyState를 모두 끝냅니다(End를 보냄). 재생 시작·애니메이션 교체·시간을 직접 옮길 때 부릅니다.
	void EndAllNotifyStates();

private:
	friend class USkeletalMeshComponent;
	bool InitializeAnimation(FString& OutError);

	// 노드가 모은 Queue로 Notify/StateBegin/StateTick/StateEnd를 정해 PendingNotifies에 쌓습니다. (UE TriggerAnimNotifies)
	void TriggerAnimNotifies(const FAnimNotifyQueue& Queue);

	// 컴포넌트가 이번 프레임에 보낼 Notify를 가져갑니다. 보내는 도중 인스턴스가 지워져도 안전하도록 통째로 넘깁니다.
	TArray<FAnimNotifyDispatch> TakePendingNotifies();

	FBoneContainer RequiredBones;

	// 지금 구간 안에 있는 NotifyState (애셋 배열을 편집해도 안전하도록 복사본)
	TArray<FAnimNotifyQueue::FEntry> ActiveNotifyStates;
	TArray<FAnimNotifyDispatch> PendingNotifies;
};
