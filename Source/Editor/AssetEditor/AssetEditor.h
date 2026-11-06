#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Editor/AssetEditor/FPreviewScene.h"

class FEditorViewportClient;
class FGizmo;

class FAssetEditor
{
public:
	FAssetEditor(
		class UEditorEngine& InEditorEngine,
		class FEditorManager& InEditorManager,
		const FString& InAssetPath
	);
	virtual ~FAssetEditor();
	
	
	////////////////////////////////////////////////////////////
	// 에디터 공통 인터페이스
	////////////////////////////////////////////////////////////

	// Editor를 초기화합니다.
	bool Initialize();

	// 틱을 돌립니다.
	virtual void Tick(float DeltaTime);
	
	// 현재 상태를 렌더링합니다.
	virtual void OnRenderPreview();

	// Editor를 포커싱합니다.
	void Activate();

	// Editor를 종료합니다.
	void Close();

	// Editor를 정리합니다.
	void Shutdown();

	virtual void OnRenderLine(class FLineBatcher& LineBatcher);

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	virtual const char* GetEditorTypeName() const = 0;
	const FString& GetAssetPath() const { return AssetPath; }
	FPreviewScene& GetPreviewScene() { return PreviewScene; }
	FEditorViewportClient* GetViewportClient() const { return ViewportClient.get(); }
	virtual const FGizmo* GetPreviewGizmo() const { return nullptr; }

	bool IsCloseRequested() const { return bCloseRequested; }

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	virtual TUniquePtr<class IEditorTab> CreateTab();

	// 리소스를 초기화합니다.
	virtual bool InitializeResources();
	
	// 리소스를 정리합니다.
	virtual void ShutdownResources();

	UEditorEngine& EditorEngine;
	FEditorManager& EditorManager;
	FPreviewScene PreviewScene;

	TUniquePtr<FEditorViewportClient> ViewportClient;

private:
	FString AssetPath;
	TUniquePtr<class IEditorTab> Tab;
	bool bInitialized = false;
	bool bShutdown = false;
	bool bCloseRequested = false;
};
