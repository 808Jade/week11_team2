#include "EnginePCH.h"
#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/AssetEditor.h"
#include <filesystem>

FAssetEditorTab::FAssetEditorTab(FAssetEditor& InEditor) : Editor(InEditor)
{
	SetDisplayFilename(std::filesystem::path(Editor.GetAssetPath()).filename().string());
}

void FAssetEditorTab::SetDisplayFilename(const FString& Filename)
{
	TabName = Filename + " (" + Editor.GetEditorTypeName() + ")###AssetEditor:" + Editor.GetAssetPath();
}

void FAssetEditorTab::OnCloseRequested()
{
	Editor.Close();
}

void FAssetEditorTab::DrawMenuBar()
{
	// 기본 Editor Menu Bar.

	if (ImGui::BeginMenu("Editor"))
	{
		if (ImGui::MenuItem("Close Editor"))
		{
			Editor.Close();
		}

		ImGui::EndMenu();
	}
}

void FAssetEditorTab::OnRender(bool bActive)
{
}
