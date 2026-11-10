#pragma once

#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"

class FAnimSequenceEditor;

// UE Animation 에디터 아래쪽의 Timeline 자리입니다.
// 재생 컨트롤·스크러버·Notify 트랙은 아직 구현하지 않았습니다.
class FAnimTimelinePanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FAnimTimelinePanel(FAnimSequenceEditor& InEditor);

protected:
	void DrawContent() override;

private:
	FAnimSequenceEditor& AnimEditor;
};
