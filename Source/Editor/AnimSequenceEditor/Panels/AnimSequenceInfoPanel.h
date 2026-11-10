#pragma once

#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"

class FAnimSequenceEditor;

// 애니메이션 에셋 정보(길이·프레임·트랙·Skeleton)와 프리뷰 메시 선택을 표시합니다. (UE Asset Details + Preview Mesh)
class FAnimSequenceInfoPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FAnimSequenceInfoPanel(FAnimSequenceEditor& InEditor);

protected:
	void DrawContent() override;

private:
	FAnimSequenceEditor& AnimEditor;
};
