#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_Base.h"
#include "Components/SkeletalMeshComponent.h"
#include <utility>

UAnimInstance::~UAnimInstance()
{
	if (auto* Component = GetSkelMeshComponent(); Component && Component->AnimScriptInstance == this)
		Component->AnimScriptInstance = nullptr;
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}

bool UAnimInstance::InitializeAnimation(FString& OutError)
{
	const USkeletalMeshComponent* Component = GetSkelMeshComponent();
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh)
	{
		OutError = "Set a skeletal mesh before initializing animation";
		return false;
	}
	if (!RequiredBones.InitializeTo(*Mesh, OutError)) return false;

	NativeInitializeAnimation();
	return true;
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	NativeUpdateAnimation(DeltaSeconds);
	if (!RootNode) return;

	FAnimNotifyQueue Queue;
	RootNode->Update(FAnimationUpdateContext{ DeltaSeconds, &Queue });
	TriggerAnimNotifies(Queue);
}

namespace
{
	bool IsSameNotify(const FAnimNotifyQueue::FEntry& A, const FAnimNotifyQueue::FEntry& B)
	{
		return A.Sequence == B.Sequence && A.Event == B.Event;
	}
}

void UAnimInstance::TriggerAnimNotifies(const FAnimNotifyQueue& Queue)
{
	// 시간이 흐르지 않았으면(정지·배속 0) NotifyState도 끝냅니다. 활성 State는 재생 중에만 존재합니다.
	if (!Queue.bTimeAdvanced)
	{
		EndAllNotifyStates();
		return;
	}

	TArray<FAnimNotifyQueue::FEntry> NewActiveStates;
	for (const FAnimNotifyQueue::FEntry& Entry : Queue.Entries)
	{
		if (!Entry.Event.IsNotifyState())
		{
			PendingNotifies.Add(FAnimNotifyDispatch{ Entry.Event, EAnimNotifyEventType::Notify, Entry.Sequence });
			continue;
		}
		// 루프로 구간이 둘로 나뉘면 같은 State가 두 번 들어올 수 있습니다.
		bool bDuplicate = false;
		for (const FAnimNotifyQueue::FEntry& Added : NewActiveStates)
			bDuplicate = bDuplicate || IsSameNotify(Added, Entry);
		if (!bDuplicate)
			NewActiveStates.Add(Entry);
	}

	// 지난 Update에는 있었는데 이번엔 없는 State → End
	for (const FAnimNotifyQueue::FEntry& Old : ActiveNotifyStates)
	{
		bool bStillActive = false;
		for (const FAnimNotifyQueue::FEntry& New : NewActiveStates)
			bStillActive = bStillActive || IsSameNotify(Old, New);
		if (!bStillActive)
			PendingNotifies.Add(FAnimNotifyDispatch{ Old.Event, EAnimNotifyEventType::StateEnd, Old.Sequence });
	}
	// 이번에 새로 들어온 State → Begin, 계속 있는 State → Tick
	for (const FAnimNotifyQueue::FEntry& New : NewActiveStates)
	{
		bool bWasActive = false;
		for (const FAnimNotifyQueue::FEntry& Old : ActiveNotifyStates)
			bWasActive = bWasActive || IsSameNotify(Old, New);
		PendingNotifies.Add(FAnimNotifyDispatch{ New.Event,
			bWasActive ? EAnimNotifyEventType::StateTick : EAnimNotifyEventType::StateBegin, New.Sequence });
	}

	ActiveNotifyStates = std::move(NewActiveStates);
}

void UAnimInstance::EndAllNotifyStates()
{
	for (const FAnimNotifyQueue::FEntry& Old : ActiveNotifyStates)
		PendingNotifies.Add(FAnimNotifyDispatch{ Old.Event, EAnimNotifyEventType::StateEnd, Old.Sequence });
	ActiveNotifyStates.Empty();
}

TArray<FAnimNotifyDispatch> UAnimInstance::TakePendingNotifies()
{
	TArray<FAnimNotifyDispatch> Out;
	Out.Swap(PendingNotifies);
	return Out;
}

bool UAnimInstance::EvaluateAnimation(TArray<FTransform>& OutPose)
{
	if (!RootNode || !RequiredBones.IsValid()) return false;

	FPoseContext Context(RequiredBones);
	Context.Pose = std::move(OutPose);
	RootNode->Evaluate(Context);
	OutPose = std::move(Context.Pose);
	return true;
}
