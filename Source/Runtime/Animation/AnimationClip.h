#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/AnimationClipData.h"

#include "AnimationClip.generated.h"

class USkeleton;

UCLASS()
class UAnimationClip : public URenderAsset
{
	GENERATED_BODY()
public:
	void SetData(FAnimationClipData&& InData) { Data = std::move(InData); }
	const FAnimationClipData& GetData() const { return Data; }

	void SetSkeleton(USkeleton* InSkeleton) { Skeleton = InSkeleton; }
	// 이 클립을 만든 뼈대입니다. 예전 클립은 nullptr일 수 있습니다.
	USkeleton* GetSkeleton() const { return Skeleton; }

private:
	FAnimationClipData Data;
	USkeleton* Skeleton = nullptr;
};