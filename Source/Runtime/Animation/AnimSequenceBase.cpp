#include "EnginePCH.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimDataModel.h"
#include <utility>

UAnimSequenceBase::~UAnimSequenceBase()
{
	delete std::exchange(DataModel, nullptr);
}

UAnimDataModel::~UAnimDataModel()
{
	if (auto* Sequence = Cast<UAnimSequenceBase>(GetOuter()); Sequence && Sequence->DataModel == this)
		Sequence->DataModel = nullptr;
}

float UAnimSequenceBase::GetPlayLength() const
{
	return DataModel ? DataModel->GetPlayLength() : 0.0f;
}

void UAnimSequenceBase::GetAnimNotifiesFromDeltaPositions(double From, double To, bool bIncludeFrom,
	TArray<const FAnimNotifyEvent*>& OutNotifies) const
{
	if (From == To && !bIncludeFrom) return;

	const bool bForward = From <= To;
	for (const FAnimNotifyEvent& Notify : Notifies)
	{
		const double Start = Notify.TriggerTime;
		const double End = Notify.GetEndTriggerTime();

		// 정방향 (From, To] : 이벤트 [Start, End]가 To 이전에 시작하고 From 이후에 끝나야 겹칩니다.
		// 역방향 [To, From) : 거울상. Notify(Start == End)는 시점 하나가 구간 안에 있는지와 같습니다.
		const bool bOverlaps = bForward
			? (Start <= To && (bIncludeFrom ? End >= From : End > From))
			: (End >= To && (bIncludeFrom ? Start <= From : Start < From));
		if (bOverlaps)
			OutNotifies.Add(&Notify);
	}
}
