#pragma once

#include "Render/SkeletalMeshData.h"
#include "UObject/Object.h"
#include "Skeleton.generated.h"

class UAssetManager;

UCLASS()
class USkeleton : public UObject {
	GENERATED_BODY()

	friend class UAssetManager;

public:
	const FString& GetPath() const;
	const TArray<FBoneInfo>& GetRefSkeleton() const;

private:
	void Initialize(const FString& Path, TArray<FBoneInfo> Bones);

private:
	FString mPath{};
	TArray<FBoneInfo> mRefSkeleton{};
};
