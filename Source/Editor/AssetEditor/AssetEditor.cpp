#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Runtime/Render/LineBatcher.h"

FAssetEditor::FAssetEditor(
	UEditorEngine& InEditorEngine,
	FEditorManager& InEditorManager,
	const FString& InAssetPath
)
	: EditorEngine(InEditorEngine)
	, EditorManager(InEditorManager)
	, AssetPath(InAssetPath)
{
}

FAssetEditor::~FAssetEditor()
{
	// 파생 클래스의 자원 정리는 Manager가 소멸 전에 Shutdown으로 수행한다.
	if (Tab)
	{
		EditorManager.RemoveEditorTab(Tab.get());
	}
}

bool FAssetEditor::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	if (bCloseRequested)
	{
		return false;
	}

	if (!InitializeResources())
	{
		Shutdown();
		return false;
	}

	Tab = CreateTab();
	if (!Tab)
	{
		Shutdown();
		return false;
	}

	EditorManager.AddEditorTab(Tab.get());
	bInitialized = true;
	Activate();
	return true;
}

void FAssetEditor::Tick(float DeltaTime)
{
	PreviewScene.Tick(DeltaTime);
}

void FAssetEditor::OnRenderPreview()
{
	// 프리뷰 장면과 뷰포트 렌더링은 후속 단계에서 연결한다.
}

void FAssetEditor::Activate()
{
	if (Tab && !bCloseRequested)
	{
		EditorManager.ActivateEditorTab(Tab.get());
	}
}

void FAssetEditor::Close()
{
	if (bCloseRequested)
	{
		return;
	}

	bCloseRequested = true;
	if (Tab)
	{
		EditorManager.RequestCloseEditorTab(Tab.get());
	}
}

void FAssetEditor::Shutdown()
{
	if (bShutdown)
	{
		return;
	}

	bShutdown = true;
	bCloseRequested = true;
	if (Tab)
	{
		EditorManager.RemoveEditorTab(Tab.get());
		Tab.reset();
	}
	ShutdownResources();
	bInitialized = false;
}

void FAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
}

bool FAssetEditor::InitializeResources()
{
	if (!PreviewScene.Initialize(&EditorEngine))
	{
		return false;
	}

	// 새로운 ViewportClient 생성
	ViewportClient = MakeUnique<FEditorViewportClient>(PreviewScene);
	ViewportClient->SetViewRotation(FRotator(30.0f, 210.0f, 0.0f));

	// Scene 세팅
	PreviewScene.Setup();

	return true;
}

TUniquePtr<IEditorTab> FAssetEditor::CreateTab()
{
	return MakeUnique<FAssetEditorTab>(*this);
}

void FAssetEditor::ShutdownResources()
{
	ViewportClient.reset();
	PreviewScene.Release();
}
