#include "EnginePCH.h"
#include "Editor/LevelEditor/LevelEditorTab.h"
#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Asset/FbxImporter/FbxAssetLoader.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Editor/Asset/FbxImporter/FbxImporter.h"
#include <filesystem>
#include <algorithm>

FLevelEditorTab::FLevelEditorTab()
{
	OnLevelLoaded.Add([this](std::string_view Path) {
		FLevelEditorTab::UpdateTabName(Path);
	});

	FLevelEditorTab::UpdateTabName(FEditorFileUtils::CurrentScenePath);
}

void FLevelEditorTab::DrawMenuBar()
{
	////////////////////////////////////////////////////////////
	// File 탭
	////////////////////////////////////////////////////////////

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("New Scene")) { OnNewScene.Broadcast(); }
		if (ImGui::MenuItem("Open Scene...")) { OnOpenScene.Broadcast(); }

		ImGui::Separator();

		if (ImGui::MenuItem("Save")) { OnSaveScene.Broadcast(); }
		if (ImGui::MenuItem("Save As...")) { OnSaveSceneAs.Broadcast(); }

		ImGui::Separator();

		if (ImGui::MenuItem("Import FBX...")) bOpenFbxImport = true;

		ImGui::EndMenu();
	}



	////////////////////////////////////////////////////////////
	// Window 탭
	////////////////////////////////////////////////////////////

	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
		{
			bool bOpen = Panel->IsOpen();

			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, bOpen))
			{
				Panel->SetOpen(!bOpen);
			}
		}
		ImGui::EndMenu();
	}



	////////////////////////////////////////////////////////////
	// Build 탭
	////////////////////////////////////////////////////////////

	if (ImGui::BeginMenu("Build"))
	{
		const bool bCanPackage = !IsPackaging || !IsPackaging();
		if (ImGui::MenuItem("Package Game (Release)", nullptr, false, bCanPackage)) { OnPackageGame.Broadcast(); }
		ImGui::EndMenu();
	}
}

