#pragma once

#include "SkinnedMeshComponent.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimationAsset;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	~USkeletalMeshComponent() override;
	void TickComponent(float DeltaTime) override;
	bool PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping, float PlayRate, FString& OutError);

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;
	void ResetPose();
	void RefreshBoneTransformsNow();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

private:
	friend class UAnimInstance;
	UAnimInstance* AnimScriptInstance = nullptr;
	TArray<FTransform> BoneSpaceTransforms;

};
