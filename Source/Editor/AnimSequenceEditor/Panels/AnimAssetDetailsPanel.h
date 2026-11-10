#pragma once

#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"

class FAnimSequenceEditor;

// UE Animation 에디터 왼쪽의 Asset Details에 해당합니다.
// 애니메이션 정보(길이·프레임·트랙·Skeleton)와 프리뷰 메시 선택을 표시합니다.
// 속도 스케일·루프 같은 편집 가능한 값은 애셋에 UPROPERTY를 추가한 뒤 이 패널에 붙일 예정입니다.
class FAnimAssetDetailsPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FAnimAssetDetailsPanel(FAnimSequenceEditor& InEditor);

protected:
	void DrawContent() override;

private:
	FAnimSequenceEditor& AnimEditor;
};
