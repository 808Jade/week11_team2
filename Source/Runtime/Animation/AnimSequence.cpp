#include "EnginePCH.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
	bool IsFiniteVector(const FVector& Value)
	{
		return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
	}

	bool IsValidRotation(const FQuat& Value)
	{
		const float LengthSquared = Value.X * Value.X + Value.Y * Value.Y
			+ Value.Z * Value.Z + Value.W * Value.W;
		return std::isfinite(LengthSquared) && LengthSquared > 1.e-8f;
	}

	bool HasValidKeyCount(int32 ChannelKeys, int32 NumberOfKeys)
	{
		return ChannelKeys == 1 || ChannelKeys == NumberOfKeys;
	}
}

bool FRawAnimSequenceTrack::HasValidKeys(int32 NumberOfKeys) const
{
	return HasValidKeyCount(PosKeys.Num(), NumberOfKeys)
		&& HasValidKeyCount(RotKeys.Num(), NumberOfKeys)
		&& HasValidKeyCount(ScaleKeys.Num(), NumberOfKeys)
		&& std::all_of(PosKeys.begin(), PosKeys.end(), IsFiniteVector)
		&& std::all_of(RotKeys.begin(), RotKeys.end(), IsValidRotation)
		&& std::all_of(ScaleKeys.begin(), ScaleKeys.end(), IsFiniteVector);
}

bool UAnimSequence::Initialize(const USkeleton* InSkeleton, int32 InNumberOfKeys, float InFrameRate,
	TArray<FBoneAnimationTrack> InTracks, FString& OutError)
{
	OutError.clear();
	if (GetDataModel())
	{
		OutError = "Animation sequence is already initialized";
		return false;
	}
	if (!InSkeleton || InNumberOfKeys < 1 || !std::isfinite(InFrameRate) || InFrameRate <= 0.0f
		|| !std::isfinite((InNumberOfKeys - 1) / InFrameRate) || InTracks.IsEmpty())
	{
		OutError = "Animation needs a skeleton, a valid frame rate and at least one key and track";
		return false;
	}
	const FReferenceSkeleton& ReferenceSkeleton = InSkeleton->GetReferenceSkeleton();
	if (!ReferenceSkeleton.Validate(OutError)) return false;

	TArray<uint8> SeenBones;
	SeenBones.SetNum(ReferenceSkeleton.GetNum());
	for (FBoneAnimationTrack& Track : InTracks)
	{
		const int32 Index = Track.BoneTreeIndex;
		FRawAnimSequenceTrack& Raw = Track.InternalTrackData;
		if (Index < 0 || Index >= SeenBones.Num() || SeenBones[Index] || !Raw.HasValidKeys(InNumberOfKeys))
		{
			OutError = "Invalid bone index, duplicate track or invalid channel keys";
			return false;
		}
		const FString& BoneName = ReferenceSkeleton.RefBoneInfo[Index].Name;
		if (!Track.Name.empty() && Track.Name != BoneName)
		{
			OutError = "Track name does not match its skeleton bone: " + Track.Name;
			return false;
		}
		SeenBones[Index] = 1;
		Track.Name = BoneName;
		for (FQuat& Rotation : Raw.RotKeys)
			Rotation = Rotation.Normalized();
	}

	auto* Model = NewObject<UAnimDataModel>(this, NAME_None, EObjectFlags::RF_Transient);
	if (!Model)
	{
		OutError = "Could not create animation data model";
		return false;
	}
	Model->BoneAnimationTracks = std::move(InTracks);
	Model->NumberOfKeys = InNumberOfKeys;
	Model->FrameRate = InFrameRate;
	SetDataModel(Model);
	SetSkeleton(InSkeleton);
	return true;
}

