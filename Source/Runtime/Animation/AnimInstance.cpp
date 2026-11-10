#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_Base.h"
#include "Components/SkeletalMeshComponent.h"
#include <utility>

UAnimInstance::~UAnimInstance()
{
	if (auto* Component = GetSkelMeshComponent(); Component && Component->AnimScriptInstance == this)
		Component->AnimScriptInstance = nullptr;
}

USkeletalMeshComponent* UAnimInstance::GetSkelMeshComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}

bool UAnimInstance::InitializeAnimation(FString& OutError)
{
	const USkeletalMeshComponent* Component = GetSkelMeshComponent();
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh)
	{
		OutError = "Set a skeletal mesh before initializing animation";
		return false;
	}
	if (!RequiredBones.InitializeTo(*Mesh, OutError)) return false;

	NativeInitializeAnimation();
	return true;
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	NativeUpdateAnimation(DeltaSeconds);
	if (RootNode) RootNode->Update(FAnimationUpdateContext{ DeltaSeconds });
}

bool UAnimInstance::EvaluateAnimation(TArray<FTransform>& OutPose)
{
	if (!RootNode || !RequiredBones.IsValid()) return false;

	FPoseContext Context(RequiredBones);
	Context.Pose = std::move(OutPose);
	RootNode->Evaluate(Context);
	OutPose = std::move(Context.Pose);
	return true;
}

FVector UAnimInstance::GetRootMotionDelta() const
{
	return RootNode ? RootNode->GetRootMotionDelta() : FVector(0, 0, 0);
}