void FLevelEditorTab::DrawFbxImportDialog()
{
	auto ChooseSource = [this]()
	{
		FString Selected = FEditorFileUtils::ChooseFbxFile();
		if (Selected.empty()) return;
		FbxSourcePath = std::move(Selected);
		const FString DefaultOutput = FbxPath::DefaultOutputDirectory(FbxSourcePath);
		strncpy_s(FbxOutputDirectory.data(), FbxOutputDirectory.size(), DefaultOutput.c_str(), _TRUNCATE);
		FbxImportError.clear();
	};

	if (bOpenFbxImport)
	{
		bOpenFbxImport = false;
		FbxSourcePath.clear();
		FbxOutputDirectory.fill(0);
		FbxImportError.clear();
		FbxSkeletonKeys.Empty();
		std::error_code Ec;
		for (fs::recursive_directory_iterator It("Assets", fs::directory_options::skip_permission_denied, Ec), End;
			!Ec && It != End; It.increment(Ec))
		{
			const FString Path = FbxPath::ToString(It->path());
			EFbxAssetType Type;
			FString Key, Ignored;
			if (FFbxAssetBake::IsBakedPath(Path) && FFbxAssetBake::ReadAssetType(Path, Type) &&
				Type == EFbxAssetType::Skeleton && FbxPath::GetAssetKey(Path, Key, Ignored))
				FbxSkeletonKeys.Add(std::move(Key));
		}
		std::sort(FbxSkeletonKeys.begin(), FbxSkeletonKeys.end());
		for (FString* Key : { &FbxOptions.SkeletalMeshSkeletonKey, &FbxOptions.AnimationSkeletonKey })
			if (std::find(FbxSkeletonKeys.begin(), FbxSkeletonKeys.end(), *Key) == FbxSkeletonKeys.end())
				Key->clear();
		ImGui::OpenPopup("Import FBX");
	}
	if (!ImGui::BeginPopupModal("Import FBX", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

	ImGui::TextWrapped("%s", FbxSourcePath.empty() ? "Select a source FBX" : FbxSourcePath.c_str());
	if (ImGui::Button("Browse FBX...")) ChooseSource();
	ImGui::InputText("Output folder", FbxOutputDirectory.data(), FbxOutputDirectory.size());
	ImGui::TextUnformatted("Output folder must be under Assets.");

	if (ImGui::Checkbox("Import Only Animations", &FbxOptions.bImportOnlyAnimations) && FbxOptions.bImportOnlyAnimations)
		FbxOptions.bImportAnimations = true;
	auto DrawSkeletonPicker = [this](const char* Label, FString& SelectedKey)
	{
		if (!ImGui::BeginCombo(Label, SelectedKey.empty() ? "None" : SelectedKey.c_str())) return;
		if (ImGui::Selectable("None", SelectedKey.empty())) SelectedKey.clear();
		for (const FString& Key : FbxSkeletonKeys)
		{
			const bool bSelected = SelectedKey == Key;
			if (ImGui::Selectable(Key.c_str(), bSelected)) SelectedKey = Key;
			if (bSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	};

	ImGui::BeginDisabled(FbxOptions.bImportOnlyAnimations);
	ImGui::SeparatorText("Static Meshes");
	ImGui::Checkbox("Bake Meshes##Static", &FbxOptions.bBakeStaticMeshes);
	ImGui::Checkbox("Combine Meshes##Static", &FbxOptions.bCombineStaticMeshes);
	ImGui::SeparatorText("Skeletal Meshes");
	DrawSkeletonPicker("Skeleton##SkeletalMesh", FbxOptions.SkeletalMeshSkeletonKey);
	ImGui::Checkbox("Bake Meshes##Skeletal", &FbxOptions.bBakeSkeletalMeshes);
	ImGui::Checkbox("Combine Meshes##Skeletal", &FbxOptions.bCombineSkeletalMeshes);
	ImGui::Checkbox("Convert Static Meshes in Bone Hierarchy", &FbxOptions.bConvertStaticsInBoneHierarchyToSkeletals);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Off: unskinned meshes under bones use the Static Meshes settings.");
	ImGui::EndDisabled();

	ImGui::SeparatorText("Animations");
	ImGui::BeginDisabled(FbxOptions.bImportOnlyAnimations);
	ImGui::Checkbox("Import Animations", &FbxOptions.bImportAnimations);
	ImGui::EndDisabled();
	ImGui::BeginDisabled(!FbxOptions.bImportAnimations && !FbxOptions.bImportOnlyAnimations);
	DrawSkeletonPicker("Skeleton##Animation", FbxOptions.AnimationSkeletonKey);
	const bool bNeedsSkeleton = FbxOptions.bImportOnlyAnimations && FbxOptions.AnimationSkeletonKey.empty();
	if (bNeedsSkeleton)
		ImGui::TextUnformatted("Select an existing Skeleton for animation-only import.");
	else if (FbxOptions.AnimationSkeletonKey.empty())
		ImGui::TextDisabled("None uses the Skeleton of the imported mesh for each rig.");
	int Range = static_cast<int>(FbxOptions.AnimationRange);
	if (ImGui::Combo("Animation Length", &Range, "Timeline\0Animated Time\0Set Range\0"))
		FbxOptions.AnimationRange = static_cast<EFbxAnimationRange>(Range);
	if (FbxOptions.AnimationRange == EFbxAnimationRange::SetRange)
	{
		ImGui::InputInt("Start Frame", &FbxOptions.FirstAnimationFrame);
		ImGui::InputInt("End Frame", &FbxOptions.LastAnimationFrame);
	}
	ImGui::InputInt("Custom Sample Rate (0 = source)", &FbxOptions.CustomBoneAnimationSampleRate);
	int Alignment = static_cast<int>(FbxOptions.FrameAlignment);
	if (ImGui::Combo("Frame Alignment", &Alignment, "None\0Floor\0Closest\0Ceiling\0"))
		FbxOptions.FrameAlignment = static_cast<EFbxFrameAlignment>(Alignment);
	ImGui::EndDisabled();

	if (!FbxImportError.empty()) ImGui::TextWrapped("%s", FbxImportError.c_str());
	ImGui::BeginDisabled(FbxSourcePath.empty() || bNeedsSkeleton);
	if (ImGui::Button("Import"))
	{
		auto Result = FFbxAssetLoader::Import(FbxSourcePath, FbxOptions, FbxOutputDirectory.data(), FFbxImporter::Cook);
		for (const auto& Warning : Result.Warnings) HTR_LOG(Warning, "FBX: {}", Warning);
		if (Result.bSuccess)
		{
			HTR_LOG(Info, "FBX import completed: {} assets", Result.AssetKeys.Num());
			ImGui::CloseCurrentPopup();
		}
		else
		{
			FbxImportError = std::move(Result.Error);
			HTR_LOG(Error, "FBX import failed: {}", FbxImportError);
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void FLevelEditorTab::Tick(float DeltaTime)
{
	for (auto& Panel : Panels)
		Panel->Tick(DeltaTime);
}

void FLevelEditorTab::OnRender(bool bActive)
{
	if (bActive)
	{
		Toolbar.Draw();
		DrawFbxImportDialog();
	}

	if (bUseDockSpace && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
	{
		// 부모 창이 달라져도 기존 패널 도킹 ID는 유지한다.
		const ImGuiID DockSpaceId = ImHashStr("EngineDockingSpace", 0, ImHashStr("DockingSpace"));
		ImGuiDockNodeFlags Flags = bPassthruCentralNode
			? ImGuiDockNodeFlags_PassthruCentralNode : ImGuiDockNodeFlags_None;
		if (!bActive)
			Flags |= ImGuiDockNodeFlags_KeepAliveOnly;

		ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags);
	}

	if (bActive)
	{
		for (auto& Panel : Panels)
		{
			if (Panel->IsOpen())
				Panel->OnRender();
		}
	}
}

void FLevelEditorTab::SetDockSpaceOptions(bool bEnabled, bool bPassthru)
{
	bUseDockSpace = bEnabled;
	bPassthruCentralNode = bPassthru;
}

void FLevelEditorTab::UpdateTabName(std::string_view InPath)
{
	const FString LevelName = InPath.empty()
		? "EmptyLevel" : std::filesystem::path(InPath).filename().string();
	TabName = LevelName + " (LevelEditor)###LevelEditor";
}
