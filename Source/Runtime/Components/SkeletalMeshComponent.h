#pragma once

#include "SkinnedMeshComponent.h"
#include "SkeletalMeshComponent.generated.h"

struct FAnimationClipData;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;
	void ResetPose();
	void RefreshBoneTransformsNow();

	// 클립 트랙마다 대응하는 본 인덱스를 이름으로 찾습니다. 없는 본은 -1입니다.
	// 정확히 같은 이름을 먼저 찾고, 없으면 대소문자, 구분자, 끝 번호를 지운 이름으로 찾습니다.
	TArray<int32> MapAnimationTracks(const FAnimationClipData& Clip) const;
	// Time(초) 위치의 포즈를 샘플링해 본에 적용합니다. 트랙이 없는 본은 건드리지 않습니다.
	void ApplyAnimationPose(const FAnimationClipData& Clip, const TArray<int32>& TrackToBone, float Time);

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

private:
	TArray<FTransform> BoneSpaceTransforms;

};