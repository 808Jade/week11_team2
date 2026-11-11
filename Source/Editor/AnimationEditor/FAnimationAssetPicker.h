#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"

enum class EAssetType;

// 콘텐츠 드로어와 각 편집기가 같은 에셋 타입별 드래그 페이로드를 사용한다.
inline constexpr char AnimSequencePayload[]{"WEEK11_ANIM_SEQUENCE"};
// Blend Space 선택 슬롯으로 파일 경로를 전달할 때 사용하는 페이로드 타입이다.
inline constexpr char BlendSpacePayload[]{"WEEK11_BLEND_SPACE"};
// 그래프 선택 슬롯으로 파일 경로를 전달할 때 사용하는 페이로드 타입이다.
inline constexpr char AnimGraphPayload[]{"WEEK11_ANIM_GRAPH"};

// 목록 선택 또는 드래그로 경로가 바뀐 경우 true를 반환한다.
bool DrawAnimationAssetPicker(const char* Label, FString& Path, EAssetType Type, const char* Preview = nullptr);
