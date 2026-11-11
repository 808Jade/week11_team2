// Resources/Shader/BoneWeightHeatmap.hlsli
// Bone Weight Heatmap을 켜면 CPU 스킨이 선택한 본의 Weight를 정점 Color.r에 기록한다.
#ifndef BONE_WEIGHT_HEATMAP_HLSLI
#define BONE_WEIGHT_HEATMAP_HLSLI

#ifndef GPU_SKIN
#define GPU_SKIN 0
#endif

#if GPU_SKIN
#define BONE_WEIGHT_HEATMAP_VS(Input, Flags) (-1.0f)
#else
#define BONE_WEIGHT_HEATMAP_VS(Input, Flags) ((Flags).y > 0.5f ? (Input).Color.r : -1.0f)
#endif

// 언리얼 Skin Weights 페인트 툴의 기본 램프(SkinWeightsPaintTool.cpp)와 같은 색이다.
float3 BoneWeightHeatmapColor(float Weight)
{
    static const float3 Ramp[7] =
    {
        float3(0.8f, 0.4f, 0.8f),
        float3(0.0f, 0.0f, 0.5f),
        float3(0.2f, 0.2f, 1.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(1.0f, 1.0f, 0.0f),
        float3(1.0f, 0.65f, 0.0f),
        float3(1.0f, 0.0f, 0.0f),
    };

    const float X = saturate(Weight) * 6.0f;
    const uint Index = min((uint)X, 5u);
    return lerp(Ramp[Index], Ramp[Index + 1], X - Index);
}

#endif
