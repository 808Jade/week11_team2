#include "EnginePCH.h"
#include "AnimSequenceEditorTab.h"
#include "AnimSequenceEditor.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Panels/AnimAssetDetailsPanel.h"
#include "Panels/AnimAssetBrowserPanel.h"
#include "Panels/AnimTimelinePanel.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshViewportPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshSettingsPanel.h"
#include "Editor/SkeletalMeshEditor/Panels/BoneHierarchyPanel.h"

namespace
{
	// 'Details' 등은 다른 네임스페이스와 이름이 겹치므로 Panel 접미사를 붙입니다.
	enum EPanelIndex
	{
		ViewportPanel,
		SkeletonTreePanel,
		AssetDetailsPanel,
		TimelinePanel,
		BoneDetailsPanel,
		SettingsPanel,
		AssetBrowserPanel
	};
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
	Panels.Add(MakeUnique<FBoneHierarchyPanel>(InEditor));
	Panels.Add(MakeUnique<FAnimAssetDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FAnimTimelinePanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FSkeletalMeshSettingsPanel>(InEditor));
	Panels.Add(MakeUnique<FAnimAssetBrowserPanel>(InEditor));
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
		if (ImGui::MenuItem("Save Notifies", "Ctrl+S", false, AnimEditor.HasUnsavedNotifies()))
		{
			AnimEditor.SaveNotifies();
		}
		ImGui::Separator();
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
	// UE Animation 에디터 배치:
	//   왼쪽: Skeleton Tree | Asset Details (탭)
	//   가운데: Viewport(위) / Timeline(아래)
	//   오른쪽: Details | Settings (위, 탭) / Asset Browser (아래)
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	ImGuiID CenterId = InDockSpaceId;
	const ImGuiID LeftId = ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Left, 0.22f, nullptr, &CenterId);
	const ImGuiID RightId = ImGui::DockBuilderSplitNode(CenterId, ImGuiDir_Right, 0.26f, nullptr, &CenterId);

	ImGuiID ViewportId = CenterId;
	const ImGuiID TimelineId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Down, 0.28f, nullptr, &ViewportId);

	ImGuiID RightTopId = RightId;
	const ImGuiID BrowserId = ImGui::DockBuilderSplitNode(RightTopId, ImGuiDir_Down, 0.45f, nullptr, &RightTopId);

	ImGui::DockBuilderDockWindow(Panels[SkeletonTreePanel]->GetWindowName(), LeftId);
	ImGui::DockBuilderDockWindow(Panels[AssetDetailsPanel]->GetWindowName(), LeftId);
	ImGui::DockBuilderDockWindow(Panels[ViewportPanel]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[TimelinePanel]->GetWindowName(), TimelineId);
	ImGui::DockBuilderDockWindow(Panels[BoneDetailsPanel]->GetWindowName(), RightTopId);
	ImGui::DockBuilderDockWindow(Panels[SettingsPanel]->GetWindowName(), RightTopId);
	ImGui::DockBuilderDockWindow(Panels[AssetBrowserPanel]->GetWindowName(), BrowserId);

	// 탭으로 묶인 노드에서 처음 보일 탭: 왼쪽은 Asset Details, 오른쪽 위는 Details (UE 기본 배치와 같게)
	if (ImGuiDockNode* LeftNode = ImGui::DockBuilderGetNode(LeftId))
	{
		LeftNode->SelectedTabId = ImHashStr(Panels[AssetDetailsPanel]->GetWindowName());
	}
	if (ImGuiDockNode* RightTopNode = ImGui::DockBuilderGetNode(RightTopId))
	{
		RightTopNode->SelectedTabId = ImHashStr(Panels[BoneDetailsPanel]->GetWindowName());
	}
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FAnimSequenceEditorTab::OnRender(bool bActive)
{
	AnimEditor.SetViewportState(0, 0, {});
	if (DisplayedSequencePath != AnimEditor.GetSequencePath())
	{
		DisplayedSequencePath = AnimEditor.GetSequencePath();
		SetDisplayFilename(FbxPath::ToString(FbxPath::FromString(DisplayedSequencePath).filename()));
	}
	if (AnimEditor.IsCloseRequested())
	{
		return;
	}

	// Ctrl+S: 이 탭이 보이는 동안 Notify를 저장합니다. 글자 입력 중에는 입력에 양보합니다.
	const ImGuiIO& IO = ImGui::GetIO();
	if (bActive && IO.KeyCtrl && !IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_S, false) && AnimEditor.HasUnsavedNotifies())
	{
		AnimEditor.SaveNotifies();
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
