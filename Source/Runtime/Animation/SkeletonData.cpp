#include "EnginePCH.h"
#include "SkeletonData.h"

FSkeletonData FSkeletonData::FromMeshBones(const TArray<FBoneInfo>& MeshBones)
{
	FSkeletonData Data;
	Data.Bones.SetNum(MeshBones.Num());
	for (int32 I = 0; I < MeshBones.Num(); ++I)
	{
		Data.Bones[I].Name = MeshBones[I].Name;
		Data.Bones[I].ParentIndex = MeshBones[I].ParentIndex;
		Data.Bones[I].RefPose = MeshBones[I].LocalBindPose;
	}
	return Data;
}

int32 FSkeletonData::FindBoneIndex(const FString& Name) const
{
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		if (Bones[I].Name == Name)
			return I;
	}
	return -1;
}

bool FSkeletonData::HasSameHierarchy(const TArray<FBoneInfo>& MeshBones) const
{
	if (Bones.Num() != MeshBones.Num()) return false;
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		if (Bones[I].Name != MeshBones[I].Name || Bones[I].ParentIndex != MeshBones[I].ParentIndex)
			return false;
	}
	return true;
}

bool FSkeletonData::Validate(FString& OutError) const
{
	if (Bones.IsEmpty())
	{
		OutError = "Skeleton: no bones";
		return false;
	}
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		const FSkeletonBone& Bone = Bones[I];
		if (Bone.ParentIndex < -1 || Bone.ParentIndex >= I)
		{
			OutError = "Skeleton: invalid parent: " + Bone.Name;
			return false;
		}
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			if (!std::isfinite(Bone.RefPose.Location[Axis]) || !std::isfinite(Bone.RefPose.Rotation[Axis]) || !std::isfinite(Bone.RefPose.Scale[Axis]))
			{
				OutError = "Skeleton: non-finite pose: " + Bone.Name;
				return false;
			}
		}
	}
	return true;
}
