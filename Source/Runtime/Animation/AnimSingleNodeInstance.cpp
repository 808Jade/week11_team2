#include "EnginePCH.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"

bool UAnimSingleNodeInstance::SetAnimationAsset(UAnimationAsset* NewAsset, bool Looping, float PlayRate, FString& OutError) {
    const auto* Sequence{Cast<UAnimSequenceBase>(NewAsset)};

    if (Sequence == nullptr) {
        OutError = "This single-node player currently supports animation sequences";
        return false;
    }

    if (Sequence->GetSkeleton() == nullptr || Sequence->GetSkeleton() != GetRequiredBones().GetSkeletonAsset()) {
        OutError = "Animation and mesh must share the same skeleton asset";
        return false;
    }

    EndAllNotifyStates();

    if (!mSingleNode.SetSequence(Sequence, Looping, PlayRate, OutError)) {
        return false;
    }

    mCurrentAsset = NewAsset;
    return true;
}

UAnimationAsset* UAnimSingleNodeInstance::GetAnimationAsset() const {
    return mCurrentAsset;
}

void UAnimSingleNodeInstance::PlayAnim() {
    EndAllNotifyStates();
    mSingleNode.Restart();
}

void UAnimSingleNodeInstance::NativeInitializeAnimation() {
    // 단일 재생과 그래프 재생 모두 UAnimInstance의 루트 평가를 사용한다.
    SetRootNode(&mSingleNode);
}

void UAnimSingleNodeInstance::SetPlaying(bool Playing) {
    mSingleNode.SetPlaying(Playing);
}

bool UAnimSingleNodeInstance::IsPlaying() const {
    return mSingleNode.IsPlaying();
}

void UAnimSingleNodeInstance::SetPosition(float Position) {
    EndAllNotifyStates();
    mSingleNode.SetAccumulatedTime(Position);
}

float UAnimSingleNodeInstance::GetCurrentTime() const {
    return static_cast<float>(mSingleNode.GetAccumulatedTime());
}

float UAnimSingleNodeInstance::GetPreviousTime() const {
    return static_cast<float>(mSingleNode.GetPreviousAssetTime());
}

float UAnimSingleNodeInstance::GetLength() const {
    return static_cast<float>(mSingleNode.GetCurrentAssetLength());
}

bool UAnimSingleNodeInstance::SetPlayRate(float PlayRate) {
    return mSingleNode.SetPlayRate(PlayRate);
}

float UAnimSingleNodeInstance::GetPlayRate() const {
    return mSingleNode.GetPlayRate();
}

void UAnimSingleNodeInstance::SetLooping(bool Looping) {
    mSingleNode.SetLooping(Looping);
}

bool UAnimSingleNodeInstance::IsLooping() const {
    return mSingleNode.IsLooping();
}

bool UAnimSingleNodeInstance::DidWrapThisUpdate() const {
    return mSingleNode.DidWrapThisUpdate();
}
