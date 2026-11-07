#include "EnginePCH.h"
#include "SkeletalMeshAnimationPanel.h"
#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"
#include "Asset/AssetManager.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectIterator.h"

FSkeletalMeshAnimationPanel::FSkeletalMeshAnimationPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Animation")
{
}

void FSkeletalMeshAnimationPanel::DrawClipCombo()
{
	UAnimationClip* Current = Editor.GetAnimationClip();

	ImGui::TextUnformatted("Clip");
	ImGui::SetNextItemWidth(-FLT_MIN);
	// 클립 이름은 파일끼리 겹칠 수 있어(mixamo.com 등) 경로로 구분합니다.
	if (!ImGui::BeginCombo("##AnimationClip", Current ? Current->GetPath().c_str() : "None"))
	{
		return;
	}

	if (ImGui::Selectable("None", Current == nullptr))
	{
		Editor.SetAnimationClip(nullptr);
	}

	for (TObjectIterator<UAnimationClip> Itr; Itr; ++Itr)
	{
		ImGui::PushID(*Itr);
		if (ImGui::Selectable(Itr->GetPath().c_str(), *Itr == Current))
		{
			Editor.SetAnimationClip(*Itr);
		}
		ImGui::PopID();
	}

	for (const FString& Path : UAssetManager::GetUnloadedAssetPaths(EAssetType::Animation))
	{
		ImGui::PushID(Path.c_str());
		if (ImGui::Selectable(Path.c_str(), false))
		{
			if (UAnimationClip* Loaded = UAssetManager::GetAssetByPath<UAnimationClip>(Path))
			{
				Editor.SetAnimationClip(Loaded);
			}
		}
		ImGui::PopID();
	}

	ImGui::EndCombo();
}

void FSkeletalMeshAnimationPanel::DrawContent()
{
	const USkeletalMesh* Mesh = Editor.GetMesh();
	const USkeleton* MeshSkeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
	if (MeshSkeleton)
	{
		// 경로가 없으면 예전 메시용으로 메모리에만 만든 스켈레톤입니다.
		ImGui::Text("Mesh Skeleton: %s", MeshSkeleton->GetPath().empty() ? "(not saved, reimport to create a file)" : MeshSkeleton->GetPath().c_str());
	}

	DrawClipCombo();

	UAnimationClip* Clip = Editor.GetAnimationClip();
	if (!Clip)
	{
		return;
	}

	const FAnimationClipData& Data = Clip->GetData();

	const USkeleton* ClipSkeleton = Clip->GetSkeleton();
	if (!ClipSkeleton)
	{
		ImGui::TextUnformatted("Clip Skeleton: unknown (reimport to record it)");
	}
	else if (Mesh && (ClipSkeleton == MeshSkeleton || ClipSkeleton->GetData().HasSameHierarchy(Mesh->GetRefSkeleton())))
	{
		ImGui::TextUnformatted("Clip Skeleton: same as mesh");
	}
	else
	{
		ImGui::Text("Clip Skeleton: different (%s)", ClipSkeleton->GetPath().c_str());
	}

	const int32 NumMatched = Editor.GetNumMatchedAnimationTracks();
	if (NumMatched == 0)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No clip track matches a bone of this mesh.");
	}
	ImGui::Text("Matched Tracks: %d / %d", NumMatched, Data.Tracks.Num());

	if (ImGui::Button(Editor.IsAnimationPlaying() ? "Pause" : "Play"))
	{
		Editor.SetAnimationPlaying(!Editor.IsAnimationPlaying());
	}

	ImGui::SameLine();
	float Time = Editor.GetAnimationTime();
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::SliderFloat("##AnimationTime", &Time, 0.0f, Data.Duration, "%.3f s"))
	{
		Editor.SetAnimationTime(Time);
	}
}
