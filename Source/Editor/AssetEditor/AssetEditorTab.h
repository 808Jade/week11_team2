#pragma once

#include "Editor/EditorUI/EditorTab.h"
#include "Core/EngineString.h"

class FAssetEditor;

class FAssetEditorTab : public IEditorTab
{
public:
	explicit FAssetEditorTab(FAssetEditor& InEditor);

	// 탭의 메뉴 바를 그립니다.
	void DrawMenuBar() override;
	
	// 탭의 렌더링을 수행합니다.
	void OnRender(bool bActive) override;

	// 탭이 닫히기 전에 호출됩니다.
	void OnCloseRequested() override;

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const char* GetTabName() const override { return TabName.c_str(); }
	bool CanClose() const override { return true; }

	// 탭에 보이는 파일 이름만 바꿉니다. ImGui ID(### 뒤)는 처음 연 애셋 경로로 유지합니다.
	void SetDisplayFilename(const FString& Filename);

private:
	FAssetEditor& Editor;
	FString TabName;
};
