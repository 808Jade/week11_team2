#include "EnginePCH.h"
#include "Animation/Skeleton.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <utility>

bool USkeleton::BuildLinkup(const USkeletalMesh& Mesh, FSkeletonToMeshLinkup& OutLinkup,
	FString& OutError) const
{
	if (!ReferenceSkeleton.Validate(OutError)) return false;
	const auto& MeshBones = Mesh.GetRefSkeleton();
	if (MeshBones.IsEmpty())
	{
		OutError = "Skeletal mesh has no bones";
		return false;
	}

	FSkeletonToMeshLinkup Linkup;
	Linkup.SkeletonToMeshTable.SetNum(ReferenceSkeleton.GetNum());
	Linkup.MeshToSkeletonTable.SetNum(MeshBones.Num());
	std::fill(Linkup.SkeletonToMeshTable.begin(), Linkup.SkeletonToMeshTable.end(), INDEX_NONE);

	TMap<FString, int32> SkeletonByName;
	for (int32 I = 0; I < ReferenceSkeleton.GetNum(); ++I)
		SkeletonByName.Add(ReferenceSkeleton.RefBoneInfo[I].Name, I);

	for (int32 MeshIndex = 0; MeshIndex < MeshBones.Num(); ++MeshIndex)
	{
		const FBoneInfo& Bone = MeshBones[MeshIndex];
		const int32* SkeletonIndex = SkeletonByName.Find(Bone.Name);
		if (!SkeletonIndex || Linkup.SkeletonToMeshTable[*SkeletonIndex] != INDEX_NONE
			|| Bone.ParentIndex < INDEX_NONE || Bone.ParentIndex >= MeshIndex)
		{
			OutError = "Invalid mesh bone mapping: " + Bone.Name;
			return false;
		}

		const int32 SkeletonParent = Bone.ParentIndex == INDEX_NONE
			? INDEX_NONE : Linkup.MeshToSkeletonTable[Bone.ParentIndex];
		if (ReferenceSkeleton.RefBoneInfo[*SkeletonIndex].ParentIndex != SkeletonParent)
		{
			OutError = "Different parent for mesh bone: " + Bone.Name;
			return false;
		}
		Linkup.SkeletonToMeshTable[*SkeletonIndex] = MeshIndex;
		Linkup.MeshToSkeletonTable[MeshIndex] = *SkeletonIndex;
	}

	OutLinkup = std::move(Linkup);
	return true;
}
