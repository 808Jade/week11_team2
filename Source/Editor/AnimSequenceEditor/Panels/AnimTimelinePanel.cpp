#include "EnginePCH.h"
#include "AnimTimelinePanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"

FAnimTimelinePanel::FAnimTimelinePanel(FAnimSequenceEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Timeline")
	, AnimEditor(InEditor)
{
}

void FAnimTimelinePanel::DrawContent()
{
	// TODO: 재생 컨트롤(재생/정지/역재생/프레임 이동/루프/배속), 스크러버, Notify 트랙
	ImGui::TextDisabled("Timeline (not implemented yet)");

	if (ImGui::Button("Restart"))
	{
		AnimEditor.RestartAnimation();
	}
}
