#pragma once

#include "Animation/BoneContainer.h"
#include "UObject/Object.h"

#include <vector>

#include "AnimInstance.generated.h"

class UAnimSequenceBase;
class USkeletalMeshComponent;
struct FAnimNode_Base;
struct FAnimNodeValue;

UCLASS()
class UAnimInstance : public UObject {
    GENERATED_BODY()

public:
    ~UAnimInstance() override;

public:
    void UpdateAnimation(float DeltaSeconds);
    bool EvaluateAnimation(TArray<FTransform>& OutPose);
    void GetReferencePose(TArray<FTransform>& OutPose) const;
    void EvaluateSequence(const UAnimSequenceBase& Sequence, double Time, TArray<FTransform>& OutPose) const;

    FAnimNode_Base* GetRootNode() const;
    uint64 GetUpdateCounter() const;
    const FVector& GetOwnerVelocity() const;
    virtual void GetDebugVariables(std::vector<FAnimNodeValue>& OutValues);
    virtual bool GetNodeLayout(const FAnimNode_Base* Node, float& X, float& Y) const;

protected:
    virtual void NativeInitializeAnimation();
    virtual void NativeUpdateAnimation(float DeltaSeconds);
    USkeletalMeshComponent* GetSkelMeshComponent() const;
    const FBoneContainer& GetRequiredBones() const;
    void SetRootNode(FAnimNode_Base* RootNode);

private:
    friend class USkeletalMeshComponent;
    bool InitializeAnimation(FString& OutError);

private:
    FBoneContainer mRequiredBones{};
    FAnimNode_Base* mRootNode{};
    uint64 mUpdateCounter{};
    FVector mLastOwnerLocation{};
    FVector mOwnerVelocity{};
    bool mHasLastOwnerLocation{};
};
