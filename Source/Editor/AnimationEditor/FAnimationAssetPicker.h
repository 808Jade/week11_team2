#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"

enum class EAssetType;

inline constexpr char AnimSequencePayload[]{"WEEK11_ANIM_SEQUENCE"};
inline constexpr char BlendSpacePayload[]{"WEEK11_BLEND_SPACE"};
inline constexpr char AnimGraphPayload[]{"WEEK11_ANIM_GRAPH"};

bool DrawAnimationAssetPicker(const char* Label, FString& Path, EAssetType Type, const char* Preview = nullptr);
