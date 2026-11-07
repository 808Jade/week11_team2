#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/AnimationClipData.h"

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

TArray<int32> USkeletalMeshComponent::MapAnimationTracks(const FAnimationClipData& Clip) const
{
    TArray<int32> TrackToBone;
    for (const FBoneAnimationTrack& Track : Clip.Tracks)
        TrackToBone.Add(FindBoneIndex(Track.BoneName));
    return TrackToBone;
}

void USkeletalMeshComponent::ApplyAnimationPose(const FAnimationClipData& Clip, const TArray<int32>& TrackToBone, float Time)
{
    if (Clip.NumFrames < 1 || TrackToBone.Num() != Clip.Tracks.Num()) return;

    const float FrameTime = FMath::Clamp(Time * Clip.SampleRate, 0.0f, static_cast<float>(Clip.NumFrames - 1));
    const int32 Frame0 = static_cast<int32>(FrameTime);
    const int32 Frame1 = FMath::Min(Frame0 + 1, Clip.NumFrames - 1);
    const float Alpha = FrameTime - static_cast<float>(Frame0);

    for (int32 I = 0; I < Clip.Tracks.Num(); ++I)
    {
        if (TrackToBone[I] < 0) continue;

        const FBoneAnimationTrack& Track = Clip.Tracks[I];
        FTransform Local;
        Local.Location = Track.Positions[Frame0] + (Track.Positions[Frame1] - Track.Positions[Frame0]) * Alpha;
        Local.Scale = Track.Scales[Frame0] + (Track.Scales[Frame1] - Track.Scales[Frame0]) * Alpha;
        Local.Rotation = FMath::Slerp(Track.Rotations[Frame0], Track.Rotations[Frame1], Alpha).ToFRotator();
        SetBoneLocalTransform(TrackToBone[I], Local);
    }
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
    BoneSpaceTransforms.Reset();
    for (const FBoneInfo& Bone : GetBones())
        BoneSpaceTransforms.Add(Bone.LocalBindPose);
}
