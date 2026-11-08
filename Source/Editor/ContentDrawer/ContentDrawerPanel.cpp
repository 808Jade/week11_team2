#include "EnginePCH.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

#include <algorithm>

namespace
{
	constexpr ImGuiTreeNodeFlags FolderNodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_FramePadding;

	FString PathLabel(const fs::path& Path)
	{
		const auto Utf8 = Path.generic_u8string();
		return FString(Utf8.begin(), Utf8.end());
	}
}

bool FContentDrawerPanel::Init()
{
	return false;
}

void FContentDrawerPanel::Tick(float DeltaTime)
{
}

void FContentDrawerPanel::OnRender()
{
	if (ImGui::Begin("Content Drawer"))
	{
		std::error_code Error;
		if (!fs::is_directory(CurrentPath, Error))
		{
			NavigateToFolder(AssetRootPath);
		}

		Error.clear();
		if (fs::is_directory(AssetRootPath, Error))
		{
			RenderFolderPane();
			ImGui::SameLine();
			RenderAssetPane();
		}
		else
		{
			ImGui::TextUnformatted("Asset folder is unavailable.");
		}
	}
	ImGui::End();
}

void FContentDrawerPanel::RenderFolderPane()
{
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float PaneWidth = ImGui::GetContentRegionAvail().x - Style.ItemSpacing.x;
	const float MaxTreeWidth = ImMax(1.0f, PaneWidth * 0.65f);
	const float MinTreeWidth = ImClamp(MaxTreeWidth * 0.5f, 1.0f, 120.0f);
	const float TreeViewWidth = ImClamp(PaneWidth * 0.35f, MinTreeWidth, ImMin(200.0f, MaxTreeWidth));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(MinTreeWidth, Style.WindowMinSize.y));
	ImGui::SetNextWindowSizeConstraints(ImVec2(MinTreeWidth, 0.0f), ImVec2(MaxTreeWidth, FLT_MAX));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, Style.Colors[ImGuiCol_FrameBg]);
	ImGui::PushStyleColor(ImGuiCol_Border, Style.Colors[ImGuiCol_SeparatorHovered]);
	const bool bTreeVisible = ImGui::BeginChild("FolderTreeView", ImVec2(TreeViewWidth, 0),
		ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar();
	if (bTreeVisible)
	{
		ImGui::SeparatorText("Folders");
		ImGui::PushStyleColor(ImGuiCol_Header, ImLerp(Style.Colors[ImGuiCol_Header], Style.Colors[ImGuiCol_Button], 0.65f));
		const fs::path FolderToReveal = bRevealCurrentFolder ? CurrentPath : fs::path{};
		bRevealCurrentFolder = false;
		if (!FolderToReveal.empty())
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}
		const bool bRootOpen = ImGui::TreeNodeEx(AssetRootPath.string().c_str(),
			FolderNodeFlags | ImGuiTreeNodeFlags_DefaultOpen |
			(AssetRootPath == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0));
		if (FolderToReveal == AssetRootPath)
		{
			ImGui::SetScrollY(0.0f);
		}
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			NavigateToFolder(AssetRootPath);
		}
		if (bRootOpen)
		{
			RenderFolderTree(AssetRootPath, FolderToReveal);
			ImGui::TreePop();
		}
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();
}

void FContentDrawerPanel::RenderAssetPane()
{
	ImGui::BeginGroup();
	ImGui::SeparatorText("Location");
	RenderPathBar();
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputTextWithHint("##AssetSearch", "Search this folder", SearchText, sizeof(SearchText),
		ImGuiInputTextFlags_EscapeClearsAll))
	{
		bResetGridScroll = true;
	}
	ImGui::Spacing();
	if (bResetGridScroll)
	{
		ImGui::SetNextWindowScroll(ImVec2(-1.0f, 0.0f));
		bResetGridScroll = false;
	}

	if (ImGui::BeginChild("AssetGridView"))
	{
		RenderCreateWidgetPopup();
		RenderAssetGrid();
	}
	ImGui::EndChild();
	ImGui::EndGroup();
}

