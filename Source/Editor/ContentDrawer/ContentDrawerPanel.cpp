#include "EnginePCH.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace1D.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Editor/AnimationEditor/FAnimationAssetPicker.h"
#include "Editor/ContentDrawer/ContentDrawerPanel.h"
#include "Asset/AssetManager.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Text/Font.h"
#include "Editor/HitoriEd/EditorDragDrop.h"

#include <algorithm>
#include <cstring>

namespace
{
	constexpr ImGuiTreeNodeFlags FolderNodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_FramePadding;

	FString PathLabel(const fs::path& Path)
	{
		const auto Utf8 = Path.generic_u8string();
		return FString(Utf8.begin(), Utf8.end());
	}

	// UE Content Browser처럼 에셋 타입을 썸네일 아래 색 띠로 구분한다.
	struct FAssetTypeStyle
	{
		const char* Label;
		ImU32 Color;
	};

	bool GetAssetTypeStyle(EFbxAssetType Type, FAssetTypeStyle& Out)
	{
		switch (Type)
		{
		case EFbxAssetType::SkeletalMesh: Out = { "Skeletal Mesh", IM_COL32(236, 105, 199, 255) }; return true; // 핑크
		case EFbxAssetType::Skeleton:     Out = { "Skeleton", IM_COL32(105, 200, 245, 255) }; return true;      // 하늘색
		case EFbxAssetType::AnimSequence: Out = { "Animation", IM_COL32(130, 220, 80, 255) }; return true;      // 연두색
		default: return false;
		}
	}

	// 타입 띠가 이미 타입을 알려 주므로, 표시 이름에서 접두사(AS_, SK_ 등)와 .fbxbin을 뺀다. 실제 파일 이름은 그대로다.
	FString MakeTileDisplayName(const FString& Filename, bool bHasTypeStyle)
	{
		if (!bHasTypeStyle)
		{
			return Filename;
		}

		FString Name = Filename;
		constexpr const char* Extension = ".fbxbin";
		constexpr size_t ExtensionLength = 7;
		if (Name.size() > ExtensionLength && Name.compare(Name.size() - ExtensionLength, ExtensionLength, Extension) == 0)
		{
			Name.resize(Name.size() - ExtensionLength);
		}
		for (const char* Prefix : { "SKEL_", "AS_", "SK_" })
		{
			const size_t Length = std::strlen(Prefix);
			if (Name.size() > Length && Name.compare(0, Length, Prefix) == 0)
			{
				Name.erase(0, Length);
				break;
			}
		}
		return Name;
	}

	// UTF-8 문자 경계 단위로 앞/뒤로 한 글자 이동한다.
	const char* NextChar(const char* It, const char* End)
	{
		if (It >= End) return End;
		++It;
		while (It < End && (static_cast<unsigned char>(*It) & 0xC0) == 0x80) ++It;
		return It;
	}

	const char* PrevChar(const char* Begin, const char* It)
	{
		if (It <= Begin) return Begin;
		--It;
		while (It > Begin && (static_cast<unsigned char>(*It) & 0xC0) == 0x80) --It;
		return It;
	}

	// Begin부터 Width 안에 들어가는 가장 긴 앞부분의 끝을 돌려준다. (최소 한 글자)
	const char* FitPrefix(const char* Begin, const char* End, float Width)
	{
		const char* Fit = NextChar(Begin, End);
		for (const char* It = Fit; It < End;)
		{
			const char* Next = NextChar(It, End);
			if (ImGui::CalcTextSize(Begin, Next).x > Width) break;
			Fit = It = Next;
		}
		return Fit;
	}

	// End에서 거꾸로 Width 안에 들어가는 가장 긴 뒷부분의 시작을 돌려준다.
	const char* FitSuffix(const char* Begin, const char* End, float Width)
	{
		const char* Fit = End;
		while (Fit > Begin)
		{
			const char* Prev = PrevChar(Begin, Fit);
			if (ImGui::CalcTextSize(Prev, End).x > Width) break;
			Fit = Prev;
		}
		return Fit;
	}

