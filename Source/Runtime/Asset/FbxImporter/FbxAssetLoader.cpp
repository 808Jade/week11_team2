#include "EnginePCH.h"
#include "FbxAssetLoader.h"
#include "FbxAssetBake.h"
#include "FbxPath.h"
#include "Asset/AssetManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Render/Material.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Render/RenderCommand.h"
#include "Render/Texture2D.h"
#include <chrono>
#include <map>
#include <set>
#include <unordered_map>

namespace
{
bool PreservesReferenceBones(const FReferenceSkeleton& Current, const FReferenceSkeleton& Updated)
{
	if (Current.GetNum() > Updated.GetNum()) return false;
	for (int32 Index = 0; Index < Current.GetNum(); ++Index)
	{
		const auto& A = Current.RefBoneInfo[Index];
		const auto& B = Updated.RefBoneInfo[Index];
		if (A.Name != B.Name || A.ParentIndex != B.ParentIndex) return false;
		const FMatrix OldPose = Current.RefBonePose[Index].GetLocalMatrix();
		const FMatrix NewPose = Updated.RefBonePose[Index].GetLocalMatrix();
		for (int32 Row = 0; Row < 4; ++Row)
			for (int32 Column = 0; Column < 4; ++Column)
				if (OldPose.M[Row][Column] != NewPose.M[Row][Column]) return false;
	}
	return true;
}

// 아직 등록하지 않은 의존 애셋도 같은 배치에서 찾는다. 실패 시 기존 검색표는 그대로 유지된다.
class FLoadBatch
{
public:
	std::map<FString, FFbxAssetData> Data;
	std::map<FString, URenderAsset*> Objects;
	std::set<FString> Loading;
	std::vector<URenderAsset*> Created;
	FString Error;
	bool bPublished = false;
	bool bPrepared = false;
	TMap<FString, URenderAsset*> PreparedRegistry;
	bool bAllowDiskDependencies = true;
	FString SelectedMeshSkeletonKey;
	FString SelectedAnimationSkeletonKey;
	USkeleton* UpdatedSkeleton = nullptr;
	FReferenceSkeleton PreviousReferenceSkeleton;

	~FLoadBatch()
	{
		if (!bPublished)
		{
			for (auto It = Created.rbegin(); It != Created.rend(); ++It) delete *It;
			if (UpdatedSkeleton) UpdatedSkeleton->ReferenceSkeleton = std::move(PreviousReferenceSkeleton);
		}
	}

	template<class T> T* Allocate()
	{
		T* Object = NewObject<T>();
		if (Object) Created.push_back(Object);
		return Object;
	}

