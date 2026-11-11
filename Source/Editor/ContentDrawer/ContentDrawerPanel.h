#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <filesystem>
#include <functional>
#include <unordered_map>

enum class EFbxAssetType : uint32;

namespace fs = std::filesystem;

class FContentDrawerPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Content Drawer"; }

	// .widget 파일을 더블클릭하면 불린다 (UI 에디터가 연다).
	void SetOpenWidgetCallback(std::function<void(const FString&)> InCallback) { OnOpenWidget = std::move(InCallback); }
	void SetCreateWidgetCallback(std::function<void(const FString&)> InCallback) { OnCreateWidget = std::move(InCallback); }
	// OBJ 또는 베이크된 FBX 메시를 더블클릭하면 Viewer를 엽니다.
	void SetOpenMeshCallback(std::function<void(const FString&)> InCallback) { OnOpenMesh = std::move(InCallback); }

private:
	void RenderFolderPane();
	void RenderAssetPane();
	void RenderCreateWidgetPopup();
	void RenderAssetGrid();
	bool RenderAssetTile(const fs::path& Path, const FString& FilenameString,
		bool bIsDirectory, float TileSize, float FramePadding, bool& bTriedThumbnailLoad);
	void NavigateToFolder(const fs::path& Path);
	void RenderPathBar();
	void RenderFolderTree(const fs::path& Path, const fs::path& FolderToReveal);
	// .fbxbin의 에셋 타입(파일 헤더). 매 프레임 파일을 열지 않도록 수정 시각과 함께 캐시한다. 읽지 못하면 false.
	bool GetBakedAssetType(const fs::path& Path, const FString& PathString, EFbxAssetType& OutType);

	struct FBakedTypeEntry
	{
		fs::file_time_type WriteTime;
		bool bValid = false;
		EFbxAssetType Type{};
	};
	std::unordered_map<FString, FBakedTypeEntry> BakedTypeCache;

	std::function<void(const FString&)> OnOpenWidget;
	std::function<void(const FString&)> OnCreateWidget;
	std::function<void(const FString&)> OnOpenMesh;
	char NewAssetName[64] = "NewWidget";
	char SearchText[128] = {};
	bool bResetGridScroll = false;
	bool bRevealCurrentFolder = true;

	fs::path AssetRootPath = "Assets";
	fs::path CurrentPath = "Assets";

	const FString FolderIconPath = "Assets/Icons/Folder.png";
	const FString FileIconPath = "Assets/Icons/File.png";
	
};