void UAnimSequence::GetAnimationPose(FAnimationPoseData& OutPoseData,
	const FAnimExtractContext& ExtractionContext) const
{
	OutPoseData.RequiredBones.FillWithRefPose(OutPoseData.Pose);
	const UAnimDataModel* Model = GetDataModel();
	if (!Model || GetSkeleton() != OutPoseData.RequiredBones.GetSkeletonAsset()) return;

	const double Time = std::isfinite(ExtractionContext.CurrentTime) ? ExtractionContext.CurrentTime : 0.0;
	const int32 LastKey = Model->GetNumberOfKeys() - 1;
	const double Frame = Time >= GetPlayLength() ? LastKey
		: std::clamp(Time * Model->GetFrameRate(), 0.0, static_cast<double>(LastKey));
	const int32 First = static_cast<int32>(Frame);
	const int32 Second = std::min(First + 1, LastKey);
	const float Alpha = static_cast<float>(Frame - First);

	for (const FBoneAnimationTrack& Track : Model->GetBoneAnimationTracks())
	{
		const int32 MeshIndex = OutPoseData.RequiredBones.GetMeshPoseIndexFromSkeletonPoseIndex(Track.BoneTreeIndex);
		if (MeshIndex == INDEX_NONE) continue;

		const auto& Raw = Track.InternalTrackData;
		FTransform& Bone = OutPoseData.Pose[MeshIndex];
		Bone.Location = Raw.PosKeys.Num() == 1 ? Raw.PosKeys[0]
			: FMath::Lerp(Raw.PosKeys[First], Raw.PosKeys[Second], Alpha);
		Bone.Rotation = (Raw.RotKeys.Num() == 1 ? Raw.RotKeys[0]
			: FMath::Slerp(Raw.RotKeys[First], Raw.RotKeys[Second], Alpha)).ToFRotator();
		Bone.Scale = Raw.ScaleKeys.Num() == 1 ? Raw.ScaleKeys[0]
			: FMath::Lerp(Raw.ScaleKeys[First], Raw.ScaleKeys[Second], Alpha);
	}

	if (ExtractionContext.bExtractRootMotion)
	{
		// root 이동·회전은 Actor로 넘기므로, Pose 안의 root는 첫 프레임 위치·회전에 고정한다
		const int32 RootIndex = OutPoseData.RequiredBones.GetMeshPoseIndexFromSkeletonPoseIndex(0);
		if (RootIndex != INDEX_NONE)
		{
			OutPoseData.Pose[RootIndex].Location = GetRootLocationAtTime(0.0);
			OutPoseData.Pose[RootIndex].Rotation = GetRootRotationAtTime(0.0).ToFRotator();
		}
	}
}

void UAnimSequence::GetKeyFrame(double Time, int32& OutFirst, int32& OutSecond, float& OutAlpha) const
{
	const UAnimDataModel* Model = GetDataModel();
	const int32 LastKey = Model->GetNumberOfKeys() - 1;
	const double Frame = Time >= GetPlayLength() ? LastKey : std::clamp(Time * Model->GetFrameRate(), 0.0, static_cast<double>(LastKey));
	OutFirst = static_cast<int32>(Frame);
	OutSecond = std::min(OutFirst + 1, LastKey);
	OutAlpha = static_cast<float>(Frame - OutFirst);
}

const FRawAnimSequenceTrack* UAnimSequence::FindRootTrack() const
{
	const UAnimDataModel* Model = GetDataModel();
	if (!Model) return nullptr;
	for (const FBoneAnimationTrack& Track : Model->GetBoneAnimationTracks())
		if (Track.BoneTreeIndex == 0)
			return &Track.InternalTrackData;
	return nullptr;
}

FVector UAnimSequence::GetRootLocationAtTime(double Time) const
{
	const FRawAnimSequenceTrack* Root = FindRootTrack();
	if (!Root || Root->PosKeys.IsEmpty()) return FVector(0, 0, 0);
	if (Root->PosKeys.Num() == 1) return Root->PosKeys[0];
	int32 First, Second;
	float Alpha;
	GetKeyFrame(Time, First, Second, Alpha);
	return FMath::Lerp(Root->PosKeys[First], Root->PosKeys[Second], Alpha);
}

FQuat UAnimSequence::GetRootRotationAtTime(double Time) const
{
	const FRawAnimSequenceTrack* Root = FindRootTrack();
	if (!Root || Root->RotKeys.IsEmpty()) return FQuat::Identity();
	if (Root->RotKeys.Num() == 1) return Root->RotKeys[0];
	int32 First, Second;
	float Alpha;
	GetKeyFrame(Time, First, Second, Alpha);
	return FMath::Slerp(Root->RotKeys[First], Root->RotKeys[Second], Alpha);
}