	URenderAsset* Resolve(const FString& Key)
	{
		if (auto It = Objects.find(Key); It != Objects.end()) return It->second;
		if (!Data.contains(Key))
		{
			if (Key == "DefaultMaterial")
			{
				auto* Default = Cast<UMaterial>(UAssetManager::FindLoadedAsset(Key));
				if (!Default) Error = "DefaultMaterial must be initialized before loading FBX meshes";
				return Default;
			}
			if (!bAllowDiskDependencies && Key != SelectedMeshSkeletonKey && Key != SelectedAnimationSkeletonKey)
			{ Error = "Missing imported dependency: " + Key; return nullptr; }
			// A lazy AssetManager load may already be on the stack. Never start a second one here.
			if (auto* Existing = UAssetManager::FindLoadedAsset(Key)) return Existing;
			fs::path Path;
			if (!FbxPath::ResolveKey(Key, Path, Error)) return nullptr;
			FFbxAssetData Asset;
			const FString FilePath = FbxPath::ToString(Path);
			if (!FFbxAssetBake::IsBakedPath(FilePath)) { Error = "Expected .fbxbin dependency: " + Key; return nullptr; }
			if (!FFbxAssetBake::ReadBaked(FilePath, Asset, Error)) return nullptr;
			Asset.Key = Key; Data.emplace(Key, std::move(Asset));
		}
		if (!Loading.insert(Key).second) { Error = "Cyclic FBX asset reference: " + Key; return nullptr; }
		const auto& Asset = Data.at(Key);
		URenderAsset* Object = nullptr;
		if (Asset.Type == EFbxAssetType::Texture2D)
		{
			Object = UAssetManager::CreateTexture(Asset.Texture);
			if (Object) Created.push_back(Object);
		}
		else if (Asset.Type == EFbxAssetType::Skeleton)
		{
			auto* Skeleton = Key == SelectedMeshSkeletonKey
				? Cast<USkeleton>(UAssetManager::FindLoadedAsset(Key)) : nullptr;
			if (Skeleton)
			{
				if (!PreservesReferenceBones(Skeleton->GetReferenceSkeleton(), Asset.RefSkeleton))
				{ Error = "Selected Skeleton on disk differs from the loaded Skeleton: " + Key; return nullptr; }
				FReferenceSkeleton Updated = Asset.RefSkeleton;
				PreviousReferenceSkeleton = std::move(Skeleton->ReferenceSkeleton);
				UpdatedSkeleton = Skeleton;
				Skeleton->ReferenceSkeleton = std::move(Updated);
			}
			else
			{
				Skeleton = Allocate<USkeleton>();
				if (!Skeleton) { Error = "Cannot create FBX Skeleton: " + Key; return nullptr; }
				Skeleton->ReferenceSkeleton = Asset.RefSkeleton;
			}
			Object = Skeleton;
		}
		else if (Asset.Type == EFbxAssetType::AnimSequence)
		{
			auto* Skeleton = Cast<USkeleton>(Resolve(Asset.SkeletonKey));
			if (!Skeleton) { if (Error.empty()) Error = "Invalid animation Skeleton reference"; return nullptr; }
			auto Tracks = Asset.Animation.BoneAnimationTracks;
			const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
			std::unordered_map<FString, int32> ByName;
			for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
				ByName.emplace(Reference.RefBoneInfo[Index].Name, Index);
			for (auto& Track : Tracks)
			{
				const auto It = ByName.find(Track.Name);
				if (It == ByName.end()) { Error = "Animation track is missing from its Skeleton: " + Track.Name; return nullptr; }
				Track.BoneTreeIndex = It->second;
			}
			auto* Sequence = Allocate<UAnimSequence>();
			if (!Sequence || !Sequence->Initialize(Skeleton, Asset.Animation.NumberOfKeys,
				Asset.Animation.FrameRate, std::move(Tracks), Error)) return nullptr;
			Object = Sequence;
		}
		else if (Asset.Type == EFbxAssetType::Material)
		{
			auto* Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
			if (!Base) { Error = "DefaultMaterial must be initialized before loading FBX materials"; return nullptr; }
			auto* Material = UMaterial::CreateInstance(Base);
			if (!Material) { Error = "Cannot create FBX material"; return nullptr; }
			Created.push_back(Material); Object = Material;
			if ((Base->ParamBuffer && !Material->ParamBuffer) || Material->Textures.Num() < 2)
			{ Error = "Cannot initialize FBX material resources"; return nullptr; }
			Material->BaseColor = Asset.Material.DiffuseOrFactor;
			Material->ShadingModel = Asset.Material.ShadingModel;
			Material->Shininess = Asset.Material.Shininess;
			Material->SpecularColor = Asset.Material.SpecularColor * Asset.Material.SpecularFactor;
			Material->EmissiveColor = Asset.Material.EmissiveColor * Asset.Material.EmissiveFactor;
			Material->SamplerState = Asset.Material.SamplerState;
			Material->UVScrollSpeed = FVector2(0, 0);
			Material->BlendState = Asset.Material.BlendState;
			if (Material->BlendState == EBlendState::AlphaBlend)
			{
				Material->PSOType = EPSOType::StaticMesh_Translucent;
				Material->DepthStencilState = EDepthStencilState::ReadOnly;
			}
			const FString Keys[]{ Asset.Material.DiffuseTexture, Asset.Material.NormalTexture };
			for (int I = 0; I < 2; ++I) if (!Keys[I].empty())
			{
				UTexture2D* Texture = nullptr;
				if (FFbxAssetBake::IsBakedPath(Keys[I])) Texture = Cast<UTexture2D>(Resolve(Keys[I]));
				else if (bAllowDiskDependencies)
				{
					FString TexturePath;
					if (FbxPath::ResolveKey(Keys[I], TexturePath, Error))
						Texture = UAssetManager::Get().LoadTexture(TexturePath);
				}
				if (!Texture) { if (Error.empty()) Error = "Invalid FBX texture reference: " + Keys[I]; return nullptr; }
				Material->Textures[I] = Texture;
			}
		}
		else
		{
			TArray<UMaterial*> Materials;
			for (const auto& MaterialKey : Asset.MaterialKeys)
			{
				auto* Material = Cast<UMaterial>(Resolve(MaterialKey));
				if (!Material) { if (Error.empty()) Error = "Invalid FBX material reference: " + MaterialKey; return nullptr; }
				Materials.Add(Material);
			}
			if (Asset.Type == EFbxAssetType::StaticMesh)
			{
				auto* Mesh = Allocate<UStaticMesh>(); Object = Mesh;
				Mesh->Materials = std::move(Materials); Mesh->ScreenThresholds = Asset.ScreenThresholds;
				int32 RuntimeLOD = 0;
				for (int32 L = 0; L < Asset.LODs.Num(); ++L)
				{
					if (Asset.LODs[L].MeshData.Indices.IsEmpty()) continue;
					if (L > 0) Mesh->ScreenThresholds[RuntimeLOD - 1] = Asset.ScreenThresholds[L - 1];
					FStaticMeshLODResource Resource;
					Resource.Data = Asset.LODs[L].MeshData;
					// Cook에서 완성한 UV와 탄젠트를 사용한다.
					Resource.Data.OptimizeTriangleOrderForVertexCache();
					if (!Resource.Data.Indices.IsEmpty())
					{
						Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(Resource.Data.Vertices.GetData(),
							static_cast<uint32>(Resource.Data.Vertices.Num() * sizeof(FVertex)), sizeof(FVertex));
						Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Resource.Data.Indices.GetData(), Resource.Data.Indices.Num());
						if (!Resource.VertexBuffer || !Resource.IndexBuffer) { Error = "Cannot create FBX mesh GPU buffers"; return nullptr; }
					}
					Resource.Data.BuildTriangleBVH();
					if (L == 0)
					{
						Mesh->MeshData = std::move(Resource.Data); Mesh->VertexBuffer = std::move(Resource.VertexBuffer);
						Mesh->IndexBuffer = std::move(Resource.IndexBuffer);
					}
					else Mesh->AdditionalLODs.Add(std::move(Resource));
					++RuntimeLOD;
				}
				Mesh->RebuildRenderData();
			}
			else if (Asset.Type == EFbxAssetType::SkeletalMesh)
			{
				auto* Skeleton = Cast<USkeleton>(Resolve(Asset.SkeletonKey));
				if (!Skeleton) { if (Error.empty()) Error = "Invalid FBX skeleton reference"; return nullptr; }
				const FReferenceSkeleton& Reference = Asset.RefSkeleton;
				TArray<FMatrix> ModelPose;
				if (!Reference.GetModelSpaceMatrices(ModelPose, Error)) return nullptr;
				TArray<FBoneInfo> Bones;
				for (int32 I = 0; I < ModelPose.Num(); ++I)
				{
					const auto& Bone = Reference.RefBoneInfo[I];
					Bones.Add({ Bone.Name, Bone.ParentIndex, Reference.RefBonePose[I], ModelPose[I].Inverse() });
				}
				TArray<FSkeletalMeshData> LODs;
				auto Thresholds = Asset.ScreenThresholds;
				for (int32 L = 0; L < Asset.LODs.Num(); ++L)
				{
					const auto& Source = Asset.LODs[L];
					if (Source.MeshData.Indices.IsEmpty()) continue;
					if (L > 0) Thresholds[LODs.Num() - 1] = Asset.ScreenThresholds[L - 1];
					FSkeletalMeshData& LOD = LODs.AddDefaulted_GetRef();
					LOD.BindVertices = Source.MeshData.Vertices;
					LOD.Indices = Source.MeshData.Indices;
					LOD.Bones = Bones;
					LOD.SourcePath = Asset.SourcePath;
					for (const auto& Slot : Source.MeshData.MaterialSlots)
						LOD.MaterialSlots.Add({ Slot.Name });
					for (const auto& Section : Source.MeshData.Sections)
						LOD.Sections.Add({ Section.StartIndex, Section.IndexCount, Section.MaterialSlotIndex });
					LOD.SkinWeight.SetNum(LOD.BindVertices.Num());
					for (int32 Vertex = 0; Vertex < LOD.BindVertices.Num(); ++Vertex)
					{
						for (int32 Influence = 0; Influence < MaxBoneInfluences; ++Influence)
						{
							LOD.SkinWeight[Vertex].BoneIndex[Influence] =
								Source.SkinWeights.InfluenceBones[Vertex][Influence];
							LOD.SkinWeight[Vertex].Weight[Influence] = Source.SkinWeights.InfluenceWeights[Vertex][Influence];
						}
					}
				}
				auto* Mesh = Allocate<USkeletalMesh>();
				if (!Mesh->Build(std::move(LODs), std::move(Materials), Thresholds, Error)) return nullptr;
				FSkeletonToMeshLinkup Linkup;
				if (!Skeleton->BuildLinkup(*Mesh, Linkup, Error)) return nullptr;
				Mesh->SetSkeleton(Skeleton);
				Object = Mesh;
			}
		}
		Loading.erase(Key);
		if (!Object) { if (Error.empty()) Error = "Cannot create FBX asset: " + Key; return nullptr; }
		Object->SetPath(Key); Objects.emplace(Key, Object);
		return Object;
	}

