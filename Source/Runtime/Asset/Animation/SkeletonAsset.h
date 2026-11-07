#pragma once

#include "Render/SkeletalMeshData.h"

class FSkeletonAsset {
public:
	static bool IsAssetPath(const FString& Path);
	static FString GetGeneratedPath(const TArray<FBoneInfo>& Bones);
	static bool HaveSameBones(const TArray<FBoneInfo>& A, const TArray<FBoneInfo>& B);

	static bool Read(const FString& Path, TArray<FBoneInfo>& OutBones, FString& OutError);
	static bool Write(const FString& Path, const TArray<FBoneInfo>& Bones, FString& OutError);
};
