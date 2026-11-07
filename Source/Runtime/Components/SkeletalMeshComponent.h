#pragma once

#include "SkinnedMeshComponent.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSequence;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent {
	GENERATED_BODY()
	friend class UAnimInstance;

public:
	using Super::Serialize;

public:
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override;
	USkeletalMeshComponent(const USkeletalMeshComponent&) = delete;
	USkeletalMeshComponent& operator=(const USkeletalMeshComponent&) = delete;

public:
	void TickComponent(float DeltaTime) override;

	bool PlayAnimation(UAnimSequence* Sequence, bool Looping, FString& OutError);
	void StopAnimation();
	UAnimInstance* GetAnimInstance() const;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;
	void ResetPose();
	void RefreshBoneTransformsNow();

	void Serialize(FStructuredArchive::FRecord Record) override;

private:
	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;
	void OnAnimInstanceDestroyed(UAnimInstance* Instance);
	void ResetBoneSpaceTransforms();
	bool ApplyAnimationPose();

private:
	TArray<FTransform> mBoneSpaceTransforms{};
	UAnimInstance* mAnimInstance{};
};
