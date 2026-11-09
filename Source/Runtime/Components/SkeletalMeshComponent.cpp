#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Serialization/MathSerialization.h"
#include <memory>
#include <utility>

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    delete std::exchange(AnimScriptInstance, nullptr);
}

void USkeletalMeshComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!AnimToPlay) return;

    FString Error;
    if (!PlayAnimation(AnimToPlay, bLoopAnimation, AnimPlayRate, Error))
        HTR_LOG(Warning, "AnimToPlay '{}' could not play on {}: {}", AnimToPlay->GetPath(), GetName(), Error);
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    if (AnimScriptInstance)
    {
        AnimScriptInstance->UpdateAnimation(DeltaTime);
        if (AnimScriptInstance->EvaluateAnimation(BoneSpaceTransforms))
            MarkBoneTransformsDirty();
    }
    Super::TickComponent(DeltaTime);
}

bool USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping,
    float PlayRate, FString& OutError)
{
    OutError.clear();
    if (!GetSkeletalMesh() || !NewAnimToPlay)
    {
        OutError = "Set a skeletal mesh and animation asset first";
        return false;
    }

    auto* SingleNode = Cast<UAnimSingleNodeInstance>(AnimScriptInstance);
    std::unique_ptr<UAnimSingleNodeInstance> NewInstance;
    if (!SingleNode)
    {
        NewInstance.reset(NewObject<UAnimSingleNodeInstance>(this, NAME_None, EObjectFlags::RF_Transient));
        if (!NewInstance)
        {
            OutError = "Could not create animation instance";
            return false;
        }
        if (!NewInstance->InitializeAnimation(OutError)) return false;
        SingleNode = NewInstance.get();
    }
    if (!SingleNode->SetAnimationAsset(NewAnimToPlay, bLooping, PlayRate, OutError)) return false;

    if (NewInstance)
        delete std::exchange(AnimScriptInstance, NewInstance.release());
    SingleNode->PlayAnim();
    SingleNode->EvaluateAnimation(BoneSpaceTransforms);
    RefreshBoneTransformsNow();
    return true;
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
    static const TArray<FBoneInfo> Empty{};
    return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
    return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
    if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return FTransform();
    return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
    if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return;
    BoneSpaceTransforms[BoneIndex] = Local;
    MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
    if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
    return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
    return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
    OnSkeletalMeshChanged();
    MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
    UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
    const TArray<FBoneInfo>& Bones = GetBones();
    if (BoneSpaceTransforms.Num() != Bones.Num())
        OnSkeletalMeshChanged();

    ComponentSpaceTransforms.SetNum(Bones.Num());
    for (int i = 0; i < Bones.Num(); ++i)
    {
        const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
        const int32 Parent = Bones[i].ParentIndex;
        ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
    }
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);   // 메시를 먼저 정해서 포즈를 바인드 포즈로 초기화한다

    FArchive& Ar = Record.GetUnderlyingArchive();
    int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
    FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

    for (int32 I = 0; I < Num; ++I)
    {
        FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
        FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
        FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
        BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

        if (Ar.IsLoading())
        {
            // 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
            const int32 Index = FindBoneIndex(Name);
            if (Index >= 0)
                BoneSpaceTransforms[Index] = Local;
        }
    }

    if (Ar.IsLoading())
        RefreshBoneTransformsNow();   // 에디터 월드에서 Tick이 안 돌아도 포즈가 바로 반영된다
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
    delete std::exchange(AnimScriptInstance, nullptr);
    BoneSpaceTransforms.Reset();
    for (const FBoneInfo& Bone : GetBones())
        BoneSpaceTransforms.Add(Bone.LocalBindPose);
}
