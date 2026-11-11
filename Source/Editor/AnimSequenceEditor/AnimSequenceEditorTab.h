#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"

class FAnimSequenceEditor;

// Animation 에디터 탭. 뷰포트·본 계층·본 디테일·설정은 SkeletalMesh 패널을 재사용하고,
// Asset Details·Timeline·Asset Browser 패널을 추가합니다. (UE Animation 에디터 배치)
class FAnimSequenceEditorTab : public FAssetEditorTab
{
public:
	explicit FAnimSequenceEditorTab(FAnimSequenceEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);

	FAnimSequenceEditor& AnimEditor;
	TArray<TUniquePtr<FSkeletalMeshEditorPanel>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
	// 탭 제목에 표시 중인 애니메이션 경로. Asset Browser로 교체되면 제목을 갱신합니다.
	FString DisplayedSequencePath;
};
