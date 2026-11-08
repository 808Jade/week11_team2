#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Math/Quat.h"
#include "Math/Vector.h"

struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;

	bool HasValidKeys(int32 NumberOfKeys) const;
};

struct FBoneAnimationTrack
{
	FString Name;
	int32 BoneTreeIndex = INDEX_NONE;
	FRawAnimSequenceTrack InternalTrackData;
};
