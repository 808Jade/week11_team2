#pragma once

#include "SkeletalMeshEditorPanel.h"

// 애니메이션 클립을 골라 프리뷰 메시에 재생합니다. 원본 애셋은 수정하지 않습니다.
class FSkeletalMeshAnimationPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshAnimationPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	void DrawClipCombo();
};
