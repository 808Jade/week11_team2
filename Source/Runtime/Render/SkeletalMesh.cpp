#include "EnginePCH.h"
#include "SkeletalMesh.h"
#include "Render/RenderCommand.h"
#include "Asset/AssetManager.h"

namespace
{
	void AccumulateBoneBounds(const FSkeletalMeshLODRenderData& LOD, TArray<FBox>& Bounds)
	{
		for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
		{
			const FVector& Position = LOD.BindVertices[V].Position;
			const FSkinWeight& Weight = LOD.SkinWeights[V];
			for (int32 K = 0; K < MaxBoneInfluences; ++K)
			{
				if (Weight.Weight[K] <= 0.0f) continue;
				Bounds[Weight.BoneIndex[K]].Expand(FBox{ Position, Position });
			}
		}
	}
}

bool USkeletalMesh::Build(FSkeletalMeshData&& ImportData, FString& OutError)
{
	TArray<FSkeletalMeshData> LODs;
	LODs.Add(std::move(ImportData));
	return Build(std::move(LODs), {}, { .30f, .10f, .03f }, OutError);
}

bool USkeletalMesh::Build(TArray<FSkeletalMeshData>&& LODs, TArray<UMaterial*>&& InMaterials,
	const std::array<float, 3>& InScreenThresholds, FString& OutError)
{
	if (LODs.IsEmpty() || LODs.Num() > 4)
	{
		OutError = "SkeletalMesh: expected 1..4 LODs";
		return false;
	}

	auto& Base = LODs[0];
	if (Base.MaterialSlots.IsEmpty()) Base.MaterialSlots.Add({ "Default" });
	for (auto& ImportData : LODs)
	{
		if (ImportData.MaterialSlots.IsEmpty()) ImportData.MaterialSlots = Base.MaterialSlots;
		if (ImportData.Sections.IsEmpty())
			ImportData.Sections.Add({ 0, static_cast<uint32>(ImportData.Indices.Num()), 0 });

		if (ImportData.Bones.Num() != Base.Bones.Num() || ImportData.MaterialSlots.Num() != Base.MaterialSlots.Num())
		{
			OutError = "SkeletalMesh: LODs must share bones and material slots";
			return false;
		}
		if (!ImportData.Validate(OutError)) return false;
	}

	if (InMaterials.IsEmpty() && !UAssetManager::BuildMaterialsFromSlots(Base.MaterialSlots, InMaterials))
	{
		OutError = "SkeletalMesh: material creation failed";
		return false;
	}
	if (InMaterials.Num() != Base.MaterialSlots.Num())
	{
		OutError = "SkeletalMesh: material count does not match slots";
		return false;
	}

	FSkeletalMeshRenderData NewRenderData;
	NewRenderData.BoneBounds.SetNum(Base.Bones.Num());
	for (FBox& Box : NewRenderData.BoneBounds)
		Box = FBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };

	for (auto& ImportData : LODs)
	{
		auto& LOD = NewRenderData.LODRenderData.AddDefaulted_GetRef();
		LOD.BindVertices = std::move(ImportData.BindVertices);
		LOD.SkinWeights = std::move(ImportData.SkinWeight);
		LOD.Indices = std::move(ImportData.Indices);
		LOD.Sections = std::move(ImportData.Sections);
		LOD.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(LOD.Indices.GetData(), static_cast<uint32>(LOD.Indices.Num()));
		if (!LOD.IndexBuffer)
		{
			OutError = "SkeletalMesh: index buffer creation failed";
			return false;
		}

		LOD.BindAABB = FBox{ LOD.BindVertices[0].Position, LOD.BindVertices[0].Position };
		TArray<FGpuSkinVertex> GpuSkinVertices;
		GpuSkinVertices.SetNum(LOD.BindVertices.Num());
		for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
		{
			const auto& Vertex = LOD.BindVertices[V];
			LOD.BindAABB.Expand(FBox{ Vertex.Position, Vertex.Position });
			GpuSkinVertices[V].Vertex = Vertex;
			for (int32 K = 0; K < MaxBoneInfluences; ++K)
			{
				GpuSkinVertices[V].BoneIndex[K] = LOD.SkinWeights[V].BoneIndex[K];
				GpuSkinVertices[V].Weight[K] = LOD.SkinWeights[V].Weight[K];
			}
		}
		LOD.GpuSkinVertexBuffer = RenderCommand::CreateStaticVertexBuffer(GpuSkinVertices.GetData(),
			sizeof(FGpuSkinVertex) * static_cast<uint32>(GpuSkinVertices.Num()), sizeof(FGpuSkinVertex));
		AccumulateBoneBounds(LOD, NewRenderData.BoneBounds);
		if (NewRenderData.LODRenderData.Num() == 1) NewRenderData.BindBounds = LOD.BindAABB;
		else NewRenderData.BindBounds.Expand(LOD.BindAABB);
	}

	RefSkeleton = std::move(Base.Bones);
	MaterialSlots = std::move(Base.MaterialSlots);
	Materials = std::move(InMaterials);
	ScreenThresholds = InScreenThresholds;
	RenderData = std::move(NewRenderData);
	RenderStates.Reset();
	return true;
}
int32 USkeletalMesh::FindBoneIndex(const FString& Name) const
{
	for (int32 I = 0; I < RefSkeleton.Num(); ++I)
	{
		if (RefSkeleton[I].Name == Name)
			return I;
	}
	return -1;
}

UMaterial* USkeletalMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex])
	{
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t USkeletalMesh::FMaterialSlotsHash::operator()(const TArray<UMaterial*>& Slots) const
{
	size_t Hash = 0;
	for (const UMaterial* Material : Slots)
		Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
	return Hash;
}

TSharedPtr<const FMeshRenderState> USkeletalMesh::GetRenderState(const TArray<UMaterial*>& InMaterials)
{
	auto& WeakState = RenderStates[InMaterials];
	if (auto State = WeakState.lock()) return State;

	auto State = MakeShared<FMeshRenderState>();
	State->LODCount = static_cast<uint8>(RenderData.LODRenderData.Num());
	for (int32 I = 0; I < 3; ++I)
		State->LODThresholdSq[I] = ScreenThresholds[I] * ScreenThresholds[I];
	for (int32 LOD = 0; LOD < State->LODCount; ++LOD)
	{
		auto& Range = State->LODs[LOD];
		Range.FirstSection = State->Sections.Num();
		for (const FSkeletalMeshSection& Section : RenderData.LODRenderData[LOD].Sections)
		{
			UMaterial* Material = Section.MaterialSlotIndex < InMaterials.size()
				? InMaterials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
			if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
		}
		Range.NumSections = State->Sections.Num() - Range.FirstSection;
	}
	WeakState = State;
	return State;
}
