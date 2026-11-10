#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Serialization/MathSerialization.h"
#include <memory>
#include <utility>

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    delete std::exchange(mAnimScriptInstance, nullptr);
}

void USkeletalMeshComponent::OnRegister() {
    Super::OnRegister();
    RestoreAnimGraph();
}

void USkeletalMeshComponent::RestoreAnimGraph() {
    if (mAnimScriptInstance || mAnimGraphPath.empty() || !GetSkeletalMesh()) {
        return;
    }

    FString Error{};
    if (!SetAnimGraph(mAnimGraphPath, Error)) {
        HTR_LOG(Warning, "Cannot restore animation graph {}. {}", mAnimGraphPath, Error);
    }
}

bool USkeletalMeshComponent::SetAnimGraph(const FString& Path, FString& OutError) {
    OutError.clear();
    if (Path.empty()) {
        mAnimGraphPath.clear();
        if (Cast<UAnimGraphInstance>(mAnimScriptInstance)) {
            delete std::exchange(mAnimScriptInstance, nullptr);
            ResetPose();
            RefreshBoneTransformsNow();
        }
        return true;
    }

    std::unique_ptr<UAnimGraphInstance> Instance{NewObject<UAnimGraphInstance>(this, NAME_None, EObjectFlags::RF_Transient)};
    if (!Instance || !Instance->SetAnimGraph(Path)) {
        OutError = "Could not load animation graph: " + Path;
        return false;
    }

    if (!GetSkeletalMesh()) {
        mAnimGraphPath = Instance->GetAnimGraph();
        return true;
    }
    if (!Instance->InitializeAnimation(OutError)) {
        return false;
    }

    mAnimGraphPath = Instance->GetAnimGraph();
    delete std::exchange(mAnimScriptInstance, Instance.release());
    mAnimScriptInstance->UpdateAnimation(0.0f);
    if (mAnimScriptInstance->EvaluateAnimation(mBoneSpaceTransforms)) {
        RefreshBoneTransformsNow();
    }
    return true;
}

const FString& USkeletalMeshComponent::GetAnimGraph() const {
    return mAnimGraphPath;
}

UAnimInstance* USkeletalMeshComponent::GetAnimInstance() const {
    return mAnimScriptInstance;
}

bool USkeletalMeshComponent::SetAnimGraphFloat(const FString& Name, float Value) {
    auto* Instance{Cast<UAnimGraphInstance>(mAnimScriptInstance)};
    return Instance && Instance->SetFloatVariable(Name, Value);
}

bool USkeletalMeshComponent::RestartAnimGraphPlayer(int32 NodeId) {
    auto* Instance{Cast<UAnimGraphInstance>(mAnimScriptInstance)};
    auto* Player{Instance ? Instance->FindAssetPlayer(NodeId) : nullptr};
    if (!Player) {
        return false;
    }

    Player->Initialize(*Instance);
    return true;
}

bool USkeletalMeshComponent::IsAnimGraphPlayerFinished(int32 NodeId) const {
    const auto* Instance{Cast<UAnimGraphInstance>(mAnimScriptInstance)};
    const auto* Player{Instance ? Instance->FindAssetPlayer(NodeId) : nullptr};
    return Player && Player->IsFinished();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    if (mAnimScriptInstance)
    {
        mAnimScriptInstance->UpdateAnimation(DeltaTime);
        if (mAnimScriptInstance->EvaluateAnimation(mBoneSpaceTransforms))
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

    auto* SingleNode = Cast<UAnimSingleNodeInstance>(mAnimScriptInstance);
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
        delete std::exchange(mAnimScriptInstance, NewInstance.release());
    SingleNode->PlayAnim();
    SingleNode->EvaluateAnimation(mBoneSpaceTransforms);
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
    if (BoneIndex < 0 || BoneIndex >= mBoneSpaceTransforms.Num()) return FTransform();
    return mBoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
    if (BoneIndex < 0 || BoneIndex >= mBoneSpaceTransforms.Num()) return;
    mBoneSpaceTransforms[BoneIndex] = Local;
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
    if (mBoneSpaceTransforms.Num() != Bones.Num())
        OnSkeletalMeshChanged();

    ComponentSpaceTransforms.SetNum(Bones.Num());
    for (int i = 0; i < Bones.Num(); ++i)
    {
        const FMatrix Local = mBoneSpaceTransforms[i].GetLocalMatrix();
        const int32 Parent = Bones[i].ParentIndex;
        ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
    }
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
    Super::Serialize(Record);   // 메시를 먼저 정해서 포즈를 바인드 포즈로 초기화한다

    FArchive& Ar = Record.GetUnderlyingArchive();
    if (auto Field{Record.TryEnterField("AnimGraph", !mAnimGraphPath.empty())}) {
        *Field << mAnimGraphPath;
    } else if (Ar.IsLoading()) {
        mAnimGraphPath.clear();
    }

    int32 Num = Ar.IsSaving() ? mBoneSpaceTransforms.Num() : 0;
    FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

    for (int32 I = 0; I < Num; ++I)
    {
        FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
        FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
        FTransform Local = Ar.IsSaving() ? mBoneSpaceTransforms[I] : FTransform();
        BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

        if (Ar.IsLoading())
        {
            // 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
            const int32 Index = FindBoneIndex(Name);
            if (Index >= 0)
                mBoneSpaceTransforms[Index] = Local;
        }
    }

    if (Ar.IsLoading()) {
        delete std::exchange(mAnimScriptInstance, nullptr);
        RefreshBoneTransformsNow();
        if (IsRegistered()) {
            RestoreAnimGraph();
        }
    }
}

void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
    delete std::exchange(mAnimScriptInstance, nullptr);
    mBoneSpaceTransforms.Reset();
    for (const FBoneInfo& Bone : GetBones())
        mBoneSpaceTransforms.Add(Bone.LocalBindPose);

    if (IsRegistered()) {
        RestoreAnimGraph();
    }
}
