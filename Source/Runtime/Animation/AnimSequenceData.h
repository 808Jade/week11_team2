#pragma once

#include "Containers/Array.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

class FAnimBoneTrack {
public:
	FAnimBoneTrack() = default;
	FAnimBoneTrack(int32 InBoneIndex, TArray<FVector> InPositionKeys, TArray<FQuat> InRotationKeys, TArray<FVector> InScaleKeys);

public:
	int32 GetBoneIndex() const;

	const TArray<FVector>& GetPositionKeys() const;
	const TArray<FQuat>& GetRotationKeys() const;
	const TArray<FVector>& GetScaleKeys() const;

private:
	int32 mBoneIndex{ -1 };
	TArray<FVector> mPositionKeys{};
	TArray<FQuat> mRotationKeys{};
	TArray<FVector> mScaleKeys{};
};
