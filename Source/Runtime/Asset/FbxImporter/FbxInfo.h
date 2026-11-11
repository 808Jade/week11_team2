#pragma once

#include "Animation/ReferenceSkeleton.h"
#include "Animation/AnimTrackTypes.h"
#include "Render/StaticMeshData.h"
#include "Render/ImageLoader.h"
#include "Render/RenderEnums.h"
#include "Render/RenderStates.h"
#include <array>

enum class EFbxAssetType : uint32
{
	StaticMesh = 1,
	SkeletalMesh,
	Skeleton,
	Material,
	Texture2D,
	AnimSequence,
};

enum class EFbxAnimationRange : uint8 { Timeline, Animated, SetRange };
enum class EFbxFrameAlignment : uint8 { None, Floor, Closest, Ceiling };

struct FFbxImportOptions
{
	bool bBakeStaticMeshes = true;
	bool bCombineStaticMeshes = false;
	bool bBakeSkeletalMeshes = true;
	bool bCombineSkeletalMeshes = true;
	bool bConvertStaticsInBoneHierarchyToSkeletals = true;
	bool bConvertStaticsWithAnimatedTransformToSkeletals = true;
	bool bImportAnimations = true;
	bool bImportOnlyAnimations = false;
	FString SkeletalMeshSkeletonKey;
	FString AnimationSkeletonKey;
	EFbxAnimationRange AnimationRange = EFbxAnimationRange::Timeline;
	EFbxFrameAlignment FrameAlignment = EFbxFrameAlignment::None;
	int32 CustomBoneAnimationSampleRate = 0;
	int32 FirstAnimationFrame = 0;
	int32 LastAnimationFrame = 0;
	// 다른 뼈대 구조의 애니메이션을 선택한 Skeleton으로 Humanoid 리타깃한다(임포트 시 베이크, 저장되지 않는 옵션).
	bool bRetargetHumanoid = false;
	// Humanoid 리타깃 때 골반의 바닥 평면 이동을 선택한 Skeleton의 root 본(0번) 트랙으로 옮긴다(저장되지 않는 옵션).
	// USkeletalMeshComponent::bEnableRootMotion을 켜면 그 이동이 Actor로 넘어간다. 끄면 예전과 똑같이 보인다.
	bool bRetargetRootMotion = false;
};

struct FFbxAnimationData
{
	static constexpr int32 MaxKeys = 1000000;
	static constexpr uint64 MaxChannelKeys = 16 * 1024 * 1024;
	double StartTime = 0.0;
	float FrameRate = 0.0f;
	int32 NumberOfKeys = 0;
	TArray<FBoneAnimationTrack> BoneAnimationTracks;
};

struct FFbxMaterialData
{
	FVector4 DiffuseOrFactor{ 1, 1, 1, 1 }; // 선형 RGB: Diffuse 맵이 없으면 Diffuse × DiffuseFactor, 있으면 DiffuseFactor를 각 채널에 저장한다. W는 불투명도.
	float Shininess = 32.f;
	FVector SpecularColor = FVector(1, 1, 1); // 선형 반사광 색상
	float SpecularFactor = 1.f; // 반사광 강도 계수
	FVector EmissiveColor = FVector(0, 0, 0); // 선형 자체 발광 색상
	float EmissiveFactor = 1.f; // 자체 발광 강도 계수
	ELightingModel ShadingModel = ELightingModel::BlinnPhong;
	ESamplerState SamplerState = ESamplerState::LinearClamp;
	EBlendState BlendState = EBlendState::Opaque;
	FString DiffuseTexture;
	FString NormalTexture;
};

// Week10 .fbxbin stores 16-bit bone indices. Runtime vertex layout is assembled by the loader.
struct FSkinWeightData
{
	TArray<std::array<uint16, 4>> InfluenceBones;
	TArray<std::array<float, 4>> InfluenceWeights;
	bool Validate(int32 VertexCount, int32 BoneCount, FString& Error) const;
};

struct FFbxMeshLODData
{
	FStaticMeshData MeshData;
	FSkinWeightData SkinWeights;
};

// Cook 결과다. SDK 포인터/GPU 리소스는 포함하지 않는다. 한 항목이 한 .fbxbin이다.
struct FFbxAssetData
{
	EFbxAssetType Type = EFbxAssetType::StaticMesh;
	FString Key;
	FString SourcePath; // 출처 식별용: Assets 상대 경로 또는 외부 원본의 절대 경로. 로딩 의존성이 아니다.
	FString SourceObject;
	FFbxImportOptions Options;
	TArray<FFbxMeshLODData> LODs;
	TArray<FString> MaterialKeys;
	std::array<float, 3> ScreenThresholds{ .30f, .10f, .03f };
	FString SkeletonKey;
	FReferenceSkeleton RefSkeleton;
	FFbxMaterialData Material;
	FImageData Texture;
	FFbxAnimationData Animation;

	bool Validate(FString& Error) const;
};

struct FFbxImportResult
{
	bool bSuccess = false;
	FString Error;
	TArray<FString> Warnings;
	TArray<FString> AssetKeys;
};
