#pragma once

#include "SkinnedMeshComponent.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimationAsset;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent {
	GENERATED_BODY()

public:
	using Super::Serialize;

public:
	~USkeletalMeshComponent() override;

public:
	void OnRegister() override;
	void TickComponent(float DeltaTime) override;
	bool PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping, float PlayRate, FString& OutError);
	bool SetAnimGraph(const FString& Path, FString& OutError);
	const FString& GetAnimGraph() const;
	UAnimInstance* GetAnimInstance() const;
	bool SetAnimGraphFloat(const FString& Name, float Value);
	bool RestartAnimGraphPlayer(int32 NodeId);
	bool IsAnimGraphPlayerFinished(int32 NodeId) const;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;
	void ResetPose();
	void RefreshBoneTransformsNow();

	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

private:
	friend class UAnimInstance;
	void RestoreAnimGraph();

private:
	UAnimInstance* mAnimScriptInstance{};
	TArray<FTransform> mBoneSpaceTransforms{};
	FString mAnimGraphPath{};

};
