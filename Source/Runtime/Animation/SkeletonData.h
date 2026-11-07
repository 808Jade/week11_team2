#pragma once

#include "Render/SkeletalMeshData.h"

struct FSkeletonBone
{
	FString Name;
	int32 ParentIndex = -1; // 부모가 항상 앞 인덱스
	FTransform RefPose;     // 부모 기준 기본 자세
};

// 메시와 클립이 함께 가리키는 공통 뼈대입니다. 메시마다 다른 값(가중치, InverseBindPose)은 갖지 않습니다.
struct FSkeletonData
{
	TArray<FSkeletonBone> Bones;

	static FSkeletonData FromMeshBones(const TArray<FBoneInfo>& MeshBones);

	int32 FindBoneIndex(const FString& Name) const;
	// 뼈 개수, 순서, 이름, 부모가 모두 같으면 같은 뼈대로 봅니다. 기본 자세는 비교하지 않습니다.
	bool HasSameHierarchy(const TArray<FBoneInfo>& MeshBones) const;
	bool Validate(FString& OutError) const;
};
