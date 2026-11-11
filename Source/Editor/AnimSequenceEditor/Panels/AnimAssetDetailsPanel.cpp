#include "EnginePCH.h"
#include "AnimAssetDetailsPanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimDataModel.h"
#include "Animation/Skeleton.h"
#include "Asset/FbxImporter/FbxPath.h"

FAnimAssetDetailsPanel::FAnimAssetDetailsPanel(FAnimSequenceEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Asset Details")
	, AnimEditor(InEditor)
{
}

void FAnimAssetDetailsPanel::DrawContent()
{
	const UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	if (!Sequence)
	{
		return;
	}

	// 애니메이션
	if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const FString Name = FbxPath::ToString(FbxPath::FromString(AnimEditor.GetSequencePath()).stem());
		ImGui::TextDisabled("Asset Name");
		ImGui::TextWrapped("%s", Name.c_str());
		ImGui::TextDisabled("Asset Path");
		ImGui::TextWrapped("%s", AnimEditor.GetSequencePath().c_str());
		ImGui::Spacing();

		if (const UAnimDataModel* Model = Sequence->GetDataModel())
		{
			ImGui::Text("Length: %.3f s", Model->GetPlayLength());
			ImGui::Text("Frames: %d", Model->GetNumberOfFrames());
			ImGui::Text("Frame Rate: %.2f fps", Model->GetFrameRate());
			ImGui::Text("Tracks: %d", static_cast<int>(Model->GetBoneAnimationTracks().Num()));
		}
	}

	// Skeleton
	if (ImGui::CollapsingHeader("Skeleton", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (const USkeleton* Skeleton = Sequence->GetSkeleton())
		{
			ImGui::TextWrapped("%s", Skeleton->GetPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("None");
		}
	}

	// UE의 Preview Mesh 선택: 같은 Skeleton을 쓰는 Skeletal Mesh만 고를 수 있습니다.
	if (ImGui::CollapsingHeader("Preview Mesh", ImGuiTreeNodeFlags_DefaultOpen))
	{
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
	}

	if (!AnimEditor.GetLastPlayError().empty())
	{
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", AnimEditor.GetLastPlayError().c_str());
	}
}
