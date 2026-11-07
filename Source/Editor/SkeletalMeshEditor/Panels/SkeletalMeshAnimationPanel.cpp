#include "EnginePCH.h"
#include "SkeletalMeshAnimationPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Animation/AnimSequence.h"
#include "Asset/AssetManager.h"

#include <algorithm>
#include <cmath>

FSkeletalMeshAnimationPanel::FSkeletalMeshAnimationPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel{ InEditor, "Animation" } {
}

void FSkeletalMeshAnimationPanel::DrawContent() {
	if (ImGui::Button("Browse FBX...")) {
		if (Editor.BrowseAnimationFbx()) {
			mSelectedClip = 0;
		}
	}

	ImGui::SameLine();
	if (Editor.GetAnimationSourcePath().empty()) {
		ImGui::TextDisabled("Choose an FBX with a matching skeleton.");
	} else {
		ImGui::TextUnformatted(Editor.GetAnimationSourcePath().c_str());
	}

	const TArray<FString>& Names{ Editor.GetAnimationNames() };
	ImGui::BeginDisabled(Names.IsEmpty());
	mSelectedClip = Names.IsEmpty() ? 0 : std::clamp(mSelectedClip, 0, Names.Num() - 1);
	ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.4f, 260.0f));
	if (ImGui::BeginCombo("Clip", Names.IsEmpty() ? "No clips" : Names[mSelectedClip].c_str())) {
		for (int32 Index{}; Index < Names.Num(); ++Index) {
			ImGui::PushID(Index);
			if (ImGui::Selectable(Names[Index].c_str(), Index == mSelectedClip)) {
				mSelectedClip = Index;
			}

			ImGui::PopID();
		}

		ImGui::EndCombo();
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::DragFloat("Sample FPS", &mSampleRate, 1.0f, 1.0f, 240.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SameLine();
	if (ImGui::Button("Import & Play")) {
		Editor.ImportAnimation(mSelectedClip, mSampleRate);
	}

	ImGui::EndDisabled();
	const TArray<FString> Assets{ UAssetManager::GetAssetPaths(EAssetType::Animation) };
	ImGui::BeginDisabled(Assets.IsEmpty());
	ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x * 0.65f, 500.0f));
	const FString& SelectedAsset{ Editor.GetAnimationAssetPath() };
	if (ImGui::BeginCombo("Saved Animation", SelectedAsset.empty() ? "Select an animation asset" : SelectedAsset.c_str())) {
		for (const FString& Path : Assets) {
			if (ImGui::Selectable(Path.c_str(), Path == SelectedAsset)) {
				Editor.LoadAnimationAsset(Path);
			}
		}

		ImGui::EndCombo();
	}

	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Load Asset...")) {
		Editor.BrowseAnimationAsset();
	}

	ImGui::Separator();
	const UAnimSequence* Animation{ Editor.GetPreviewAnimation() };
	ImGui::BeginDisabled(!Animation);
	if (ImGui::Button("Play")) {
		Editor.PlayAnimation();
	}

	ImGui::SameLine();
	if (ImGui::Button("Pause")) {
		Editor.PauseAnimation();
	}

	ImGui::SameLine();
	if (ImGui::Button("Stop")) {
		Editor.StopAnimation();
	}

	ImGui::SameLine();
	bool Looping{ Editor.IsAnimationLooping() };
	if (ImGui::Checkbox("Loop", &Looping)) {
		Editor.SetAnimationLooping(Looping);
	}

	ImGui::SameLine();
	float PlayRate{ Editor.GetAnimationPlayRate() };
	ImGui::SetNextItemWidth(100.0f);
	if (ImGui::DragFloat("Speed", &PlayRate, 0.05f, -4.0f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) {
		Editor.SetAnimationPlayRate(PlayRate);
	}

	float Position{ Editor.GetAnimationPosition() };
	const float Duration{ Editor.GetAnimationDuration() };
	ImGui::BeginDisabled(Duration <= 0.0f);
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderFloat("##AnimationPosition", &Position, 0.0f, Duration, "%.3f s", ImGuiSliderFlags_AlwaysClamp)) {
		Editor.SeekAnimation(Position);
	}

	ImGui::EndDisabled();
	ImGui::EndDisabled();
	if (Animation) {
		ImGui::Text("%s | %s | %.3f / %.3f s | %d samples @ %.2f FPS", Editor.GetAnimationName().c_str(), Editor.IsAnimationPlaying() ? "Playing" : "Paused / Stopped", Editor.GetAnimationPosition(), Duration, Animation->GetNumSamples(), Animation->GetSampleRate());
	} else {
		ImGui::TextDisabled("Load an animation asset or import an FBX clip to preview.");
	}

	if (!Editor.GetAnimationError().empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 1.0f, 0.4f, 0.35f, 1.0f });
		ImGui::TextWrapped("%s", Editor.GetAnimationError().c_str());
		ImGui::PopStyleColor();
	}

	if (!Editor.GetAnimationWarnings().IsEmpty() && ImGui::CollapsingHeader("Import Warnings")) {
		for (const FString& Warning : Editor.GetAnimationWarnings()) {
			ImGui::TextWrapped("%s", Warning.c_str());
		}
	}
}
