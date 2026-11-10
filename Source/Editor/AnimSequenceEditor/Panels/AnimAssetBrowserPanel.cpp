#include "EnginePCH.h"
#include "AnimAssetBrowserPanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxPath.h"

#include <algorithm>
#include <cctype>

namespace
{
	FString ToLower(FString Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(),
			[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
		return Text;
	}
}

FAnimAssetBrowserPanel::FAnimAssetBrowserPanel(FAnimSequenceEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Asset Browser")
	, AnimEditor(InEditor)
{
}

void FAnimAssetBrowserPanel::DrawContent()
{
	// 검색창과 새로고침
	const float RefreshWidth = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SetNextItemWidth(-(RefreshWidth + ImGui::GetStyle().ItemSpacing.x));
	ImGui::InputTextWithHint("##Filter", "Search Assets", Filter, sizeof(Filter));
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		AnimEditor.RefreshCompatibleAnimations();
	}

	const TArray<FString>& Paths = AnimEditor.GetCompatibleAnimationPaths();
	const FString FilterText = ToLower(Filter);
	const FString CurrentKey = AnimEditor.GetSequencePath();

	constexpr ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
	if (!ImGui::BeginTable("##AnimAssets", 2, TableFlags))
	{
		return;
	}
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.6f);
	ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.4f);
	ImGui::TableHeadersRow();

	int32 Shown = 0;
	for (const FString& Path : Paths)
	{
		const FString Name = FbxPath::ToString(FbxPath::FromString(Path).stem());
		if (!FilterText.empty() && ToLower(Name).find(FilterText) == FString::npos)
		{
			continue;
		}
		++Shown;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::PushID(Path.c_str());
		const bool bCurrent = Path == CurrentKey;
		// 클릭하면 이 에디터의 애니메이션만 교체합니다. (새 탭을 열지 않음)
		if (ImGui::Selectable(Name.c_str(), bCurrent, ImGuiSelectableFlags_SpanAllColumns) && !bCurrent)
		{
			AnimEditor.ChangeAnimation(Path);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", Path.c_str());
		}
		ImGui::PopID();

		ImGui::TableSetColumnIndex(1);
		ImGui::TextDisabled("%s", FbxPath::ToString(FbxPath::FromString(Path).parent_path()).c_str());
	}
	ImGui::EndTable();

	if (Shown == 0)
	{
		ImGui::TextDisabled(Paths.IsEmpty() ? "No animations use this Skeleton." : "No matching assets.");
	}
}
