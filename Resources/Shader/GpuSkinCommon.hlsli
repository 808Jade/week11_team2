// Resources/Shader/GpuSkinCommon.hlsli
// 스켈레탈을 그리는 패스의 버텍스 셰이더가 공유한다. GPU_SKIN=1로 컴파일한 변형에서만 스키닝한다.
#ifndef GPU_SKIN_COMMON_HLSLI
#define GPU_SKIN_COMMON_HLSLI

#ifndef GPU_SKIN
#define GPU_SKIN 0
#endif

#if GPU_SKIN

// FGpuSkinVertex에서 FVertex 다음에 오는 필드. 리플렉션이 이 선언으로 레이아웃을 만든다
#define GPU_SKIN_VS_INPUT \
    uint4 BlendIndices : BLENDINDICES; \
    float4 BlendWeights : BLENDWEIGHT;

struct FGpuSkinBoneMatrix
{
    row_major float4x4 Value;
};

StructuredBuffer<FGpuSkinBoneMatrix> GpuSkinRefToLocal : register(t14);

float4x4 GpuSkinCalcBoneMatrix(uint4 BlendIndices, float4 BlendWeights)
{
    float4x4 BoneMatrix = BlendWeights.x * GpuSkinRefToLocal[BlendIndices.x].Value;
    BoneMatrix += BlendWeights.y * GpuSkinRefToLocal[BlendIndices.y].Value;
    BoneMatrix += BlendWeights.z * GpuSkinRefToLocal[BlendIndices.z].Value;
    BoneMatrix += BlendWeights.w * GpuSkinRefToLocal[BlendIndices.w].Value;
    return BoneMatrix;
}

float3 GpuSkinSafeNormalize(float3 V)
{
    const float LengthSquared = dot(V, V);
    return LengthSquared > 1.0e-12f ? V / sqrt(LengthSquared) : V;
}

// 비균등 스케일에서도 면에 수직이 되도록 3x3 부분의 여인수 행렬로 노멀을 옮긴다.
// 미러(행렬식 음수)면 여인수 결과가 뒤집히므로 부호를 되돌린다.
float3 GpuSkinTransformNormal(float4x4 M, float3 N)
{
    const float3 R0 = M[0].xyz;
    const float3 R1 = M[1].xyz;
    const float3 R2 = M[2].xyz;
    const float3 C0 = cross(R1, R2);
    const float3 Result = C0 * N.x + cross(R2, R0) * N.y + cross(R0, R1) * N.z;
    return GpuSkinSafeNormalize(dot(R0, C0) < 0.0f ? -Result : Result);
}

void GpuSkinVertex(uint4 BlendIndices, float4 BlendWeights, inout float3 Position, inout float3 Normal, inout float4 Tangent)
{
    const float4x4 M = GpuSkinCalcBoneMatrix(BlendIndices, BlendWeights);
    Position = mul(float4(Position, 1.0f), M).xyz;
    Normal = GpuSkinTransformNormal(M, Normal);
    Tangent.xyz = GpuSkinSafeNormalize(mul(float4(Tangent.xyz, 0.0f), M).xyz);
}

// 스키닝 결과를 로컬 정점처럼 넘긴다. 이후의 World 변환 코드는 그대로 쓴다
#define GPU_SKIN_APPLY(Input, Position, Normal, Tangent) GpuSkinVertex(Input.BlendIndices, Input.BlendWeights, Position, Normal, Tangent)

#else

#define GPU_SKIN_VS_INPUT
#define GPU_SKIN_APPLY(Input, Position, Normal, Tangent)

#endif

#endif
