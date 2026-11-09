#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimationAsset.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	~USkeletalMeshComponent() override;
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;
	bool PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping, float PlayRate, FString& OutError);

	// UE의 Animation Mode = Use Animation Asset. 게임 시작 시 AnimToPlay를 자동 재생한다(스크립트 불필요).
	// 메시와 같은 Skeleton의 애니메이션만 재생된다. Details 패널은 같은 Skeleton의 애니메이션만 보여 준다.
	UPROPERTY(EditAnywhere)
	UAnimationAsset* AnimToPlay = nullptr;

	UPROPERTY(EditAnywhere)
	bool bLoopAnimation = true;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.01, UIMin=0.01, UIMax=5, Delta=0.01))
	float AnimPlayRate = 1.0f;

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
