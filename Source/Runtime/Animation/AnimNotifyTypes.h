#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Containers/Array.h"

class UAnimSequenceBase;

// 애니메이션의 특정 시간에 붙이는 이벤트. (UE FAnimNotifyEvent)
//   Duration == 0 : Notify       - TriggerTime을 지나가는 순간 한 번 발동 (발소리, 효과음)
//   Duration >  0 : NotifyState  - [TriggerTime, TriggerTime + Duration] 구간에 들어갈 때 Begin, 머무는 동안 Tick, 나갈 때 End (공격 판정 구간)
// 지금은 이름만 있는 Notify입니다. 받는 쪽(Lua, 델리게이트)이 이름으로 구분합니다.
struct FAnimNotifyEvent
{
	FString NotifyName;
	float TriggerTime = 0.0f; // 초
	float Duration = 0.0f;    // 초. 0이면 Notify
	int32 TrackIndex = 0;     // Timeline에서 그릴 줄 번호 (발동과는 무관)

	bool IsNotifyState() const { return Duration > 0.0f; }
	float GetEndTriggerTime() const { return TriggerTime + (IsNotifyState() ? Duration : 0.0f); }

	bool operator==(const FAnimNotifyEvent& Other) const
	{
		return NotifyName == Other.NotifyName && TriggerTime == Other.TriggerTime
			&& Duration == Other.Duration && TrackIndex == Other.TrackIndex;
	}
};

enum class EAnimNotifyEventType : uint8
{
	Notify,     // Duration == 0 인 Notify가 발동
	StateBegin, // NotifyState 구간에 들어감
	StateTick,  // NotifyState 구간에 머무는 중 (매 Update)
	StateEnd,   // NotifyState 구간에서 나감 (또는 재생이 멈춤)
};

// 밖(컴포넌트 → 델리게이트·Lua)으로 보낼 Notify 하나. 애셋 배열이 바뀌어도 안전하도록 이벤트를 복사해 둡니다.
struct FAnimNotifyDispatch
{
	FAnimNotifyEvent Event;
	EAnimNotifyEventType Type = EAnimNotifyEventType::Notify;
	const UAnimSequenceBase* Sequence = nullptr;
};

// 이번 Update에서 노드들이 지나간 구간에 걸린 Notify를 모으는 곳. (UE FAnimNotifyQueue)
// UAnimInstance가 만들어 FAnimationUpdateContext로 노드에 넘깁니다.
struct FAnimNotifyQueue
{
	struct FEntry
	{
		FAnimNotifyEvent Event;
		const UAnimSequenceBase* Sequence = nullptr;
	};

	TArray<FEntry> Entries;
	// 이번 Update에 시간이 실제로 흘렀는지. 흐르지 않았으면(정지) 활성 NotifyState를 모두 끝냅니다.
	bool bTimeAdvanced = false;
};
