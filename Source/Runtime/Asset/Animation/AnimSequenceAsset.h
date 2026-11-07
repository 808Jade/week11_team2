#pragma once

#include "Core/EngineString.h"

class UAnimSequence;

class FAnimSequenceAsset {
public:
	static bool IsAssetPath(const FString& Path);
	static bool Read(const FString& Path, UAnimSequence& OutSequence, FString& OutError);
	static bool Write(const FString& Path, const UAnimSequence& Sequence, FString& OutError);
};
