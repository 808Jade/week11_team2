#include "EnginePCH.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Editor/AnimationEditor/FAnimGraphEditor.h"
#include "Editor/AnimationEditor/FAnimGraphPanel.h"
#include "Editor/AnimationEditor/FBlendSpaceEditor.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Animation/AnimSequenceBase.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include <filesystem>

FEditorManager::FEditorManager(UEditorEngine& InEditorEngine)
	: EditorEngine(InEditorEngine)
	, LevelEditorTab(MakeUnique<FLevelEditorTab>())
{
	TabContainer.AddTab(LevelEditorTab.get());
}

FEditorManager::~FEditorManager() = default;

void FEditorManager::OpenEditor(const FString& AssetPath)
{
	if (bShuttingDown)
	{
		return;
	}

	const FString Path = FAssetUtils::GetAssetKey(AssetPath);
	if (Path.empty())
	{
		HTR_LOG(Warning, "Cannot open an editor for an empty or invalid asset path.");
		return;
	}

	PendingOpens.Add(Path);
}

FAssetEditor* FEditorManager::FindEditor(const FString& Path) const
{
	for (const auto& Editor : Editors)
	{
		if (Editor->IsEditingAsset(Path))
		{
			return Editor.get();
		}
	}

	return nullptr;
}

void FEditorManager::CreateEditor(const FString& AssetPath) {
    const FString FilePathForAnimation{FAssetUtils::ResolveAssetFilePath(AssetPath)};
    const auto AnimationPath{FbxPath::FromString(FilePathForAnimation)};
    const auto Extension{AnimationPath.extension()};
    if (Extension == L".animgraph" || Extension == L".blendspace") {
        FString Error{};
        if (Extension == L".animgraph") {
            if (!mAnimGraphEditor) {
                mAnimGraphEditor = MakeUnique<FAnimGraphEditor>();
            }
            mAnimGraphEditor->Open(AnimationPath, Error);
        } else {
            if (!mBlendSpaceEditor) {
                mBlendSpaceEditor = MakeUnique<FBlendSpaceEditor>();
            }
            mBlendSpaceEditor->Open(AnimationPath, Error);
        }
        if (!Error.empty()) {
            HTR_LOG(Error, "{}", Error);
        }
        return;
    }

	// 이미 열려 있는 에디터가 있으면 활성화
	if (FAssetEditor* Existing = FindEditor(AssetPath))
	{
		Existing->OnReopenAsset(AssetPath);
		Existing->Activate();
		return;
	}

	// 애셋을 불러온다
	URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(AssetPath);
	
	// AssetPath가 제각각인 경우가 있으므로, 실패해도 여러가지 Key로 재시도한다.
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(AssetPath);
	if (!Asset && !FilePath.empty())
	{
		// 프로젝트 상대 경로로 시도
		const FString ProjectPath = std::filesystem::path(FilePath)
			.lexically_relative(std::filesystem::current_path()).generic_string();
		Asset = UAssetManager::GetAssetByPath<URenderAsset>(ProjectPath);
		
		if (!Asset)
		{
			// 절대 경로로 시도
			Asset = UAssetManager::GetAssetByPath<URenderAsset>(FilePath);
		}
	}
	
	// obj 파일은 AssetManager에 등록되지 않으므로 직접 로드
	if (!Asset && std::filesystem::path(AssetPath).extension() == ".obj")
	{
		Asset = UAssetManager::LoadObjStaticMesh(FilePath);
	}

	// 베이크된 FBX는 파일에 기록된 실제 메시 타입으로 로드합니다.
	if (!Asset && FFbxAssetBake::IsBakedPath(FilePath))
	{
		FString Error;
		Asset = FFbxAssetLoader::LoadAsset(FilePath, Error);
		if (!Asset)
		{
			HTR_LOG(Warning, "Failed to load mesh preview: {} ({})", AssetPath, Error);
			return;
		}
	}

	if (!Asset)
	{
		HTR_LOG(Warning, "Asset is not loaded or could not be loaded: {}", AssetPath.c_str());
		return;
	}


	// Editor 불러오기
	TUniquePtr<FAssetEditor> Editor = nullptr;
	if (Asset->IsA<UStaticMesh>())
	{
		UStaticMesh* MeshAsset = Cast<UStaticMesh>(Asset);
		Editor = MakeUnique<FStaticMeshEditor>(EditorEngine, *this, AssetPath, *MeshAsset);
	}

	if (Asset->IsA<USkeletalMesh>())
	{
		USkeletalMesh* MeshAsset = Cast<USkeletalMesh>(Asset);
		Editor = MakeUnique<FSkeletalMeshEditor>(EditorEngine, *this, AssetPath, *MeshAsset);
	}

	// 애니메이션은 같은 Skeleton을 쓰는 Skeletal Mesh를 프리뷰 메시로 골라 엽니다. (UE Skeleton Preview Mesh)
	if (UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Asset))
	{
		USkeletalMesh* PreviewMesh = nullptr;
		for (const FString& MeshPath : FAnimSequenceEditor::FindPreviewMeshPaths(*Sequence))
		{
			PreviewMesh = UAssetManager::GetAssetByPath<USkeletalMesh>(MeshPath);
			if (PreviewMesh && PreviewMesh->GetSkeleton() == Sequence->GetSkeleton()) break;
			PreviewMesh = nullptr;
		}
		if (!PreviewMesh)
		{
			HTR_LOG(Warning, "No skeletal mesh uses the Skeleton of this animation: {}", AssetPath.c_str());
			return;
		}
		Editor = MakeUnique<FAnimSequenceEditor>(EditorEngine, *this, AssetPath, *Sequence, *PreviewMesh);
	}

	// 지원하지 않는 타입
	if (!Editor)
	{
		HTR_LOG(Warning, "Unsupported asset editor type: {}", AssetPath.c_str());
		return;
	}

	// 초기화 실패시 로깅 후 return
	if (!Editor->Initialize())
	{
		Editor->Shutdown();
		HTR_LOG(Warning, "Asset editor initialization failed: {}", AssetPath.c_str());
		return;
	}

	Editors.Add(std::move(Editor));
	HTR_LOG(Info, "Opened asset editor: {}", AssetPath.c_str());
}

