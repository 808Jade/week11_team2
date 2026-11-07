#pragma once

#include "SkeletalMeshEditorPanel.h"

class FSkeletalMeshAnimationPanel : public FSkeletalMeshEditorPanel {
public:
	explicit FSkeletalMeshAnimationPanel(FSkeletalMeshEditor& InEditor);

private:
	void DrawContent() override;

private:
	int32 mSelectedClip{};
	float mSampleRate{ 30.0f };
};
