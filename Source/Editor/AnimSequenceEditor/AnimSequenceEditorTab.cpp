#include "EnginePCH.h"
#include "AnimSequenceEditorTab.h"
#include "AnimSequenceEditor.h"
#include "Panels/AnimSequenceInfoPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshViewportPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshSettingsPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/BoneHierarchyPanel.h"

namespace
{
	enum EPanelIndex { ViewportPanel, InfoPanel, HierarchyPanel, DetailsPanel, SettingsPanel };
}

FAnimSequenceEditorTab::FAnimSequenceEditorTab(FAnimSequenceEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, AnimEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 애셋별 도킹 ID를 유지합니다.
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("AnimSequenceDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("AnimSequencePanelClass", 0, AssetId);

	// 순서는 EPanelIndex와 같아야 합니다.
	Panels.Add(MakeUnique<FSkeletalMeshViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FAnimSequenceInfoPanel>(InEditor));
	Panels.Add(MakeUnique<FBoneHierarchyPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshSettingsPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FAnimSequenceEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("Viewer"))
	{
		if (ImGui::MenuItem("Close Viewer"))
		{
			AnimEditor.Close();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
		{
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, Panel->IsOpen()))
			{
				Panel->SetOpen(!Panel->IsOpen());
			}
		}
		ImGui::EndMenu();
	}
}

void FAnimSequenceEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	const ImGuiID RightId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.24f, nullptr, &ViewportId);

	// 오른쪽: 애니메이션 정보(위) → 본 계층 → 본 디테일 → 설정
	ImGuiID RemainingId = RightId;
	const ImGuiID InfoId = ImGui::DockBuilderSplitNode(RemainingId, ImGuiDir_Up, 0.30f, nullptr, &RemainingId);
	const ImGuiID HierarchyId = ImGui::DockBuilderSplitNode(RemainingId, ImGuiDir_Up, 0.40f, nullptr, &RemainingId);
	ImGuiID DetailsId = RemainingId;
	const ImGuiID SettingsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Down, 0.40f, nullptr, &DetailsId);

	ImGui::DockBuilderDockWindow(Panels[ViewportPanel]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[InfoPanel]->GetWindowName(), InfoId);
	ImGui::DockBuilderDockWindow(Panels[HierarchyPanel]->GetWindowName(), HierarchyId);
	ImGui::DockBuilderDockWindow(Panels[DetailsPanel]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[SettingsPanel]->GetWindowName(), SettingsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FAnimSequenceEditorTab::OnRender(bool bActive)
{
	AnimEditor.SetViewportState(0, 0, {});
	if (AnimEditor.IsCloseRequested())
	{
		return;
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 제거하고 기본 레이아웃으로 시작합니다.
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(DockSpaceId, Size);
			bLayoutInitialized = true;
		}

		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = PanelClassId;
		WindowClass.DockingAllowUnclassed = false;
		const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
		{
			ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
		}
	}

	for (auto& Panel : Panels)
	{
		// 분리된 패널은 부모 탭이 가려져도 독립적으로 표시합니다.
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
		}
	}
}