void FContentDrawerPanel::RenderCreateWidgetPopup()
{
	bool bOpenCreateWidget = false;
	// 빈 곳 우클릭 (항목 위 우클릭은 제외)
	if (ImGui::BeginPopupContextWindow("##DrawerContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Create Widget"))
		{
			strcpy_s(NewAssetName, "NewWidget");
			bOpenCreateWidget = true;
		}
		ImGui::EndPopup();
	}
	if (bOpenCreateWidget)
	{
		ImGui::OpenPopup("Create Widget");
	}

	if (!ImGui::BeginPopupModal("Create Widget", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		return;
	}

	ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
	const fs::path Target = CurrentPath / (FString(NewAssetName) + ".widget");
	const bool bExists = fs::exists(Target);
	if (bExists)
	{
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "A file with this name already exists.");
	}

	ImGui::BeginDisabled(bExists || NewAssetName[0] == '\0');
	if (ImGui::Button("Create") && OnCreateWidget)
	{
		OnCreateWidget(Target.generic_string());
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void FContentDrawerPanel::RenderAssetGrid()
{
	const ImGuiStyle& Style = ImGui::GetStyle();
	constexpr float ThumbnailSize = 80.0f;
	const float FramePadding = ImMax(Style.FramePadding.x, Style.FramePadding.y);
	const float TileSize = ThumbnailSize + FramePadding * 2.0f;
	const float CellWidth = TileSize + Style.CellPadding.x * 2.0f;
	const int ColumnCount = ImMax(1, static_cast<int>(ImGui::GetContentRegionAvail().x / CellWidth));
	if (!ImGui::BeginTable("AssetGridTable", ColumnCount))
	{
		return;
	}

	ImGui::PushStyleColor(ImGuiCol_Button, Style.Colors[ImGuiCol_Header]);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Style.Colors[ImGuiCol_HeaderHovered]);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Style.Colors[ImGuiCol_HeaderActive]);

	const bool bSearching = SearchText[0] != '\0';
	bool bFoundEntry = false;
	fs::path OpenFolder;
	std::error_code IterateError;
	fs::directory_iterator It(CurrentPath, IterateError);
	const fs::directory_iterator End;

	// 프레임당 썸네일 로드 시도 한 건
	bool bTriedThumbnailLoad = false;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Path = It->path();

		const FString FilenameString = PathLabel(Path.filename());
		if (bSearching && !ImStristr(FilenameString.c_str(), nullptr, SearchText, nullptr))
			continue;

		std::error_code TypeError;
		const bool bIsDirectory = fs::is_directory(Path, TypeError);

		if (TypeError)
			continue;

		bFoundEntry = true;
		ImGui::TableNextColumn();
		if (RenderAssetTile(Path, FilenameString, bIsDirectory, TileSize, FramePadding, bTriedThumbnailLoad))
		{
			OpenFolder = Path;
		}
	}
	ImGui::PopStyleColor(3);
	ImGui::EndTable();

	if (!OpenFolder.empty())
	{
		NavigateToFolder(OpenFolder);
	}
	else if (IterateError)
	{
		ImGui::TextDisabled("Could not read this folder.");
	}
	else if (bSearching && !bFoundEntry)
	{
		ImGui::TextDisabled("No matching items.");
	}
}

