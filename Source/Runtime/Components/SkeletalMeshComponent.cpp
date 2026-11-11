#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Serialization/MathSerialization.h"
#include "GameFramework/Actor.h"
#include "Script/LuaScriptInstance.h"
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
    // 씬 로드와 메시 변경 시 저장된 경로로 재생 인스턴스를 다시 만든다.
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

    // 아직 메시가 없으면 경로만 보관하고 등록 또는 메시 설정 시 초기화를 완료한다.
    if (!GetSkeletalMesh()) {
        mAnimGraphPath = Instance->GetAnimGraph();
        return true;
    }
    if (!Instance->InitializeAnimation(OutError)) {
        return false;
    }

    // 새 인스턴스 초기화가 성공한 뒤에만 기존 재생기를 교체한다.
    mAnimGraphPath = Instance->GetAnimGraph();
    delete std::exchange(mAnimScriptInstance, Instance.release());
    // 에디터에서 Tick을 기다리지 않고 연결 직후의 초기 포즈를 메시와 렌더링에 반영한다.
    mAnimScriptInstance->SetRootMotionEnabled(bEnableRootMotion);
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

bool USkeletalMeshComponent::SetAnimGraphBool(const FString& Name, bool Value) {
    auto* Instance{Cast<UAnimGraphInstance>(mAnimScriptInstance)};
    return Instance && Instance->SetBoolVariable(Name, Value);
}

bool USkeletalMeshComponent::RestartAnimGraphPlayer(const FString& Name) {
    auto* Instance{Cast<UAnimGraphInstance>(mAnimScriptInstance)};
    auto* Player{Instance ? Instance->FindAssetPlayer(Name) : nullptr};
    if (!Player) {
        return false;
    }

    Player->Initialize(*Instance);
    return true;
}

bool USkeletalMeshComponent::IsAnimGraphPlayerFinished(const FString& Name) const {
    const auto* Instance{Cast<UAnimGraphInstance>(mAnimScriptInstance)};
    const auto* Player{Instance ? Instance->FindAssetPlayer(Name) : nullptr};
    return Player && Player->IsFinished();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime) {
    if (mAnimScriptInstance != nullptr) {
        mAnimScriptInstance->SetRootMotionEnabled(bEnableRootMotion);
        mAnimScriptInstance->UpdateAnimation(DeltaTime);
        ApplyRootMotion(mAnimScriptInstance->GetRootMotionDelta());

        if (mAnimScriptInstance->EvaluateAnimation(mBoneSpaceTransforms)) {
            MarkBoneTransformsDirty();
        }

        DispatchAnimNotifies();
    }

    Super::TickComponent(DeltaTime);
}

bool USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool Looping, float PlayRate, FString& OutError) {
    OutError.clear();

    if (GetSkeletalMesh() == nullptr || NewAnimToPlay == nullptr) {
        OutError = "Set a skeletal mesh and animation asset first";
        return false;
    }

    auto* SingleNode{Cast<UAnimSingleNodeInstance>(mAnimScriptInstance)};
    std::unique_ptr<UAnimSingleNodeInstance> NewInstance{};

    if (SingleNode == nullptr) {
        NewInstance.reset(NewObject<UAnimSingleNodeInstance>(this, NAME_None, EObjectFlags::RF_Transient));

        if (NewInstance == nullptr) {
            OutError = "Could not create animation instance";
            return false;
        }

        if (!NewInstance->InitializeAnimation(OutError)) {
            return false;
        }

        SingleNode = NewInstance.get();
    }

    if (!SingleNode->SetAnimationAsset(NewAnimToPlay, Looping, PlayRate, OutError)) {
        return false;
    }

    if (NewInstance != nullptr) {
        delete std::exchange(mAnimScriptInstance, NewInstance.release());
    }

    SingleNode->SetRootMotionEnabled(bEnableRootMotion);
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
    // 실행 포인터 대신 에셋 경로만 저장하고 로드 후에는 메시를 기준으로 인스턴스를 재생성한다.
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

void USkeletalMeshComponent::BeginPlay() {
    Super::BeginPlay();
    if (!mAnimGraphPath.empty() || !AnimToPlay) return;

    FString Error{};
    if (!PlayAnimation(AnimToPlay, bLoopAnimation, AnimPlayRate, Error))
        HTR_LOG(Warning, "AnimToPlay '{}' could not play on {}: {}", AnimToPlay->GetPath(), GetName(), Error);
}

void USkeletalMeshComponent::DispatchAnimNotifies() {
    if (!mAnimScriptInstance) return;

    const TArray<FAnimNotifyDispatch> Notifies{mAnimScriptInstance->TakePendingNotifies()};
    for (const FAnimNotifyDispatch& Notify : Notifies) {
        OnAnimNotify.Broadcast(this, Notify.Event, Notify.Type);

        AActor* Owner{GetOwner()};
        if (!Owner || !Owner->Script) continue;
        switch (Notify.Type) {
        case EAnimNotifyEventType::Notify:     Owner->Script->Call("OnAnimNotify", Notify.Event.NotifyName); break;
        case EAnimNotifyEventType::StateBegin: Owner->Script->Call("OnAnimNotifyBegin", Notify.Event.NotifyName); break;
        case EAnimNotifyEventType::StateEnd:   Owner->Script->Call("OnAnimNotifyEnd", Notify.Event.NotifyName); break;
        case EAnimNotifyEventType::StateTick:  break;
        }
    }
}

void USkeletalMeshComponent::ApplyRootMotion(const FRootMotionDelta& Delta) {
    const FVector& T{Delta.mTranslation};
    const FQuat& R{Delta.mRotation};
    const bool HasTranslation{T.X != 0.0f || T.Y != 0.0f || T.Z != 0.0f};
    const bool HasRotation{R.X * R.X + R.Y * R.Y + R.Z * R.Z > 1.e-10f};
    if (!HasTranslation && !HasRotation) return;

    AActor* Owner{GetOwner()};
    USceneComponent* Root{Owner ? Owner->GetRootComponent() : nullptr};
    if (!Root) return;

    if (HasTranslation) {
        const FVector WorldDelta{GetWorldMatrix().TransformVector(T)};
        Root->SetWorldLocation(Root->GetWorldLocation() + WorldDelta, false);
    }

    if (HasRotation) {
        const FQuat ComponentRot{GetWorldRotation().Quaternion()};
        const FQuat WorldDeltaRot{ComponentRot * R * ComponentRot.UnitInverse()};
        const FQuat NewRootRot{WorldDeltaRot * Root->GetWorldRotation().Quaternion()};
        Root->SetWorldRotation(NewRootRot.ToFRotator(), false);
    }
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const {
    return Cast<UAnimSingleNodeInstance>(mAnimScriptInstance);
}
