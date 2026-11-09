#pragma once

#include <cstdint>
#include <string_view>
#include "Core/Misc/Optional.h"

enum class EShaderType
{
	Vertex,
	Pixel,
	Compute,
};

enum class ERenderingPath
{
    Forward,
    Deferred
};

enum class ELightingModel
{
    Gouraud,
    Lambert,
    BlinnPhong,
    Unlit
};

inline constexpr const char* LightingModelNames[] = { "Gouraud", "Lambert", "BlinnPhong", "Unlit" };

inline TOptional<ELightingModel> ParseLightingModel(std::string_view Name)
{
    if (Name == "Phong") return ELightingModel::BlinnPhong;
    for (int Index = 0; Index <= static_cast<int>(ELightingModel::Unlit); ++Index)
        if (Name == LightingModelNames[Index]) return static_cast<ELightingModel>(Index);
    return {};
}

enum class ELightCullingMode
{
    Off = 0,
    Tiled = 1,
    Clustered = 2,
    Tiled25D = 3
};

enum class EPcfMode : uint32_t
{
    Off = 0,
    Sample8 = 1,
    Sample16 = 2,
    Sample32 = 3,
    PCSS = 4
};
