#include "EnginePCH.h"
#include "Animation/Skeleton.h"

#include <utility>

const FString& USkeleton::GetPath() const {
	return mPath;
}

const TArray<FBoneInfo>& USkeleton::GetRefSkeleton() const {
	return mRefSkeleton;
}

void USkeleton::Initialize(const FString& Path, TArray<FBoneInfo> Bones) {
	mPath = Path;
	mRefSkeleton = std::move(Bones);
}
