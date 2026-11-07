#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Serialization/MathSerialization.h"

#include <utility>

USkeletalMeshComponent::~USkeletalMeshComponent() {
	delete mAnimInstance;
}

void USkeletalMeshComponent::TickComponent(float DeltaTime) {
	if (mAnimInstance && mAnimInstance->Update(DeltaTime) && !ApplyAnimationPose()) {
		HTR_LOG(Warning, "Animation pose evaluation failed; restoring reference pose.");
		StopAnimation();
	}

	Super::TickComponent(DeltaTime);
}

bool USkeletalMeshComponent::PlayAnimation(UAnimSequence* Sequence, bool Looping, FString& OutError) {
	OutError.clear();
	if (!GetSkeletalMesh()) {
		OutError = "Animation: skeletal mesh is not assigned";
		return false;
	}

	if (!mAnimInstance) {
		mAnimInstance = NewObject<UAnimInstance>(this);
		mAnimInstance->Initialize(this);
	}

	if (!mAnimInstance->Play(Sequence, GetBones(), Looping, OutError)) {
		return false;
	}

	if (!ApplyAnimationPose()) {
		OutError = "Animation: initial pose evaluation failed";
		StopAnimation();
		return false;
	}

	RefreshBoneTransformsNow();
	return true;
}

void USkeletalMeshComponent::StopAnimation() {
	if (mAnimInstance) {
		mAnimInstance->Stop();
	}

	ResetBoneSpaceTransforms();
	MarkBoneTransformsDirty();
	RefreshBoneTransformsNow();
}

UAnimInstance* USkeletalMeshComponent::GetAnimInstance() const {
	return mAnimInstance;
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const {
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const {
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const {
	if (BoneIndex < 0 || BoneIndex >= mBoneSpaceTransforms.Num()) return FTransform{};
	return mBoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local) {
	if (BoneIndex < 0 || BoneIndex >= mBoneSpaceTransforms.Num()) return;
	mBoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const {
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const {
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose() {
	StopAnimation();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow() {
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms() {
	const TArray<FBoneInfo>& Bones{ GetBones() };
	if (mBoneSpaceTransforms.Num() != Bones.Num()) {
		ResetBoneSpaceTransforms();
	}

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int Index{}; Index < Bones.Num(); ++Index) {
		const FMatrix Local{ mBoneSpaceTransforms[Index].GetLocalMatrix() };
		const int32 Parent{ Bones[Index].ParentIndex };
		ComponentSpaceTransforms[Index] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record) {
	Super::Serialize(Record);   // 메시를 먼저 정해서 포즈를 바인드 포즈로 초기화한다

	FArchive& Ar{ Record.GetUnderlyingArchive() };
	int32 Num{ Ar.IsSaving() ? mBoneSpaceTransforms.Num() : 0 };
	FStructuredArchive::FArray PoseArray{ Record.EnterArray("BonePose", Num) };

	for (int32 I{}; I < Num; ++I) {
		FStructuredArchive::FRecord BoneRecord{ PoseArray.EnterElement().EnterRecord() };
		FString Name{ Ar.IsSaving() ? GetBones()[I].Name : FString{} };
		FTransform Local{ Ar.IsSaving() ? mBoneSpaceTransforms[I] : FTransform{} };
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading()) {
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index{ FindBoneIndex(Name) };
			if (Index >= 0)
				mBoneSpaceTransforms[Index] = Local;
		}
	}

	if (Ar.IsLoading())
		RefreshBoneTransformsNow();   // 에디터 월드에서 Tick이 안 돌아도 포즈가 바로 반영된다
}

void USkeletalMeshComponent::ResetBoneSpaceTransforms() {
	mBoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		mBoneSpaceTransforms.Add(Bone.LocalBindPose);
}

void USkeletalMeshComponent::OnSkeletalMeshChanged() {
	if (mAnimInstance) {
		mAnimInstance->Stop();
	}

	ResetBoneSpaceTransforms();
}

void USkeletalMeshComponent::OnAnimInstanceDestroyed(UAnimInstance* Instance) {
	if (mAnimInstance == Instance) {
		mAnimInstance = nullptr;
	}
}

bool USkeletalMeshComponent::ApplyAnimationPose() {
	TArray<FTransform> Pose{};
	if (!mAnimInstance || !GetSkeletalMesh() || !mAnimInstance->EvaluatePose(Pose) || Pose.Num() != GetBones().Num()) {
		return false;
	}

	mBoneSpaceTransforms = std::move(Pose);
	MarkBoneTransformsDirty();
	return true;
}
