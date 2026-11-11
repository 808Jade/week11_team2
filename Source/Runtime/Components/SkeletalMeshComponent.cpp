#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "GameFramework/Actor.h"
#include "Script/LuaScriptInstance.h"
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
        // Details에서 재생 중에 바꿔도 바로 반영되도록 매 Tick 넘긴다.
        if (auto* SingleNode = Cast<UAnimSingleNodeInstance>(AnimScriptInstance))
            SingleNode->SetRootMotionEnabled(bEnableRootMotion);
        AnimScriptInstance->UpdateAnimation(DeltaTime);
        ApplyRootMotion(AnimScriptInstance->GetRootMotionDelta());
        if (AnimScriptInstance->EvaluateAnimation(BoneSpaceTransforms))
            MarkBoneTransformsDirty();
        DispatchAnimNotifies();
    }
    Super::TickComponent(DeltaTime);
}

void USkeletalMeshComponent::DispatchAnimNotifies()
{
    if (!AnimScriptInstance) return;

    // 받는 쪽(Lua)이 PlayAnimation·메시 교체로 인스턴스를 바꿔도 안전하도록 목록을 먼저 꺼냅니다.
    const TArray<FAnimNotifyDispatch> Notifies = AnimScriptInstance->TakePendingNotifies();
    for (const FAnimNotifyDispatch& Notify : Notifies)
    {
        OnAnimNotify.Broadcast(this, Notify.Event, Notify.Type);

        AActor* Owner = GetOwner();
        if (!Owner || !Owner->Script) continue;
        switch (Notify.Type)
        {
        case EAnimNotifyEventType::Notify:     Owner->Script->Call("OnAnimNotify", Notify.Event.NotifyName); break;
        case EAnimNotifyEventType::StateBegin: Owner->Script->Call("OnAnimNotifyBegin", Notify.Event.NotifyName); break;
        case EAnimNotifyEventType::StateEnd:   Owner->Script->Call("OnAnimNotifyEnd", Notify.Event.NotifyName); break;
        case EAnimNotifyEventType::StateTick:  break;
        }
    }
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
    SingleNode->SetRootMotionEnabled(bEnableRootMotion);
    SingleNode->PlayAnim();
    SingleNode->EvaluateAnimation(BoneSpaceTransforms);
    RefreshBoneTransformsNow();
    return true;
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
    return Cast<UAnimSingleNodeInstance>(AnimScriptInstance);
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

void USkeletalMeshComponent::ApplyRootMotion(const FRootMotionDelta& Delta)
{
    const FVector& T = Delta.Translation;
    const FQuat& R = Delta.Rotation;
    const bool bHasTranslation = T.X != 0.0f || T.Y != 0.0f || T.Z != 0.0f;
    // 단위 쿼터니언의 XYZ 길이는 sin(각도/2)이다. 1e-10 기준이면 약 0.001도 미만의 회전은 무시한다
    const bool bHasRotation = R.X * R.X + R.Y * R.Y + R.Z * R.Z > 1.e-10f;
    if (!bHasTranslation && !bHasRotation) return;

    AActor* Owner = GetOwner();
    USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
    if (!Root) return;

    // 이동 먼저: 이동량은 이번 프레임에 돌기 전의 방향 기준이다 (TransformVector는 w=0이라 이동 성분 제외)
    if (bHasTranslation)
    {
        const FVector WorldDelta = GetWorldMatrix().TransformVector(T);
        Root->SetWorldLocation(Root->GetWorldLocation() + WorldDelta, false);
    }

    // 컴포넌트 공간 회전을 월드 공간 회전으로 바꾼 뒤(C * R * C⁻¹) Actor 회전 앞에 곱한다
    if (bHasRotation)
    {
        const FQuat ComponentRot = GetWorldRotation().Quaternion();
        const FQuat WorldDeltaRot = ComponentRot * R * ComponentRot.UnitInverse();
        const FQuat NewRootRot = WorldDeltaRot * Root->GetWorldRotation().Quaternion();
        Root->SetWorldRotation(NewRootRot.ToFRotator(), false);
    }
}