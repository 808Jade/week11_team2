#pragma once

#include "Core/Delegate/Delegate.h"
#include "Editor/EditorUI/EditorTab.h"
#include "Editor/LevelEditor/LevelEditorToolbar.h"
#include "Asset/FbxImporter/FbxInfo.h"

class FLevelEditorTab : public IEditorTab
{
public:
	explicit FLevelEditorTab();

	const char* GetTabName() const override { return TabName.c_str(); }
	bool CanDetach() const override { return false; }
	void DrawMenuBar() override;

	TDelegate<> OnNewScene;
	TDelegate<> OnOpenScene;
	TDelegate<> OnSaveScene;
	TDelegate<> OnSaveSceneAs;
	TDelegate<> OnPackageGame;
	TDelegate<const FString&> OnLevelLoaded;

	void SetIsPackagingQuery(std::function<bool()> InQuery) { IsPackaging = std::move(InQuery); }

	FLevelEditorToolbar* GetToolbar() { return &Toolbar; }
	TArray<TUniquePtr<IEditorPanel>>& GetPanels() { return Panels; }

	template <typename T>
	T* AddPanel()
	{
		TUniquePtr<T> Panel = MakeUnique<T>();
		T* Result = Panel.get();
		Result->Init();
		Panels.Add(std::move(Panel));
		return Result;
	}

	void Tick(float DeltaTime);

	void OnRender(bool bActive) override;
	
	void SetDockSpaceOptions(bool bEnabled, bool bPassthru);

	void UpdateTabName(std::string_view InPath);

private:
	void DrawFbxImportDialog();
	FString FbxSourcePath;
	std::array<char, 4096> FbxOutputDirectory{};
	FFbxImportOptions FbxOptions;
	TArray<FString> FbxSkeletonKeys;
	FString FbxImportError;
	bool bOpenFbxImport = false;

	std::function<bool()> IsPackaging;
	FLevelEditorToolbar Toolbar;
	TArray<TUniquePtr<IEditorPanel>> Panels;
	bool bUseDockSpace = true;
	bool bPassthruCentralNode = false;

	FString TabName;
};