	void PreparePublish()
	{
		TMap<FString, URenderAsset*> Replacements;
		for (const auto& [Key, Object] : Objects)
			Replacements.Add(Key, Object);
		PreparedRegistry = UAssetManager::Get().PrepareAssetRegistration(Replacements);
		bPrepared = true;
	}
	void Publish()
	{
		if (!bPrepared) PreparePublish();
		UAssetManager::Get().CommitAssetRegistration(PreparedRegistry);
		bPublished = true;
	}
};

// 임시 파일은 Assets 안의 이번 임포트 전용 폴더에만 만든다.
struct FImportFolder
{
	fs::path Path;
	~FImportFolder()
	{
		FString Key, Error;
		if (!Path.empty() && Path.filename().wstring().starts_with(L".fbx-import-") &&
			FbxPath::GetAssetKey(FbxPath::ToString(Path), Key, Error, false))
		{
			std::error_code Ignored; fs::remove_all(Path, Ignored);
		}
	}
};
}

URenderAsset* FFbxAssetLoader::LoadAsset(const FString& Path, FString& Error)
{
	try
	{
		FString Key;
		if (!FFbxAssetBake::IsBakedPath(Path)) { Error = "Expected .fbxbin asset: " + Path; return nullptr; }
		if (!FbxPath::GetAssetKey(Path, Key, Error)) return nullptr;
		if (auto* Existing = UAssetManager::FindLoadedAsset(Key)) return Existing;
		FLoadBatch Batch;
		auto* Object = Batch.Resolve(Key);
		if (!Object) { Error = Batch.Error; return nullptr; }
		Batch.Publish(); return Object;
	}
	catch (const std::exception& E) { Error = E.what(); return nullptr; }
}

