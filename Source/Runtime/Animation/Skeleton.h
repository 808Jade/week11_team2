#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/ReferenceSkeleton.h"
#include "Skeleton.generated.h"

class USkeletalMesh;

struct FSkeletonToMeshLinkup
{
	TArray<int32> SkeletonToMeshTable;
	TArray<int32> MeshToSkeletonTable;
};

UCLASS()
class USkeleton : public URenderAsset
{
	GENERATED_BODY()
public:
	FReferenceSkeleton ReferenceSkeleton;
	const FReferenceSkeleton& GetReferenceSkeleton() const { return ReferenceSkeleton; }
	bool BuildLinkup(const USkeletalMesh& Mesh, FSkeletonToMeshLinkup& OutLinkup, FString& OutError) const;
};
