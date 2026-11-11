#include "EnginePCH.h"
#include "Animation/BoneContainer.h"
#include "Animation/Skeleton.h"
#include "Render/SkeletalMesh.h"
#include <utility>

bool FBoneContainer::InitializeTo(const USkeletalMesh& InMesh, FString& OutError)
{
	const USkeleton* Skeleton = InMesh.GetSkeleton();
	if (!Skeleton)
	{
		OutError = "Skeletal mesh has no skeleton asset";
		return false;
	}
	FSkeletonToMeshLinkup Linkup;
	if (!Skeleton->BuildLinkup(InMesh, Linkup, OutError)) return false;
	SkeletonToPoseBoneIndexArray = std::move(Linkup.SkeletonToMeshTable);
	AssetSkeletalMesh = &InMesh;
	return true;
}

const USkeleton* FBoneContainer::GetSkeletonAsset() const
{
	return AssetSkeletalMesh ? AssetSkeletalMesh->GetSkeleton() : nullptr;
}

int32 FBoneContainer::GetMeshPoseIndexFromSkeletonPoseIndex(int32 SkeletonBoneIndex) const
{
	if (SkeletonBoneIndex < 0 || SkeletonBoneIndex >= SkeletonToPoseBoneIndexArray.Num())
		return INDEX_NONE;
	return SkeletonToPoseBoneIndexArray[SkeletonBoneIndex];
}

void FBoneContainer::FillWithRefPose(TArray<FTransform>& OutPose) const
{
	// 메시 없이 노드만 평가하는 경우(상태 머신 단위 테스트 등)는 빈 포즈로 시작한다.
	if (!AssetSkeletalMesh)
	{
		OutPose.Empty();
		return;
	}
	const auto& MeshBones = AssetSkeletalMesh->GetRefSkeleton();
	OutPose.SetNum(MeshBones.Num());
	for (int32 I = 0; I < MeshBones.Num(); ++I)
		OutPose[I] = MeshBones[I].LocalBindPose;
}