FFbxImportResult FFbxAssetLoader::Import(const FString& SourcePath, const FFbxImportOptions& Options, const FString& OutputDirectory, FCook Cook)
{
	FFbxImportResult Result;
	try
	{
		FString SourceKey;
		fs::path Source, Destination;
		if (!FbxPath::GetSourcePath(SourcePath, Source, SourceKey, Result.Error) ||
			!FbxPath::GetOutputDirectory(OutputDirectory.empty() ? FbxPath::DefaultOutputDirectory(SourcePath) : OutputDirectory, Destination, Result.Error)) return Result;
		FFbxImportOptions ResolvedOptions = Options;
		if (Options.bImportOnlyAnimations) ResolvedOptions.SkeletalMeshSkeletonKey.clear();
		if (!Options.bImportAnimations && !Options.bImportOnlyAnimations) ResolvedOptions.AnimationSkeletonKey.clear();
		auto ResolveSkeletonKey = [&](FString& Key)
		{
			if (Key.empty()) return true;
			FString SkeletonPath;
			return FbxPath::ResolveKey(Key, SkeletonPath, Result.Error)
				&& FbxPath::GetAssetKey(SkeletonPath, Key, Result.Error);
		};
		if (!ResolveSkeletonKey(ResolvedOptions.SkeletalMeshSkeletonKey) ||
			!ResolveSkeletonKey(ResolvedOptions.AnimationSkeletonKey)) return Result;
		fs::create_directories(Destination);
		FImportFolder Folder;
		for (uint32 I = 0; I < 100; ++I)
		{
			Folder.Path = Destination / (".fbx-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(I));
			if (fs::create_directory(Folder.Path)) break;
			Folder.Path.clear();
		}
		if (Folder.Path.empty()) { Result.Error = "Cannot create FBX staging directory"; return Result; }
		TArray<FFbxAssetData> Cooked;
		if (!Cook(FbxPath::ToString(Source), ResolvedOptions, FbxPath::ToString(Folder.Path), Cooked, Result.Warnings, Result.Error,
			FbxPath::ToString(Destination))) return Result;
		FLoadBatch Batch; Batch.bAllowDiskDependencies = false;
		Batch.SelectedMeshSkeletonKey = ResolvedOptions.SkeletalMeshSkeletonKey;
		Batch.SelectedAnimationSkeletonKey = ResolvedOptions.AnimationSkeletonKey;
		struct FReplacement { fs::path Target, Staged, Backup; bool bBackedUp = false, bInstalled = false; };
		std::vector<FReplacement> Files;
		TArray<FString> OutputKeys;
		for (auto& Data : Cooked)
		{
			fs::path Target;
			if (!FbxPath::ResolveKey(Data.Key, Target, Result.Error)) return Result;
			FReplacement File{ Target, Folder.Path / (std::to_string(Files.size()) + ".new"), Folder.Path / (std::to_string(Files.size()) + ".old") };
			if (!FFbxAssetBake::WriteBaked(FbxPath::ToString(File.Staged), Data, Result.Error)) return Result;
			FFbxAssetData Verified;
			if (!FFbxAssetBake::ReadBaked(FbxPath::ToString(File.Staged), Verified, Result.Error)) return Result;
			Verified.Key = Data.Key;
			if (!Batch.Data.emplace(Data.Key, std::move(Verified)).second) { Result.Error = "Duplicate FBX output key"; return Result; }
			Files.push_back(std::move(File)); OutputKeys.Add(Data.Key);
		}
		for (const auto& Key : OutputKeys)
			if (!Batch.Resolve(Key)) { Result.Error = Batch.Error; return Result; }
		Batch.PreparePublish();
		// 모든 데이터/리소스가 준비된 뒤 파일을 교체한다. 중간 실패 시 백업을 되돌린다.
		try
		{
			for (auto& File : Files)
			{
				if (fs::exists(File.Target)) { fs::rename(File.Target, File.Backup); File.bBackedUp = true; }
				fs::rename(File.Staged, File.Target); File.bInstalled = true;
			}
		}
		catch (const std::exception& E)
		{
			Result.Error = FString("Cannot replace FBX outputs: ") + E.what();
			bool Restored = true;
			for (auto It = Files.rbegin(); It != Files.rend(); ++It)
			{
				std::error_code Ec;
				if (It->bInstalled) fs::remove(It->Target, Ec);
				if (Ec) Restored = false;
				Ec.clear();
				if (It->bBackedUp) fs::rename(It->Backup, It->Target, Ec);
				if (Ec) Restored = false;
			}
			// 복원까지 실패한 경우 복구용 백업을 지우지 않는다.
			if (!Restored) { Result.Error += "; backups retained at " + FbxPath::ToString(Folder.Path); Folder.Path.clear(); }
			return Result;
		}
		Batch.Publish(); Result.AssetKeys = std::move(OutputKeys); Result.bSuccess = true;
	}
	catch (const std::exception& E) { Result.Error = E.what(); }
	return Result;
}
