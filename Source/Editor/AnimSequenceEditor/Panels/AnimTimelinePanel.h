#pragma once

#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"

class FAnimSequenceEditor;

// UE Animation 에디터 아래쪽의 Timeline 자리입니다.
// 아래쪽 재생 컨트롤 줄은 구현됨. 프레임 눈금·스크러버·Notify 트랙은 아직입니다.
class FAnimTimelinePanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FAnimTimelinePanel(FAnimSequenceEditor& InEditor);

protected:
	void DrawContent() override;

private:
	// 아래쪽 재생 컨트롤 줄: 처음/이전 프레임/역재생/재생/다음 프레임/끝/루프/배속/현재 위치
	void DrawTransportBar();

	FAnimSequenceEditor& AnimEditor;
};
