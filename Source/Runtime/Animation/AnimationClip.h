#pragma once

#include "Asset/RenderAsset.h"
#include "Animation/AnimationClipData.h"

#include "AnimationClip.generated.h"

UCLASS()
class UAnimationClip : public URenderAsset
{
	GENERATED_BODY()
public:
	void SetData(FAnimationClipData&& InData) { Data = std::move(InData); }
	const FAnimationClipData& GetData() const { return Data; }

private:
	FAnimationClipData Data;
};