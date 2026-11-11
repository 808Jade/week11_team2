#include "EnginePCH.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "AnimSequenceEditorTab.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimDataModel.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectIterator.h"

#include <algorithm>
#include <cmath>

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
	RememberSavedNotifies();
}

void FAnimSequenceEditor::RememberSavedNotifies()
{
	// 처음 보는 애니메이션만 기록합니다. 로드할 때 파일에서 읽었으므로 지금 값이 디스크 값입니다.
	if (Sequence)
		SavedNotifies.try_emplace(Sequence, Sequence->Notifies);
}

bool FAnimSequenceEditor::HasUnsavedNotifies() const
{
	if (!Sequence) return false;
	const auto It = SavedNotifies.find(Sequence);
	return It != SavedNotifies.end() && !(Sequence->Notifies == It->second);
}

bool FAnimSequenceEditor::SaveNotifies()
{
	if (!Sequence || SequencePath.empty())
	{
		LastNotifySaveError = "No animation to save";
		return false;
	}
	if (!Sequence->SaveNotifiesToFile(SequencePath, LastNotifySaveError))
	{
		HTR_LOG(Error, "Failed to save notifies for {}: {}", SequencePath, LastNotifySaveError);
		return false;
	}
	SavedNotifies[Sequence] = Sequence->Notifies;
	HTR_LOG(Info, "Saved {} notifies: {}", Sequence->Notifies.Num(), UAnimSequenceBase::GetNotifiesFilePath(SequencePath));
	return true;
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

	// 교체 전 애니메이션의 저장 안 된 Notify는 애셋(메모리)에 남아 있어서, 다시 돌아오면 이어서 저장할 수 있습니다.
	if (HasUnsavedNotifies())
		HTR_LOG(Warning, "Notifies of {} are not saved yet", SequencePath);
	Sequence = NewSequence;
	SequencePath = Key;
	RememberSavedNotifies();
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
	if (!Component->PlayAnimation(Sequence, bLooping, bReverse ? -PlaySpeed : PlaySpeed, LastPlayError))
	{
		HTR_LOG(Warning, "Animation preview failed for {}: {}", SequencePath, LastPlayError);
	}
}

UAnimSingleNodeInstance* FAnimSequenceEditor::GetPlayer() const
{
	const USkeletalMeshComponent* Component = GetPreviewComponent();
	return Component ? Component->GetSingleNodeInstance() : nullptr;
}

void FAnimSequenceEditor::Play(bool bInReverse)
{
	UAnimSingleNodeInstance* Player = GetPlayer();
	if (!Player) return;

	bReverse = bInReverse;
	Player->SetPlayRate(bReverse ? -PlaySpeed : PlaySpeed);
	Player->SetLooping(bLooping);

	// 루프가 아닌데 이미 진행 방향의 끝에 있으면 반대쪽 끝에서 다시 시작합니다. (UE 재생 버튼과 같은 동작)
	if (!bLooping)
	{
		const float Time = Player->GetCurrentTime();
		const float Length = Player->GetLength();
		if (!bReverse && Time >= Length) Player->SetPosition(0.0f);
		if (bReverse && Time <= 0.0f) Player->SetPosition(Length);
	}
	Player->SetPlaying(true);
}

void FAnimSequenceEditor::Pause()
{
	if (UAnimSingleNodeInstance* Player = GetPlayer())
	{
		Player->SetPlaying(false);
	}
}

void FAnimSequenceEditor::JumpToStart()
{
	SetPlaybackTime(0.0f);
}

void FAnimSequenceEditor::JumpToEnd()
{
	SetPlaybackTime(GetPlaybackLength());
}

void FAnimSequenceEditor::StepFrame(int32 Delta)
{
	const int32 Frames = GetNumberOfFrames();
	const float Rate = GetFrameRate();
	if (Frames <= 0 || Rate <= 0.0f) return;

	// 현재 시간을 가장 가까운 프레임으로 맞춘 뒤 이동합니다. 프레임 번호는 0..Frames (키 개수 = Frames + 1)
	int32 Frame = static_cast<int32>(std::lround(GetPlaybackTime() * Rate)) + Delta;
	if (bLooping)
	{
		Frame %= Frames;
		if (Frame < 0) Frame += Frames;
	}
	else
	{
		Frame = std::clamp(Frame, 0, Frames);
	}
	SetPlaybackTime(static_cast<float>(Frame) / Rate);
}

void FAnimSequenceEditor::SetPlaybackTime(float Time)
{
	if (UAnimSingleNodeInstance* Player = GetPlayer())
	{
		Player->SetPlaying(false);
		Player->SetPosition(Time);
	}
}

void FAnimSequenceEditor::SetLooping(bool bInLooping)
{
	bLooping = bInLooping;
	if (UAnimSingleNodeInstance* Player = GetPlayer())
	{
		Player->SetLooping(bLooping);
	}
}

void FAnimSequenceEditor::SetPlaySpeed(float InSpeed)
{
	if (!std::isfinite(InSpeed) || InSpeed <= 0.0f) return;
	PlaySpeed = InSpeed;
	if (UAnimSingleNodeInstance* Player = GetPlayer())
	{
		Player->SetPlayRate(bReverse ? -PlaySpeed : PlaySpeed);
	}
}

bool FAnimSequenceEditor::IsPlaying() const
{
	const UAnimSingleNodeInstance* Player = GetPlayer();
	return Player && Player->IsPlaying();
}

bool FAnimSequenceEditor::IsPlayingReverse() const
{
	const UAnimSingleNodeInstance* Player = GetPlayer();
	return Player && Player->IsPlaying() && Player->GetPlayRate() < 0.0f;
}

float FAnimSequenceEditor::GetPlaybackTime() const
{
	const UAnimSingleNodeInstance* Player = GetPlayer();
	return Player ? Player->GetCurrentTime() : 0.0f;
}

float FAnimSequenceEditor::GetPlaybackLength() const
{
	return Sequence ? Sequence->GetPlayLength() : 0.0f;
}

float FAnimSequenceEditor::GetFrameRate() const
{
	const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
	return Model ? Model->GetFrameRate() : 0.0f;
}

int32 FAnimSequenceEditor::GetNumberOfFrames() const
{
	const UAnimDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
	return Model ? Model->GetNumberOfFrames() : 0;
}

void FAnimSequenceEditor::ShutdownResources()
{
	Sequence = nullptr;
	SequencePath.clear();
	PreviewMeshPaths.Empty();
	CompatibleAnimationPaths.Empty();
	PreviewMeshPath.clear();
	SavedNotifies.clear();
	FSkeletalMeshEditor::ShutdownResources();
}
