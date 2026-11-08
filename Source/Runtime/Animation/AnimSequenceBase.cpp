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
