#include "EnginePCH.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "AnimSequenceEditorTab.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectIterator.h"

#include <algorithm>

FAnimSequenceEditor::FAnimSequenceEditor(
	UEditorEngine& InEditorEngine,
	FEditorManager& InEditorManager,
	const FString& InAssetPath,
	UAnimSequenceBase& InSequence,
	USkeletalMesh& InPreviewMesh
)
	: FSkeletalMeshEditor(InEditorEngine, InEditorManager, InAssetPath, InPreviewMesh)
	, Sequence(&InSequence)
{
}

TArray<FString> FAnimSequenceEditor::FindPreviewMeshPaths(const UAnimSequenceBase& Sequence)
{
	TArray<FString> Paths;
	const USkeleton* Skeleton = Sequence.GetSkeleton();
	if (!Skeleton)
	{
		return Paths;
	}
	const FString SkeletonKey = FAssetUtils::GetAssetKey(Skeleton->GetPath());

	auto AddUnique = [&Paths](const FString& Key)
	{
		if (!Key.empty() && std::find(Paths.begin(), Paths.end(), Key) == Paths.end())
		{
			Paths.Add(Key);
		}
	};

	// 이미 로드된 메시: 재생 시 검사와 같은 기준(같은 USkeleton 객체)으로 고릅니다.
	for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
	{
		const FString Path = Itr->GetPath();
		if (Path.empty() || UAssetManager::FindLoadedAsset(Path) != *Itr || Itr->GetSkeleton() != Skeleton)
		{
			continue;
		}
		AddUnique(FAssetUtils::GetAssetKey(Path));
	}

	// 아직 로드되지 않은 메시: 로드하지 않고 베이크 파일에 기록된 SkeletonKey만 비교합니다.
	for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::SkeletalMesh))
	{
		const FString FilePath = FAssetUtils::ResolveAssetFilePath(ItemPath);
		if (!FFbxAssetBake::IsBakedPath(FilePath))
		{
			continue;
		}

		FFbxAssetData Data;
		FString Error;
		if (FFbxAssetBake::ReadBaked(FilePath, Data, Error) && FAssetUtils::GetAssetKey(Data.SkeletonKey) == SkeletonKey)
		{
			AddUnique(FAssetUtils::GetAssetKey(ItemPath));
		}
	}

	std::sort(Paths.begin(), Paths.end());
	return Paths;
}

bool FAnimSequenceEditor::InitializeResources()
{
	if (!Sequence || !FSkeletalMeshEditor::InitializeResources())
	{
		return false;
	}

	PreviewMeshPaths = FindPreviewMeshPaths(*Sequence);
	if (const USkeletalMesh* Mesh = GetMesh())
	{
		PreviewMeshPath = FAssetUtils::GetAssetKey(Mesh->GetPath());
	}

	RestartAnimation();
	return true;
}

TUniquePtr<IEditorTab> FAnimSequenceEditor::CreateTab()
{
	return MakeUnique<FAnimSequenceEditorTab>(*this);
}

bool FAnimSequenceEditor::ChangePreviewMesh(const FString& MeshPath)
{
	USkeletalMesh* NewMesh = UAssetManager::GetAssetByPath<USkeletalMesh>(MeshPath);
	if (!NewMesh || NewMesh->GetSkeleton() != Sequence->GetSkeleton())
	{
		LastPlayError = "Preview mesh does not use this animation's Skeleton: " + MeshPath;
		return false;
	}

	// 메시를 바꾸면 컴포넌트가 애니메이션 인스턴스를 지우므로 다시 재생합니다.
	if (!SetPreviewMesh(*NewMesh))
	{
		LastPlayError = "Preview mesh has no renderable LOD: " + MeshPath;
		return false;
	}
	PreviewMeshPath = FAssetUtils::GetAssetKey(MeshPath);
	RestartAnimation();
	return true;
}

void FAnimSequenceEditor::RestartAnimation()
{
	USkeletalMeshComponent* Component = GetPreviewComponent();
	if (!Component || !Sequence)
	{
		return;
	}

	LastPlayError.clear();
	if (!Component->PlayAnimation(Sequence, true, 1.0f, LastPlayError))
	{
		HTR_LOG(Warning, "Animation preview failed for {}: {}", GetAssetPath(), LastPlayError);
	}
}

void FAnimSequenceEditor::ShutdownResources()
{
	Sequence = nullptr;
	PreviewMeshPaths.Empty();
	PreviewMeshPath.clear();
	FSkeletalMeshEditor::ShutdownResources();
}