void FEditorManager::Tick(float DeltaTime)
{
	if (bShuttingDown)
	{
		return;
	}

	LevelEditorTab->Tick(DeltaTime);

	for (auto& Editor : Editors)
	{
		if (!Editor->IsCloseRequested())
		{
			Editor->Tick(DeltaTime);
		}
	}
}

void FEditorManager::RenderPreview()
{
	for (auto& Editor : Editors)
	{
		if (!Editor->IsCloseRequested())
		{
			Editor->OnRenderPreview();
		}
	}
}

void FEditorManager::LateTick()
{
	// ImGui의 분리 창까지 렌더링한 뒤 호출한다. 이번 프레임 텍스처 참조도 끝난 시점이다.
	for (int32 Index = Editors.Num() - 1; Index >= 0; --Index)
	{
		if (Editors[Index]->IsCloseRequested())
		{
			HTR_LOG(Info, "Closed asset editor: {}", Editors[Index]->GetAssetPath().c_str());
			Editors[Index]->Shutdown();
			Editors.RemoveAt(Index);
		}
	}

	TArray<FString> Requests = std::move(PendingOpens);
	PendingOpens = {};

	for (const FString& Path : Requests)
	{
		CreateEditor(Path);
	}
}

void FEditorManager::Shutdown()
{
	if (bShuttingDown)
	{
		return;
	}

	bShuttingDown = true;
	mAnimGraphPanel.reset();
	mAnimGraphEditor.reset();
	mBlendSpaceEditor.reset();
	
	PendingOpens.Empty();

	for (auto& Editor : Editors)
	{
		Editor->Shutdown();
	}
	Editors.Empty();

	TabContainer.RemoveTab(LevelEditorTab.get());
	LevelEditorTab.reset();
}

void FEditorManager::DrawEditorUI(USkeletalMeshComponent* DebugTarget, bool Playing) {
    TabContainer.OnRender();
    if (mAnimGraphEditor) {
        mAnimGraphEditor->Draw();
    }
    if (mBlendSpaceEditor) {
        mBlendSpaceEditor->Draw();
    }
    if (mAnimGraphPanel && mAnimGraphDebugOpen) {
        mAnimGraphPanel->Draw(DebugTarget, Playing, mAnimGraphDebugOpen);
    }
}

void FEditorManager::OpenAnimGraphDebug() {
    if (!mAnimGraphPanel) {
        mAnimGraphPanel = MakeUnique<FAnimGraphPanel>();
    }
    mAnimGraphDebugOpen = true;
}
