#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"

UAnimInstance::~UAnimInstance() {
    if (auto* Component{GetSkelMeshComponent()}; Component && Component->AnimScriptInstance == this) {
        Component->AnimScriptInstance = nullptr;
    }
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const {
    return Cast<USkeletalMeshComponent>(GetOuter());
}

bool UAnimInstance::InitializeAnimation(FString& OutError) {
    const USkeletalMeshComponent* Component{GetSkelMeshComponent()};
    const USkeletalMesh* Mesh{Component ? Component->GetSkeletalMesh() : nullptr};
    if (!Mesh) {
        OutError = "Set a skeletal mesh before initializing animation";
        return false;
    }
    if (!mRequiredBones.InitializeTo(*Mesh, OutError)) {
        return false;
    }

    mHasLastOwnerLocation = false;
    mOwnerVelocity = {};
    NativeInitializeAnimation();

    if (mRootNode) {
        mRootNode->Initialize(*this);
    }
    return true;
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds) {
    if (const auto* Component{GetSkelMeshComponent()}) {
        const FVector Location{Component->GetWorldLocation()};
        if (mHasLastOwnerLocation && DeltaSeconds > 0.0f) {
            mOwnerVelocity = (Location - mLastOwnerLocation) * (1.0f / DeltaSeconds);
        }
        mLastOwnerLocation = Location;
        mHasLastOwnerLocation = true;
    }

    NativeUpdateAnimation(DeltaSeconds);
    ++mUpdateCounter;

    if (mRootNode) {
        const FAnimationUpdateContext Context{*this, DeltaSeconds, mUpdateCounter};
        mRootNode->RecordUpdate(Context);
        mRootNode->Update(Context);
    }
}

bool UAnimInstance::EvaluateAnimation(TArray<FTransform>& OutPose) {
    if (!mRootNode || !mRequiredBones.IsValid()) {
        return false;
    }

    FPoseContext Context{*this, std::move(OutPose)};
    mRootNode->Evaluate(Context);
    OutPose = std::move(Context.mPose);
    return true;
}

void UAnimInstance::GetReferencePose(TArray<FTransform>& OutPose) const {
    mRequiredBones.FillWithRefPose(OutPose);
}

void UAnimInstance::EvaluateSequence(const UAnimSequenceBase& Sequence, double Time, TArray<FTransform>& OutPose) const {
    FAnimationPoseData PoseData{mRequiredBones, OutPose};
    Sequence.GetAnimationPose(PoseData, FAnimExtractContext{Time});
}

FAnimNode_Base* UAnimInstance::GetRootNode() const {
    return mRootNode;
}

uint64 UAnimInstance::GetUpdateCounter() const {
    return mUpdateCounter;
}

const FVector& UAnimInstance::GetOwnerVelocity() const {
    return mOwnerVelocity;
}

void UAnimInstance::GetDebugVariables(std::vector<FAnimNodeValue>&) {
}

bool UAnimInstance::GetNodeLayout(const FAnimNode_Base*, float&, float&) const {
    return false;
}

void UAnimInstance::NativeInitializeAnimation() {
}

void UAnimInstance::NativeUpdateAnimation(float) {
}

const FBoneContainer& UAnimInstance::GetRequiredBones() const {
    return mRequiredBones;
}

void UAnimInstance::SetRootNode(FAnimNode_Base* RootNode) {
    mRootNode = RootNode;
}