	// 이름을 최대 두 줄로 그린다. 두 줄에도 넘치면 가운데를 생략해서 앞부분(1줄)과 뒷부분(2줄)을 보여 준다.
	// 반환값: 이름 전체를 다 보여 줬는지
	bool DrawTwoLineName(ImDrawList* DrawList, const ImVec2& Min, float Width, const FString& Name)
	{
		const char* Begin = Name.c_str();
		const char* End = Begin + Name.size();
		const float LineHeight = ImGui::GetTextLineHeight();
		const ImU32 Color = ImGui::GetColorU32(ImGuiCol_Text);

		auto DrawLine = [&](int32 Line, const char* LineBegin, const char* LineEnd, const char* Prefix)
		{
			const float PrefixWidth = Prefix ? ImGui::CalcTextSize(Prefix).x : 0.0f;
			const float TextWidth = PrefixWidth + ImGui::CalcTextSize(LineBegin, LineEnd).x;
			ImVec2 Pos(Min.x + ImMax(0.0f, (Width - TextWidth) * 0.5f), Min.y + LineHeight * Line);
			if (Prefix)
			{
				DrawList->AddText(Pos, Color, Prefix);
				Pos.x += PrefixWidth;
			}
			DrawList->AddText(Pos, Color, LineBegin, LineEnd);
		};

		DrawList->PushClipRect(Min, ImVec2(Min.x + Width, Min.y + LineHeight * 2.0f), true);
		bool bComplete = true;
		if (ImGui::CalcTextSize(Begin, End).x <= Width)
		{
			DrawLine(0, Begin, End, nullptr);
		}
		else
		{
			const char* FirstEnd = FitPrefix(Begin, End, Width);
			DrawLine(0, Begin, FirstEnd, nullptr);
			if (ImGui::CalcTextSize(FirstEnd, End).x <= Width)
			{
				DrawLine(1, FirstEnd, End, nullptr);
			}
			else
			{
				constexpr const char* Ellipsis = "...";
				const char* TailBegin = FitSuffix(FirstEnd, End, Width - ImGui::CalcTextSize(Ellipsis).x);
				DrawLine(1, TailBegin, End, Ellipsis);
				bComplete = false;
			}
		}
		DrawList->PopClipRect();
		return bComplete;
	}
}

bool FContentDrawerPanel::GetBakedAssetType(const fs::path& Path, const FString& PathString, EFbxAssetType& OutType)
{
	if (!FFbxAssetBake::IsBakedPath(PathString)) return false;

	std::error_code Ec;
	const fs::file_time_type WriteTime = fs::last_write_time(Path, Ec);
	if (Ec) return false;

	FBakedTypeEntry& Entry = BakedTypeCache[PathString];
	if (Entry.WriteTime != WriteTime)
	{
		// 새로 만들어졌거나 다시 임포트된 파일만 헤더를 읽는다.
		Entry.WriteTime = WriteTime;
		Entry.bValid = FFbxAssetBake::ReadAssetType(PathString, Entry.Type);
	}
	OutType = Entry.Type;
	return Entry.bValid;
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
		RenderCreateAssetPopup();
		RenderAssetGrid();
	}
	ImGui::EndChild();
	ImGui::EndGroup();
}

