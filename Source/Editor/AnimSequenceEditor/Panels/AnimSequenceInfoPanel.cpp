#include "EnginePCH.h"
#include "AnimSequenceInfoPanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimDataModel.h"
#include "Animation/Skeleton.h"
#include "Asset/FbxImporter/FbxPath.h"

FAnimSequenceInfoPanel::FAnimSequenceInfoPanel(FAnimSequenceEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Animation")
	, AnimEditor(InEditor)
{
}

void FAnimSequenceInfoPanel::DrawContent()
{
	const UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	if (!Sequence)
	{
		return;
	}

	const FString Name = FbxPath::ToString(FbxPath::FromString(AnimEditor.GetAssetPath()).stem());
	ImGui::TextDisabled("Asset Name");
	ImGui::TextWrapped("%s", Name.c_str());
	ImGui::Spacing();
	ImGui::TextDisabled("Asset Path");
	ImGui::TextWrapped("%s", AnimEditor.GetAssetPath().c_str());
	ImGui::Separator();

	if (const UAnimDataModel* Model = Sequence->GetDataModel())
	{
		ImGui::Text("Length: %.3f s", Model->GetPlayLength());
		ImGui::Text("Frames: %d", Model->GetNumberOfFrames());
		ImGui::Text("Frame Rate: %.2f fps", Model->GetFrameRate());
		ImGui::Text("Tracks: %d", static_cast<int>(Model->GetBoneAnimationTracks().Num()));
	}
	if (const USkeleton* Skeleton = Sequence->GetSkeleton())
	{
		ImGui::TextWrapped("Skeleton: %s", Skeleton->GetPath().c_str());
	}

	// UE의 Preview Mesh 선택: 같은 Skeleton을 쓰는 Skeletal Mesh만 고를 수 있습니다.
	ImGui::SeparatorText("Preview Mesh");
	const FString& Current = AnimEditor.GetPreviewMeshPath();
	const FString CurrentName = FbxPath::ToString(FbxPath::FromString(Current).stem());
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::BeginCombo("##PreviewMesh", CurrentName.c_str()))
	{
		for (const FString& Path : AnimEditor.GetPreviewMeshPaths())
		{
			const bool bSelected = Path == Current;
			const FString ItemName = FbxPath::ToString(FbxPath::FromString(Path).stem());
			ImGui::PushID(Path.c_str());
			if (ImGui::Selectable(ItemName.c_str(), bSelected) && !bSelected)
			{
				AnimEditor.ChangePreviewMesh(Path);
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
			if (bSelected) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button("Restart"))
	{
		AnimEditor.RestartAnimation();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(Timeline controls come next)");

	if (!AnimEditor.GetLastPlayError().empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", AnimEditor.GetLastPlayError().c_str());
	}
}