bool FContentDrawerPanel::RenderAssetTile(const fs::path& Path, const FString& FilenameString,
	bool bIsDirectory, float TileSize, float FramePadding, bool& bTriedThumbnailLoad)
{
	const FString PathString = Path.generic_string();

	ImGui::PushID(PathString.c_str()); // 각 위젯에 고유 ID 부여
	UTexture2D* Thumbnail = nullptr;

	// 드래그에 사용하는 실제 에셋과 표시용 썸네일을 구분한다.
	UTexture2D* TextureAsset = nullptr;
	UFont* FontAsset = nullptr;

	if (bIsDirectory)
	{
		Thumbnail = UAssetManager::GetAssetByPath<UTexture2D>(FolderIconPath);
	}
	else
	{
		URenderAsset* Asset = UAssetManager::FindLoadedAsset(PathString);
		if (!Asset && !bTriedThumbnailLoad)
		{
			// 메시는 썸네일로 쓰지 않으므로 드로어를 연 것만으로 로드하지 않는다
			const EAssetType UnloadedType = UAssetManager::GetUnloadedAssetType(PathString);
			if (UnloadedType == EAssetType::Texture || UnloadedType == EAssetType::Font)
			{
				Asset = UAssetManager::GetAssetByPath<URenderAsset>(PathString);
				bTriedThumbnailLoad = true;
			}
		}

		if (Asset && Asset->IsA<UTexture2D>())
		{
			TextureAsset = Cast<UTexture2D>(Asset);
			Thumbnail = TextureAsset;
		}
		else if (Asset && Asset->IsA<UFont>())
		{
			FontAsset = Cast<UFont>(Asset);

			// 폰트는 글리프 아틀라스를 그대로 미리보기로 쓴다
			Thumbnail = FontAsset->AtlasTexture;
		}

		if (Thumbnail == nullptr)
		{
			Thumbnail = UAssetManager::GetAssetByPath<UTexture2D>(FileIconPath);
		}
	}

	const float AvailableWidth = ImGui::GetContentRegionAvail().x;
	const float ButtonSize = ImClamp(AvailableWidth, 1.0f, TileSize);
	const float TileOffset = ImMax(0.0f, (AvailableWidth - ButtonSize) * 0.5f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + TileOffset);
	ImGui::Button("##Thumbnail", ImVec2(ButtonSize, ButtonSize));

	ImVec2 ImageSize(0.0f, 0.0f);
	const FTexture2D* Resource = Thumbnail ? Thumbnail->GetResource() : nullptr;
	const ImTextureID TextureId = Resource ? (ImTextureID)Resource->GetSRV() : ImTextureID_Invalid;
	if (TextureId != ImTextureID_Invalid && Thumbnail->GetWidth() > 0 && Thumbnail->GetHeight() > 0)
	{
		const float Width = static_cast<float>(Thumbnail->GetWidth());
		const float Height = static_cast<float>(Thumbnail->GetHeight());
		const float Inset = ImMin(FramePadding, ButtonSize * 0.25f);
		const float Scale = (ButtonSize - Inset * 2.0f) / ImMax(Width, Height);
		ImageSize = ImVec2(Width * Scale, Height * Scale);
		if (ImGui::IsItemVisible())
		{
			const ImVec2 ButtonMin = ImGui::GetItemRectMin();
			const ImVec2 ImageMin(ButtonMin.x + (ButtonSize - ImageSize.x) * 0.5f,
				ButtonMin.y + (ButtonSize - ImageSize.y) * 0.5f);
			ImGui::GetWindowDrawList()->AddImage(TextureId, ImageMin,
				ImVec2(ImageMin.x + ImageSize.x, ImageMin.y + ImageSize.y));
		}
	}

	// 페이로드에 포인터를 그대로 실어서 받는 쪽이 AssetMap을 다시 뒤지지 않게 한다.
	if (TextureAsset && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Texture, &TextureAsset, sizeof(UTexture2D*));

		// 끌고 다니는 동안 커서를 따라다니는 미리보기
		if (ImageSize.x > 0.0f && ImageSize.y > 0.0f)
		{
			ImGui::Image(TextureId, ImageSize);
		}
		ImGui::TextUnformatted(FilenameString.c_str());

		ImGui::EndDragDropSource();
	}
	if (FontAsset && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Font, &FontAsset, sizeof(UFont*));

		ImGui::TextUnformatted(FilenameString.c_str());

		ImGui::EndDragDropSource();
	}

	const bool bOpenItem = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	if (bOpenItem && !bIsDirectory)
	{
		// TODO: UIEditor를 FAssetEditor 계열로 통합
		if (Path.extension() == ".widget" && OnOpenWidget)
		{
			OnOpenWidget(PathString);
		}
		else if (OnOpenMesh)
		{
			EFbxAssetType Type;
			const bool bFbxMesh = FFbxAssetBake::IsBakedPath(PathString) && FFbxAssetBake::ReadAssetType(PathString, Type)
				&& (Type == EFbxAssetType::StaticMesh || Type == EFbxAssetType::SkeletalMesh);
			if (Path.extension() == ".obj" || bFbxMesh) OnOpenMesh(PathString);
		}
	}

	// 셀 전체 폭에 파일 이름 표시
	const float LabelWidth = ImMax(1.0f, AvailableWidth);
	ImGui::Dummy(ImVec2(LabelWidth, ImGui::GetTextLineHeight()));
	if (ImGui::IsItemVisible())
	{
		const char* Filename = FilenameString.c_str();
		const ImVec2 TextSize = ImGui::CalcTextSize(Filename);
		ImVec2 LabelMin = ImGui::GetItemRectMin();
		const ImVec2 LabelMax = ImGui::GetItemRectMax();
		LabelMin.x += ImMax(0.0f, (LabelWidth - TextSize.x) * 0.5f);
		ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), LabelMin, LabelMax, LabelMax.x,
			Filename, Filename + FilenameString.size(), &TextSize);
		if (TextSize.x > LabelWidth)
		{
			ImGui::SetItemTooltip("%s", Filename);
		}
	}

	ImGui::PopID();
	return bOpenItem && bIsDirectory;
}