void FContentDrawerPanel::RenderCreateAssetPopup() {
    // 에셋 종류별 기본 이름을 정한 뒤 공통 이름 입력 및 중복 검사 창으로 연결한다.
    bool OpenCreate{};
    if (ImGui::BeginPopupContextWindow("##DrawerContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Widget")) {
            strcpy_s(NewAssetName, "NewWidget");
            mCreateAssetExtension = ".widget";
            OpenCreate = true;
        }
        if (ImGui::MenuItem("Create Anim Graph")) {
            strcpy_s(NewAssetName, "NewAnimGraph");
            mCreateAssetExtension = ".animgraph";
            OpenCreate = true;
        }
        if (ImGui::MenuItem("Create Blend Space 1D")) {
            strcpy_s(NewAssetName, "NewBlendSpace");
            mCreateAssetExtension = ".blendspace";
            OpenCreate = true;
        }
        ImGui::EndPopup();
    }
    if (OpenCreate) {
        mCreateAssetError.clear();
        ImGui::OpenPopup("Create Asset");
    }

    if (!ImGui::BeginPopupModal("Create Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::InputText("Name", NewAssetName, sizeof(NewAssetName));
    const FString Name{NewAssetName};
    const fs::path Target{CurrentPath / FbxPath::FromString(Name + mCreateAssetExtension)};
    std::error_code Error{};
    const bool Exists{fs::exists(Target, Error)};
    if (Exists) {
        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "A file with this name already exists.");
    }
    if (!mCreateAssetError.empty()) {
        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", mCreateAssetError.c_str());
    }

    ImGui::BeginDisabled(Exists || Error || Name.empty() || FbxPath::FileName(Name) != Name);
    if (ImGui::Button("Create")) {
        const FString Path{FbxPath::ToString(Target)};
        if (mCreateAssetExtension == ".widget") {
            if (OnCreateWidget) {
                OnCreateWidget(Path);
            }
            ImGui::CloseCurrentPopup();
        } else {
            bool Created{};
            if (mCreateAssetExtension == ".animgraph") {
                std::unique_ptr<UAnimGraph> Asset{NewObject<UAnimGraph>()};
                Created = UAssetManager::SaveAnimGraph(Path, *Asset, mCreateAssetError);
            } else {
                std::unique_ptr<UBlendSpace1D> Asset{NewObject<UBlendSpace1D>()};
                Created = UAssetManager::SaveBlendSpace(Path, *Asset, mCreateAssetError);
            }
            if (Created) {
                if (OnOpenMesh) {
                    OnOpenMesh(Path);
                }
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
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
	const bool bThumbnailHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);

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

	// 베이크된 FBX 에셋은 타입별 색 띠와 타입 이름을 썸네일 아래쪽에 겹쳐 그린다.
	EFbxAssetType BakedType;
	FAssetTypeStyle TypeStyle;
	const bool bHasTypeStyle = !bIsDirectory && GetBakedAssetType(Path, PathString, BakedType)
		&& GetAssetTypeStyle(BakedType, TypeStyle);
	if (bHasTypeStyle && ImGui::IsItemVisible())
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 ButtonMin = ImGui::GetItemRectMin();
		const ImVec2 ButtonMax = ImGui::GetItemRectMax();
		constexpr float StripHeight = 4.0f;
		const float LabelHeight = ImGui::GetTextLineHeight();

		const ImVec2 StripMin(ButtonMin.x, ButtonMax.y - StripHeight);
		const ImVec2 LabelMin(ButtonMin.x, StripMin.y - LabelHeight);
		DrawList->AddRectFilled(LabelMin, ImVec2(ButtonMax.x, StripMin.y), IM_COL32(20, 20, 20, 190));
		DrawList->AddRectFilled(StripMin, ButtonMax, TypeStyle.Color);

		const ImVec2 TextSize = ImGui::CalcTextSize(TypeStyle.Label);
		const ImVec2 TextPos(ButtonMin.x + ImMax(2.0f, (ButtonMax.x - ButtonMin.x - TextSize.x) * 0.5f), LabelMin.y);
		DrawList->PushClipRect(LabelMin, ImVec2(ButtonMax.x, StripMin.y), true);
		DrawList->AddText(TextPos, TypeStyle.Color, TypeStyle.Label);
		DrawList->PopClipRect();
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

    if (!bIsDirectory) {
        const auto Extension{Path.extension()};
        // 타입별 페이로드를 사용해 애니메이션 선택기와 Blend Space 캔버스에 드롭할 수 있게 한다.
        const char* PayloadType{};
        if (Extension == L".animgraph") {
            PayloadType = AnimGraphPayload;
        } else if (Extension == L".blendspace") {
            PayloadType = BlendSpacePayload;
        } else if (UAssetManager::GetUnloadedAssetType(PathString) == EAssetType::AnimSequence || Cast<UAnimSequenceBase>(UAssetManager::FindLoadedAsset(PathString))) {
            PayloadType = AnimSequencePayload;
        }
        if (PayloadType && ImGui::BeginDragDropSource()) {
            const FString DragPath{FbxPath::ToString(Path)};
            ImGui::SetDragDropPayload(PayloadType, DragPath.c_str(), DragPath.size() + 1);
            ImGui::TextUnformatted(FilenameString.c_str());
            ImGui::EndDragDropSource();
        }
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
			// 메시 Viewer와 Animation 에디터 모두 같은 콜백(EditorManager::OpenEditor)으로 엽니다.
			EFbxAssetType Type;
			const bool bFbxOpenable = GetBakedAssetType(Path, PathString, Type)
				&& (Type == EFbxAssetType::StaticMesh || Type == EFbxAssetType::SkeletalMesh || Type == EFbxAssetType::AnimSequence);
			if (Path.extension() == ".obj" || bFbxOpenable || Path.extension() == ".animgraph" || Path.extension() == ".blendspace") OnOpenMesh(PathString);
		}
	}

	// 셀 전체 폭에 이름을 최대 두 줄로 표시 (UE Content Browser 타일처럼). 줄 수를 고정해 격자 높이를 맞춘다.
	const float LabelWidth = ImMax(1.0f, AvailableWidth);
	ImGui::Dummy(ImVec2(LabelWidth, ImGui::GetTextLineHeight() * 2.0f));
	const bool bLabelHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
	if (ImGui::IsItemVisible())
	{
		DrawTwoLineName(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), LabelWidth,
			MakeTileDisplayName(FilenameString, bHasTypeStyle));
	}

	// 썸네일이나 이름 어디에 올려도 전체 이름·타입·경로를 보여 준다.
	if ((bThumbnailHovered || bLabelHovered) && !ImGui::IsDragDropActive() && ImGui::BeginTooltip())
	{
		ImGui::TextUnformatted(FilenameString.c_str());
		if (bHasTypeStyle)
		{
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(TypeStyle.Color), "%s", TypeStyle.Label);
		}
		ImGui::TextDisabled("%s", PathLabel(Path).c_str());
		ImGui::EndTooltip();
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
