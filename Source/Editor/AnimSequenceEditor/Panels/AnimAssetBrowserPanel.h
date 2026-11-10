#pragma once

#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"
#include "Core/EngineString.h"

class FAnimSequenceEditor;

// UE Animation 에디터 오른쪽 아래의 Asset Browser에 해당합니다.
// 같은 Skeleton을 쓰는 애니메이션만 나열하고, 클릭하면 이 에디터의 애니메이션을 교체합니다.
class FAnimAssetBrowserPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FAnimAssetBrowserPanel(FAnimSequenceEditor& InEditor);

protected:
	void DrawContent() override;

private:
	FAnimSequenceEditor& AnimEditor;
	char Filter[128] = {};
};
