#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Transform.h"

class USkeleton;
class USkeletalMesh;

struct FBoneContainer
{
	bool InitializeTo(const USkeletalMesh& InMesh, FString& OutError);
	bool IsValid() const { return AssetSkeletalMesh != nullptr; }
	const USkeleton* GetSkeletonAsset() const;
	int32 GetMeshPoseIndexFromSkeletonPoseIndex(int32 SkeletonBoneIndex) const;
	void FillWithRefPose(TArray<FTransform>& OutPose) const;

private:
	const USkeletalMesh* AssetSkeletalMesh = nullptr;
	TArray<int32> SkeletonToPoseBoneIndexArray;
};
