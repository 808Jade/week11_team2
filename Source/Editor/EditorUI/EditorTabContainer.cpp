#include "EnginePCH.h"
#include "Editor/EditorUI/EditorTabContainer.h"

void FEditorTabContainer::AddTab(IEditorTab* Tab)
{
	if (!Tab)
	{
		return;
	}

	for (IEditorTab* Existing : Tabs)
	{
		if (Existing == Tab)
		{
			return;
		}
	}

	Tabs.Add(Tab);
}

void FEditorTabContainer::RemoveTab(IEditorTab* Tab)
{
	// 소유 객체는 UI 순회와 ImGui 렌더링이 끝난 뒤에만 파괴한다.
	assert(!bRendering);
	for (int32 Index = PendingCloseTabs.Num() - 1; Index >= 0; --Index)
	{
		if (PendingCloseTabs[Index] == Tab)
		{
			PendingCloseTabs.RemoveAt(Index);
		}
	}

	for (int32 Index = Tabs.Num() - 1; Index >= 0; --Index)
	{
		if (Tabs[Index] == Tab)
		{
			Tabs.RemoveAt(Index);
		}
	}

	if (MainSelectedTab == Tab)
	{
		MainSelectedTab = nullptr;
	}
	
	if (FocusedTab == Tab)
	{
		FocusedTab = nullptr;
	}

	if (PendingActivation == Tab)
	{
		PendingActivation = nullptr;
	}
}

void FEditorTabContainer::ActivateTab(IEditorTab* Tab)
{
	for (IEditorTab* Existing : Tabs)
	{
		if (Existing == Tab)
		{
			PendingActivation = Tab;
			return;
		}
	}
}

void FEditorTabContainer::RequestCloseTab(IEditorTab* Tab)
{
	bool bRegistered = false;
	for (IEditorTab* Existing : Tabs)
	{
		bRegistered |= Existing == Tab;
	}

	if (!bRegistered || !Tab->CanClose())
	{
		return;
	}

	for (IEditorTab* Existing : PendingCloseTabs)
	{
		if (Existing == Tab)
		{
			return;
		}
	}
	PendingCloseTabs.Add(Tab);
}

void FEditorTabContainer::ProcessCloseRequests()
{
	assert(!bRendering);
	TArray<IEditorTab*> Requests = std::move(PendingCloseTabs);
	PendingCloseTabs = {};
	for (IEditorTab* Tab : Requests)
	{
		RemoveTab(Tab);
		Tab->OnCloseRequested();
	}
}

void FEditorTabContainer::DrawMainMenuBar()
{
	if (ImGui::BeginMenuBar())
	{
		if (MainSelectedTab)
		{
			DrawTabMenu(*MainSelectedTab);
		}

		ImGui::EndMenuBar();
	}
}

