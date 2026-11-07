#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/SkeletonData.h"

#include "Skeleton.generated.h"

UCLASS()
class USkeleton : public URenderAsset
{
	GENERATED_BODY()
public:
	void SetData(FSkeletonData&& InData) { Data = std::move(InData); }
	const FSkeletonData& GetData() const { return Data; }

private:
	FSkeletonData Data;
};
