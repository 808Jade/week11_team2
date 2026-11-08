#pragma once

#include "Asset/RenderAsset.h"
#include "AnimationAsset.generated.h"

class USkeleton;

struct FAnimExtractContext
{
	double CurrentTime = 0.0;
};

UCLASS()
class UAnimationAsset : public URenderAsset
{
	GENERATED_BODY()

public:
	const USkeleton* GetSkeleton() const { return Skeleton; }
	virtual float GetPlayLength() const { return 0.0f; }

protected:
	void SetSkeleton(const USkeleton* InSkeleton) { Skeleton = InSkeleton; }

private:
	const USkeleton* Skeleton = nullptr;
};
