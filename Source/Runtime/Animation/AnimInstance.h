#pragma once

#include "UObject/Object.h"
#include "Animation/BoneContainer.h"
#include "AnimInstance.generated.h"

class USkeletalMeshComponent;
struct FAnimNode_Base;

UCLASS()
class UAnimInstance : public UObject
{
	GENERATED_BODY()

public:
	~UAnimInstance() override;
	void UpdateAnimation(float DeltaSeconds);
	bool EvaluateAnimation(TArray<FTransform>& OutPose);

	FVector GetRootMotionDelta() const;

protected:
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}
	USkeletalMeshComponent* GetSkelMeshComponent() const;
	const FBoneContainer& GetRequiredBones() const { return RequiredBones; }
	FAnimNode_Base* RootNode = nullptr;

private:
	friend class USkeletalMeshComponent;
	bool InitializeAnimation(FString& OutError);
	FBoneContainer RequiredBones;
};
