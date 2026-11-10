#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"

class FAnimSequenceEditor;

// Animation 에디터 탭. 뷰포트·본 계층·본 디테일·설정은 SkeletalMesh 패널을 재사용하고,
// 애니메이션 정보와 프리뷰 메시 선택 패널을 추가합니다. (Timeline 패널은 이후 단계에서 아래쪽에 추가)
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
};