void FContentDrawerPanel::NavigateToFolder(const fs::path& Path)
{
	bRevealCurrentFolder = true;
	if (CurrentPath != Path)
	{
		CurrentPath = Path;
		SearchText[0] = '\0';
		bResetGridScroll = true;
	}
}

void FContentDrawerPanel::RenderPathBar()
{
	const fs::path DisplayedPath = CurrentPath;
	const ImGuiStyle& Style = ImGui::GetStyle();
	const ImVec4 CurrentFolderColor = Style.Colors[ImGuiCol_Button];
	const ImVec4 FolderButtonColor = Style.Colors[ImGuiCol_Header];
	const float Gap = Style.ItemInnerSpacing.x;
	const float SeparatorWidth = ImGui::CalcTextSize("/").x + Gap;

	ImGui::PushStyleColor(ImGuiCol_Button, FolderButtonColor);
	ImGui::BeginDisabled(DisplayedPath == AssetRootPath);
	if (ImGui::ArrowButton("##ParentFolder", ImGuiDir_Left))
	{
		NavigateToFolder(DisplayedPath.parent_path());
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Go to parent folder");
	ImGui::PopStyleColor();

	fs::path FolderPath;
	for (const fs::path& Part : DisplayedPath)
	{
		const bool bRoot = FolderPath.empty();
		FolderPath /= Part;
		const FString Label = PathLabel(Part);
		const float ButtonWidth = ImGui::CalcTextSize(Label.c_str()).x + Style.FramePadding.x * 2.0f;
		const float PrefixWidth = bRoot ? 0.0f : SeparatorWidth;
		ImGui::SameLine(0.0f, bRoot ? Style.ItemSpacing.x : Gap);
		if (PrefixWidth + ButtonWidth > ImGui::GetContentRegionAvail().x)
		{
			ImGui::NewLine();
		}
		if (!bRoot && SeparatorWidth < ImGui::GetContentRegionAvail().x)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("/");
			ImGui::SameLine(0.0f, Gap);
		}

		const float AvailableWidth = ImMax(1.0f, ImGui::GetContentRegionAvail().x);
		const bool bCurrentFolder = FolderPath == DisplayedPath;
		ImGui::PushID(PathLabel(FolderPath).c_str());
		ImGui::PushStyleColor(ImGuiCol_Button, bCurrentFolder ? CurrentFolderColor : FolderButtonColor);
		if (ImGui::Button(Label.c_str(), ImVec2(ImMin(AvailableWidth, ButtonWidth), 0.0f)))
		{
			NavigateToFolder(FolderPath);
		}
		ImGui::PopStyleColor();
		if (ButtonWidth > AvailableWidth)
		{
			ImGui::SetItemTooltip("%s", Label.c_str());
		}
		ImGui::PopID();
	}
}

void FContentDrawerPanel::RenderFolderTree(const fs::path& Path, const fs::path& FolderToReveal)
{
	std::error_code IterateError;
	fs::directory_iterator It(Path, IterateError);
	const fs::directory_iterator End;

	for (; !IterateError && It != End; It.increment(IterateError))
	{
		const fs::path Entry = It->path();

		std::error_code TypeError;
		const bool bIsDirectory =
			fs::is_directory(Entry, TypeError);

		if (TypeError || !bIsDirectory)
			continue;

		const FString FilenameString = PathLabel(Entry.filename());

		const ImGuiTreeNodeFlags Flags = FolderNodeFlags | (Entry == CurrentPath ? ImGuiTreeNodeFlags_Selected : 0);

		// 이동한 폴더의 경로만 한 번 펼치고 이후에는 트리의 토글 상태를 사용한다.
		if (!FolderToReveal.empty() &&
			std::mismatch(Entry.begin(), Entry.end(), FolderToReveal.begin(), FolderToReveal.end()).first == Entry.end())
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		}
		const bool bIsOpen = ImGui::TreeNodeEx(FilenameString.c_str(), Flags);
		if (Entry == FolderToReveal && !ImGui::IsItemVisible())
		{
			ImGui::SetScrollHereY();
		}
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			NavigateToFolder(Entry);
		}

		if (bIsOpen)
		{
			RenderFolderTree(Entry, FolderToReveal);
			ImGui::TreePop();
		}
	}

	if (IterateError)
	{
		ImGui::TextUnformatted("Could not read this folder.");
	}
}
