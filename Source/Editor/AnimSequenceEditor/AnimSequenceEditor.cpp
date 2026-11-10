#include "EnginePCH.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "AnimSequenceEditorTab.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Editor/AssetEditor/EditorManager.h"
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
	, SequencePath(FAssetUtils::GetAssetKey(InAssetPath))
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

TArray<FString> FAnimSequenceEditor::FindCompatibleAnimationPaths(const UAnimSequenceBase& Sequence)
{
	TArray<FString> Paths;
	const USkeleton* Skeleton = Sequence.GetSkeleton();
	if (!Skeleton)
	{
		return Paths;
	}

	// DetailsPanel의 AnimToPlay 목록과 같은 방식: 로드한 뒤 같은 USkeleton 객체인지 비교합니다.
	for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::AnimSequence))
	{
		UAssetManager::GetAssetByPath<UAnimSequenceBase>(ItemPath);
	}

	for (TObjectIterator<UAnimSequenceBase> Itr; Itr; ++Itr)
	{
		const FString Path = Itr->GetPath();
		if (Path.empty() || UAssetManager::FindLoadedAsset(Path) != *Itr || Itr->GetSkeleton() != Skeleton)
		{
			continue;
		}
		const FString Key = FAssetUtils::GetAssetKey(Path);
		if (!Key.empty() && std::find(Paths.begin(), Paths.end(), Key) == Paths.end())
		{
			Paths.Add(Key);
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

	RefreshCompatibleAnimations();
	RestartAnimation();
	return true;
}

void FAnimSequenceEditor::RefreshCompatibleAnimations()
{
	CompatibleAnimationPaths = Sequence ? FindCompatibleAnimationPaths(*Sequence) : TArray<FString>{};
}

bool FAnimSequenceEditor::ChangeAnimation(const FString& AnimationPath)
{
	const FString Key = FAssetUtils::GetAssetKey(AnimationPath);
	if (Key.empty() || Key == SequencePath)
	{
		return Key == SequencePath;
	}

	UAnimSequenceBase* NewSequence = UAssetManager::GetAssetByPath<UAnimSequenceBase>(AnimationPath);
	if (!NewSequence)
	{
		LastPlayError = "Failed to load animation: " + AnimationPath;
		return false;
	}
	// 프리뷰 메시는 그대로 쓰므로 같은 Skeleton이어야 합니다.
	if (!Sequence || NewSequence->GetSkeleton() != Sequence->GetSkeleton())
	{
		LastPlayError = "Animation does not use the same Skeleton: " + AnimationPath;
		return false;
	}

	Sequence = NewSequence;
	SequencePath = Key;
	RestartAnimation();
	return true;
}

bool FAnimSequenceEditor::IsEditingAsset(const FString& Path) const
{
	return FAssetEditor::IsEditingAsset(Path) || (!SequencePath.empty() && Path == SequencePath);
}

void FAnimSequenceEditor::OnReopenAsset(const FString& Path)
{
	// 교체된 상태에서 처음 연 애니메이션을 다시 열면 그 애니메이션으로 되돌립니다.
	ChangeAnimation(Path);
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
		HTR_LOG(Warning, "Animation preview failed for {}: {}", SequencePath, LastPlayError);
	}
}

void FAnimSequenceEditor::ShutdownResources()
{
	Sequence = nullptr;
	SequencePath.clear();
	PreviewMeshPaths.Empty();
	CompatibleAnimationPaths.Empty();
	PreviewMeshPath.clear();
	FSkeletalMeshEditor::ShutdownResources();
}
