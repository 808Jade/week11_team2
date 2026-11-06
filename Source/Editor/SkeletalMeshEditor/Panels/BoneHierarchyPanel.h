#pragma once

#include "SkeletalMeshEditorPanel.h"
#include "Containers/Array.h"

// 스켈레탈 메시의 본 계층을 표시하는 패널입니다.
class FBoneHierarchyPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	void DrawBoneHierarchy(int32 BoneIndex);
};