void FEditorTabContainer::DrawTabs()
{
	assert(!bRendering);
	bRendering = true;

	// 일반 패널은 각 편집기의 내부 도킹 영역에서만 배치한다.
	ImGuiWindowClass TabClass;
	TabClass.ClassId = ImGui::GetID("EditorTabClass");
	TabClass.DockingAllowUnclassed = false;
	TabClass.DockNodeFlagsOverrideSet = 
		ImGuiDockNodeFlags_NoDockingSplit |
		ImGuiDockNodeFlags_NoWindowMenuButton;

	const ImGuiID DockSpaceId = ImGui::GetID("EditorTabs");
	ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f),
		ImGuiDockNodeFlags_NoDockingSplit |
		ImGuiDockNodeFlags_NoWindowMenuButton | 
		ImGuiDockNodeFlags_NoCloseButton, &TabClass);

	MainSelectedTab = nullptr;

	for (IEditorTab* Tab : Tabs)
	{
		const bool bCanDetach = Tab->CanDetach();
		ImGui::SetNextWindowClass(&TabClass);
		ImGui::SetNextWindowDockID(DockSpaceId, bCanDetach ? ImGuiCond_FirstUseEver : ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(960.0f, 640.0f), ImGuiCond_FirstUseEver);

		// NoUndocking은 같은 노드의 모든 탭에 적용되므로 창별 NoMove로 고정한다.
		const ImGuiWindow* ExistingWindow = ImGui::FindWindowByName(Tab->GetTabName());
		const bool bInMainDockSpace =
			!bCanDetach ||
			!ExistingWindow ||
			IsInDockSpace(ExistingWindow, DockSpaceId);

		ImGuiWindowFlags Flags = ImGuiWindowFlags_NoCollapse;
		if (!bInMainDockSpace)
		{
			Flags |= ImGuiWindowFlags_MenuBar;
		}

		if (!bCanDetach)
		{
			Flags |= ImGuiWindowFlags_NoMove;
		}

		
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
		const bool bActivate = PendingActivation == Tab;
		if (bActivate)
		{
			ImGui::SetNextWindowFocus();
		}
		bool bOpen = true;
		// 메인 영역의 탭은 메뉴에서 닫고, 분리된 탭만 닫기 버튼을 표시합니다.
		const bool bShowCloseButton = Tab->CanClose() && !bInMainDockSpace;
		const bool bVisible = ImGui::Begin(Tab->GetTabName(), Tab->CanClose() ? &bOpen : nullptr, Flags);
		ImGui::PopStyleVar(2);
		

		if (!bOpen)
		{
			RequestCloseTab(Tab);
			Tab->OnRender(false);
			ImGui::End();
			continue;
		}

		if (bActivate && bVisible)
		{
			ImGuiViewport* Viewport = ImGui::GetWindowViewport();
			if (Viewport->PlatformHandle && ImGui::GetPlatformIO().Platform_SetWindowFocus)
			{
				ImGui::GetPlatformIO().Platform_SetWindowFocus(Viewport);
			}

			PendingActivation = nullptr;
		}

		if (bVisible)
		{
			if (IsInDockSpace(ImGui::GetCurrentWindow(), DockSpaceId))
			{
				MainSelectedTab = Tab;
			}

			else if (ImGui::BeginMenuBar())
			{
				DrawTabMenu(*Tab);
				ImGui::EndMenuBar();
			}
		}

		// 숨겨진 탭도 내부 도킹 노드를 유지할 수 있도록 호출한다.
		Tab->OnRender(bVisible);
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_DockHierarchy))
		{
			FocusedTab = Tab;
		}
		ImGui::End();
	}
	bRendering = false;
}

bool FEditorTabContainer::IsInDockSpace(const ImGuiWindow* Window, ImGuiID DockSpaceId)
{
	return Window->DockNode && ImGui::DockNodeGetRootNode(Window->DockNode)->ID == DockSpaceId;
}

void FEditorTabContainer::DrawTabMenu(IEditorTab& Tab)
{
	// 탭 전환 시 이전 탭에서 열었던 메뉴 팝업을 다른 탭이 재사용하지 않는다.
	{
		ImGui::PushID(&Tab);
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 10.0f));
			Tab.DrawMenuBar();
			ImGui::PopStyleVar(2);
		}
		ImGui::PopID();
	}
}

void FEditorTabContainer::OnRender(bool bPassthruCentralNode)
{
	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(Viewport->WorkPos);
	ImGui::SetNextWindowSize(Viewport->WorkSize);
	ImGui::SetNextWindowViewport(Viewport->ID);

	ImGuiWindowFlags Flags = ImGuiWindowFlags_MenuBar
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus;

	if (bPassthruCentralNode)
		Flags |= ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	// 메뉴 높이를 먼저 확보하고 탭 선택이 확정된 후 내용을 그린다.
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 10.0f));
	ImGui::Begin("DockingSpace", nullptr, Flags);
	ImGui::PopStyleVar(4);

	DrawTabs();
	DrawMainMenuBar();
	ImGui::End();
	ProcessCloseRequests();
}
