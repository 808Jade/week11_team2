#include "EnginePCH.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"

bool UAnimSingleNodeInstance::SetAnimationAsset(UAnimationAsset* NewAsset, bool bLooping,
	float PlayRate, FString& OutError)
{
	const auto* Sequence = Cast<UAnimSequenceBase>(NewAsset);
	if (!Sequence)
	{
		OutError = "This single-node player currently supports animation sequences";
		return false;
	}
	if (!Sequence->GetSkeleton() || Sequence->GetSkeleton() != GetRequiredBones().GetSkeletonAsset())
	{
		OutError = "Animation and mesh must share the same skeleton asset";
		return false;
	}
	if (!mSingleNode.SetSequence(Sequence, bLooping, PlayRate, OutError)) return false;

	mCurrentAsset = NewAsset;
	return true;
}

UAnimationAsset* UAnimSingleNodeInstance::GetAnimationAsset() const {
    return mCurrentAsset;
}

void UAnimSingleNodeInstance::PlayAnim() {
    mSingleNode.SetAccumulatedTime(0.0);
}

void UAnimSingleNodeInstance::NativeInitializeAnimation() {
    SetRootNode(&mSingleNode);
}
