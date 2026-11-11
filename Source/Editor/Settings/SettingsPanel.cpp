#include "EnginePCH.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Asset/AssetManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "Engine/World.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "GameFramework/DefaultPawn.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UObjectIterator.h"

namespace
{
	bool BeginSettingsTable(const char* Id)
	{
		if (!ImGui::BeginTable(Id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
			return false;
		ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.56f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.44f);
		return true;
	}

	void SettingRow(const char* Label, const char* Tooltip = nullptr)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::PushTextWrapPos();
		ImGui::TextUnformatted(Label);
		ImGui::PopTextWrapPos();
		if (Tooltip)
			ImGui::SetItemTooltip("%s", Tooltip);

		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	const char* GetShadingModelOverrideName(const TOptional<ELightingModel>& Override)
	{
		return Override ? LightingModelNames[static_cast<int>(*Override)] : "None";
	}
}

// 종료 시 렌더·에디터·뷰포트 설정을 함께 저장한다.
FSettingsPanel::~FSettingsPanel()
{
	ViewportsPanel = nullptr;
	SaveSettings();
}

// 설정 파일을 읽어 패널 상태를 초기화한다.
bool FSettingsPanel::Init()
{
	LoadSettings();
	return true;
}


// 설정 패널의 프레임 갱신 진입점이다.
void FSettingsPanel::Tick(float DeltaTime)
{
}

// ImGui 조작으로 렌더·카메라 설정과 저장·복원 요청을 처리한다.
void FSettingsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
	ImGui::Begin("Settings");

	if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen) && BeginSettingsTable("RenderingValues"))
	{
		const char* RenderingPaths[] = { "Forward", "Deferred" };
		int RenderingPathIndex = static_cast<int>(Settings.RenderingPath);
		SettingRow("Rendering Path");
		if (ImGui::Combo("##RenderingPath", &RenderingPathIndex, RenderingPaths, IM_ARRAYSIZE(RenderingPaths)))
			Settings.RenderingPath = static_cast<ERenderingPath>(RenderingPathIndex);

		bool bUseGPUSkinning = USkinnedMeshComponent::IsGPUSkinningEnabled();
		SettingRow("GPU Skinning");
		if (ImGui::Checkbox("##GPUSkinning", &bUseGPUSkinning))
			USkinnedMeshComponent::SetUseGPUSkinning(bUseGPUSkinning);

		SettingRow("Shading Model Override", "None uses each material's shading model in Forward.");
		if (ImGui::BeginCombo("##ShadingModelOverride", GetShadingModelOverrideName(Settings.ShadingModelOverride)))
		{
			if (ImGui::Selectable("None", !Settings.ShadingModelOverride))
				Settings.ShadingModelOverride.Reset();
			for (int Index = 0; Index < IM_ARRAYSIZE(LightingModelNames); ++Index)
			{
				const auto Choice = static_cast<ELightingModel>(Index);
				if (ImGui::Selectable(LightingModelNames[Index], Settings.ShadingModelOverride == Choice))
					Settings.ShadingModelOverride = Choice;
			}
			ImGui::EndCombo();
		}

		const char* LightCullingModes[] = { "Off", "Tiled", "Clustered", "Tiled 2.5D" };
		int LightCullingModeIndex = static_cast<int>(Settings.LightCullingMode);
		SettingRow("Light Culling", "Applies to Forward Lambert/Blinn-Phong. Off evaluates all uploaded local lights.");
		if (ImGui::Combo("##LightCulling", &LightCullingModeIndex, LightCullingModes, IM_ARRAYSIZE(LightCullingModes)))
			Settings.LightCullingMode = static_cast<ELightCullingMode>(LightCullingModeIndex);
		ImGui::EndTable();
	}

	DrawShadowMapSettings();

	if (ImGui::CollapsingHeader("Decals", ImGuiTreeNodeFlags_DefaultOpen) && BeginSettingsTable("DecalValues"))
	{
		SettingRow("Draw Decals");
		ImGui::Checkbox("##DrawDecals", &Settings.bDrawDecals);

		const char* DecalBroadPhases[] = { "Off", "Linear AABB", "BVH" };
		int DecalBroadPhaseIndex = Settings.bDecalBVHCulling ? 2 : (Settings.bDecalCulling ? 1 : 0);
		SettingRow("Broad Phase");
		if (ImGui::Combo("##DecalBroadPhase", &DecalBroadPhaseIndex, DecalBroadPhases, IM_ARRAYSIZE(DecalBroadPhases)))
		{
			Settings.bDecalCulling = DecalBroadPhaseIndex != 0;
			Settings.bDecalBVHCulling = DecalBroadPhaseIndex == 2;
		}

		SettingRow("Narrow Phase (SAT)");
		ImGui::Checkbox("##DecalNarrowPhase", &Settings.bDecalNarrowCulling);
		SettingRow("Use OBBTree");
		ImGui::Checkbox("##DecalOBBTree", &Settings.bDecalOBBTree);
		ImGui::EndTable();
	}

	if (ImGui::CollapsingHeader("Post Process", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (BeginSettingsTable("PostProcessValues"))
		{
			SettingRow("FXAA");
			ImGui::Checkbox("##FXAA", &Settings.bEnableFXAA);
			SettingRow("Gamma Correction");
			ImGui::Checkbox("##GammaCorrection", &Settings.bGammaCorrection);
			SettingRow("Display Gamma");
			ImGui::BeginDisabled(!Settings.bGammaCorrection);
			ImGui::SliderFloat("##DisplayGamma", &Settings.DisplayGamma, 1.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::EndDisabled();
			SettingRow("Vignette");
			ImGui::Checkbox("##Vignette", &Settings.bShowVignette);
			SettingRow("Camera Fade / Letterbox");
			ImGui::Checkbox("##CameraOverlay", &Settings.bShowCameraOverlay);
			ImGui::EndTable();
		}

		if (ImGui::TreeNode("Camera Overlay Debug"))
		{
			if (BeginSettingsTable("CameraOverlayDebugValues"))
			{
				FCameraOverlayDebug& D = GCameraOverlayDebug;
				SettingRow("Override (PIE)");
				ImGui::Checkbox("##Override", &D.bOverride);
				SettingRow("Fade Color");
				ImGui::ColorEdit3("##FadeColor", D.FadeColor);
				SettingRow("Fade Amount");
				ImGui::SliderFloat("##FadeAmount", &D.FadeAmount, 0.0f, 1.0f);
				SettingRow("Letterbox");
				ImGui::SliderFloat("##Letterbox", &D.LetterboxAmount, 0.0f, 1.0f);
				SettingRow("Letterbox Aspect");
				ImGui::SliderFloat("##LetterboxAspect", &D.LetterboxAspect, 1.0f, 2.5f);
				SettingRow("Vignette");
				ImGui::SliderFloat("##DebugVignette", &D.VignetteIntensity, 0.0f, 2.0f);
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
	}

	if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (BeginSettingsTable("ViewportValues"))
		{
			SettingRow("Draw Primitives", "Fill mode is configured per viewport.");
			ImGui::Checkbox("##DrawPrimitives", &Settings.bDrawPrimitives);
			SettingRow("Show Skeletal Mesh");
			ImGui::Checkbox("##ShowSkeletalMesh", &Settings.bShowSkeletalMesh);
			SettingRow("Draw Bounding Box");
			ImGui::Checkbox("##DrawBoundingBox", &Settings.bDrawBoundingBox);
			SettingRow("Show Object UUID");
			ImGui::Checkbox("##ShowUUID", &Settings.bShowUUID);
			ImGui::EndTable();
		}

		ImGui::SeparatorText("Grid");
		if (BeginSettingsTable("ViewportGridValues"))
		{
			SettingRow("Draw Batch Line / Grid");
			ImGui::Checkbox("##DrawBatchLine", &Settings.bDrawBatchLine);
			SettingRow("Draw PS Grid");
			ImGui::Checkbox("##DrawPSGrid", &Settings.bDrawPSGrid);
			SettingRow("Grid Spacing");
			ImGui::SliderInt("##GridSpacing", &Settings.GridSpacing, 1, 100);
			ImGui::EndTable();
		}

		ImGui::SeparatorText("Scene Depth");
		if (BeginSettingsTable("ViewportDepthValues"))
		{
			SettingRow("Max Distance", "White represents this distance from the camera.");
			if (ImGui::DragFloat("##SceneDepthRange", &Settings.SceneDepthMaxRange, 0.5f,
				0.1f, 10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
				ApplySceneDepthRange();
			ImGui::EndTable();
		}
	}

	if (ImGui::CollapsingHeader("Editor & PIE", ImGuiTreeNodeFlags_DefaultOpen) && BeginSettingsTable("EditorValues"))
	{
		SettingRow("Camera Rotate Sensitivity");
		ImGui::SliderFloat("##CameraRotateSensitivity", &Settings.MouseSensitivity, 0.01f, 1.0f, "%.2f");
		SettingRow("Camera Speed");
		ImGui::SliderFloat("##CameraSpeed", &Settings.CameraSpeed, 0.1f, 10.0f, "%.2f");
		DrawDefaultPawnSettings();
		ImGui::EndTable();
	}

	UCameraComponent* CamCom = World->GetMainCamera()->GetCameraComponent();
	CamCom->SetMouseSensitivity(Settings.MouseSensitivity);
	CamCom->SetMoveSpeed(Settings.CameraSpeed);

	ImGui::Dummy(ImVec2(0.0f, SectionGap));
	ImGui::SeparatorText("Settings File");
	const float ButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Load Settings", ImVec2(ButtonWidth, 0.0f)))
		LoadSettings();
	ImGui::SetItemTooltip("Load settings from editor.ini");
	ImGui::SameLine();
	if (ImGui::Button("Save Settings", ImVec2(ButtonWidth, 0.0f)))
		SaveSettings();
	ImGui::SetItemTooltip("Save settings to editor.ini");

	ImGui::End();
}

// 알려진 설정 섹션을 editor.ini에 함께 기록해 뷰포트 값 유실을 막는다.
bool FSettingsPanel::SaveSettings() const
{
	FEditorSettings Snapshot = Settings;
	ReadViewportSettings(Snapshot);
	std::ofstream File("editor.ini");

	if (!File.is_open())
	{
		HTR_LOG(Error, "Failed to open editor.ini for saving.");
		return false;
	}

	File << "[Rendering]\n";
	File << "Wireframe=" << Settings.bWireframe << "\n";
	File << "DrawPrimitives=" << Settings.bDrawPrimitives << "\n";
	File << "CacheShadows=" << Settings.bCacheShadows << "\n";
	File << "PcfMode=" << static_cast<uint32_t>(Settings.PcfMode) << "\n";
	File << "DrawBoundingBox=" << Settings.bDrawBoundingBox << "\n";
	File << "ShowUUID=" << Settings.bShowUUID << "\n";
	File << "DrawDecals=" << Settings.bDrawDecals << "\n";
	File << "DecalCulling=" << Settings.bDecalCulling << "\n";
	File << "DecalNarrowCulling=" << Settings.bDecalNarrowCulling << "\n";
	File << "DecalBVHCulling=" << Settings.bDecalBVHCulling << "\n";
	File << "DecalOBBTree=" << Settings.bDecalOBBTree << "\n";
	File << "DrawBatchLine=" << Settings.bDrawBatchLine << "\n";
	File << "DrawCascadeBounds=" << Settings.bDrawCascadeBounds << "\n";
	File << "DrawPSGrid=" << Settings.bDrawPSGrid << "\n";
	File << "SceneDepthMaxRange=" << Settings.SceneDepthMaxRange << "\n";
	File << "EnableFXAA=" << Settings.bEnableFXAA << "\n";

	File << "GammaCorrection=" << Settings.bGammaCorrection << "\n";
	File << "DisplayGamma=" << Settings.DisplayGamma << "\n";
	File << "ShowVignette=" << Settings.bShowVignette << "\n";
	File << "ShowCameraOverlay=" << Settings.bShowCameraOverlay << "\n";
	File << "ShowSkeletalMesh=" << Settings.bShowSkeletalMesh << "\n";

	File << "LightCullingMode=" << (Settings.LightCullingMode == ELightCullingMode::Off ? "Off" : Settings.LightCullingMode == ELightCullingMode::Tiled ? "Tiled" : Settings.LightCullingMode == ELightCullingMode::Tiled25D ? "Tiled25D" : "Clustered") << "\n";
	File << "RenderingPath=" << (Settings.RenderingPath == ERenderingPath::Forward ? "Forward" : "Deferred") << "\n";
	File << "ShadingModelOverride=" << GetShadingModelOverrideName(Settings.ShadingModelOverride) << "\n";
	File << "\n";

	File << "[Editor]\n";
	File << "CameraMoveSpeed=" << Settings.CameraSpeed << "\n";
	File << "CameraRotateSensitivity=" << Settings.MouseSensitivity << "\n";
	File << "GridSpacing=" << Settings.GridSpacing << "\n";
	File << "\n";

	File << "[MultipleViewports]\n";
	File << "Horizontal=" << Snapshot.MultipleViewportsHorizontal << "\n";
	File << "Vertical=" << Snapshot.MultipleViewportsVertical << "\n";
	File << "Layout=" << (Snapshot.bMultipleViewportsSingle ? "Single" : "Quad") << "\n";
	File << "SingleViewIndex=" << Snapshot.MultipleViewportsSingleViewIndex << "\n";

	for (int32 Index = 0; Index < 4; ++Index)
    {
        File << "View" << Index << "Fov=" << Snapshot.ViewFov[Index] << "\n";
        File << "View" << Index << "OrthoWidth=" << Snapshot.ViewOrthoWidth[Index] << "\n";
        File << "View" << Index << "Preset=" << Snapshot.ViewPreset[Index] << "\n";
        File << "View" << Index << "Wireframe=" << Snapshot.ViewWireframe[Index] << "\n";
        File << "View" << Index << "Mode=" << Snapshot.ViewMode[Index] << "\n";
        File << "View" << Index << "LocationX=" << Snapshot.ViewLocation[Index].X << "\n";
        File << "View" << Index << "LocationY=" << Snapshot.ViewLocation[Index].Y << "\n";
        File << "View" << Index << "LocationZ=" << Snapshot.ViewLocation[Index].Z << "\n";
        File << "View" << Index << "RotationX=" << Snapshot.ViewRotation[Index].X << "\n";
        File << "View" << Index << "RotationY=" << Snapshot.ViewRotation[Index].Y << "\n";
        File << "View" << Index << "RotationZ=" << Snapshot.ViewRotation[Index].Z << "\n";
        File << "View" << Index << "RotationW=" << Snapshot.ViewRotation[Index].W << "\n";
    }

	File << "DirectionalShadowMapSizeLevel=" << Settings.DirectionalShadowMapSizeLevel << '\n';
	File << "PointShadowMapSizeLevel=" << Settings.PointShadowMapSizeLevel << '\n';
	File << "SpotShadowMapSizeLevel=" << Settings.SpotShadowMapSizeLevel << '\n';

    File.close();
	return true;
}

// 파일의 키를 파싱해 렌더·카메라·레이아웃 설정을 복원한다.
bool FSettingsPanel::LoadSettings()
{
	std::ifstream File("editor.ini");
	if (!File.is_open())
	{
		return false;
	}

	uint8 LocationComponentMasks[4]{};
	uint8 RotationComponentMasks[4]{};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = false;
		Settings.bViewRotationSaved[Index] = false;
	}

	bool bHasShadingModelOverride = false;
	std::string Line;
	while (std::getline(File, Line))
	{
		if (Line.empty() || Line[0] == ';' || Line[0] == '[') continue;

		std::istringstream Iss(Line);
		std::string Key;
		if (std::getline(Iss, Key, '='))
		{
			std::string ValueStr;
			if (std::getline(Iss, ValueStr))
			{
				// 새 View 키는 NaN·잘못된 숫자·범위 밖 값을 무시한다.
                for (int32 Index = 0; Index < 4; ++Index)
                {
                    FString Prefix = "View";
                    Prefix += static_cast<char>('0' + Index);
                    std::istringstream Number(ValueStr);
                    float Value = 0;
                    if (!(Number >> Value) || !std::isfinite(Value)) continue;
                    if (Key == Prefix + "Fov" && Value >= 1 && Value <= 179)
                        Settings.ViewFov[Index] = Value;
                    else if (Key == Prefix + "OrthoWidth" && Value >= 0.01f)
                        // 파일 값은 절대 상한으로 먼저 제한하고, 실제 View 비율 상한은 Adapter가 적용한다.
                        Settings.ViewOrthoWidth[Index] = FMath::Clamp(Value, 0.01f, 1000000.0f);
                    else if (Key == Prefix + "Preset" && Value >= 0 && Value <= 7 && Value == static_cast<int32>(Value))
                        Settings.ViewPreset[Index] = static_cast<int32>(Value);
                    else if (Key == Prefix + "Wireframe" && (Value == 0 || Value == 1))
                        Settings.ViewWireframe[Index] = static_cast<int32>(Value);
					else if (Key == Prefix + "Mode" && Value >= 0 &&
						Value <= static_cast<int32>(EViewportMode::ShadowMask) && Value == static_cast<int32>(Value))
						Settings.ViewMode[Index] = static_cast<int32>(Value);
                    else if (Key == Prefix + "LocationX")
                    {
                        Settings.ViewLocation[Index].X = Value;
                        LocationComponentMasks[Index] |= 1 << 0;
                    }
                    else if (Key == Prefix + "LocationY")
                    {
                        Settings.ViewLocation[Index].Y = Value;
                        LocationComponentMasks[Index] |= 1 << 1;
                    }
                    else if (Key == Prefix + "LocationZ")
                    {
                        Settings.ViewLocation[Index].Z = Value;
                        LocationComponentMasks[Index] |= 1 << 2;
                    }
                    else if (Key == Prefix + "RotationX")
                    {
                        Settings.ViewRotation[Index].X = Value;
                        RotationComponentMasks[Index] |= 1 << 0;
                    }
                    else if (Key == Prefix + "RotationY")
                    {
                        Settings.ViewRotation[Index].Y = Value;
                        RotationComponentMasks[Index] |= 1 << 1;
                    }
                    else if (Key == Prefix + "RotationZ")
                    {
                        Settings.ViewRotation[Index].Z = Value;
                        RotationComponentMasks[Index] |= 1 << 2;
                    }
                    else if (Key == Prefix + "RotationW")
                    {
                        Settings.ViewRotation[Index].W = Value;
                        RotationComponentMasks[Index] |= 1 << 3;
                    }
                }
                if (Key == "Wireframe") Settings.bWireframe = std::stoi(ValueStr);
				else if (Key == "DrawPrimitives") Settings.bDrawPrimitives = std::stoi(ValueStr);
				else if (Key == "CacheShadows") Settings.bCacheShadows = std::stoi(ValueStr);
				else if (Key == "PcfMode")
				{
					const int Mode = std::stoi(ValueStr);
					if (Mode >= 0 && Mode <= static_cast<int>(EPcfMode::PCSS))
						Settings.PcfMode = static_cast<EPcfMode>(Mode);
				}
				else if (Key == "DrawBoundingBox") Settings.bDrawBoundingBox = std::stoi(ValueStr);
				else if (Key == "ShowUUID") Settings.bShowUUID = std::stoi(ValueStr);
				else if (Key == "DrawDecals") Settings.bDrawDecals = std::stoi(ValueStr);
				else if (Key == "DecalCulling") Settings.bDecalCulling = std::stoi(ValueStr);
				else if (Key == "DecalNarrowCulling") Settings.bDecalNarrowCulling = std::stoi(ValueStr);
				else if (Key == "DecalBVHCulling") Settings.bDecalBVHCulling = std::stoi(ValueStr);
				else if (Key == "DecalOBBTree") Settings.bDecalOBBTree = std::stoi(ValueStr);
				else if (Key == "DrawBatchLine") Settings.bDrawBatchLine = std::stoi(ValueStr);
				else if (Key == "DrawCascadeBounds") Settings.bDrawCascadeBounds = std::stoi(ValueStr);
				else if (Key == "DrawPSGrid") Settings.bDrawPSGrid = std::stoi(ValueStr);
				else if (Key == "SceneDepthMaxRange") Settings.SceneDepthMaxRange = std::stof(ValueStr);
				else if (Key == "EnableFXAA") Settings.bEnableFXAA = std::stoi(ValueStr);

				else if (Key == "GammaCorrection")   Settings.bGammaCorrection = std::stoi(ValueStr);
				else if (Key == "DisplayGamma")      Settings.DisplayGamma = std::stof(ValueStr);
				else if (Key == "ShowVignette")      Settings.bShowVignette = std::stoi(ValueStr);
				else if (Key == "ShowCameraOverlay") Settings.bShowCameraOverlay = std::stoi(ValueStr);
				else if (Key == "ShowSkeletalMesh")  Settings.bShowSkeletalMesh = std::stoi(ValueStr);

				else if (Key == "LightCullingMode")
				{
					if (ValueStr == "Off") Settings.LightCullingMode = ELightCullingMode::Off;
					else if (ValueStr == "Tiled") Settings.LightCullingMode = ELightCullingMode::Tiled;
					else if (ValueStr == "Clustered") Settings.LightCullingMode = ELightCullingMode::Clustered;
					else if (ValueStr == "Tiled25D") Settings.LightCullingMode = ELightCullingMode::Tiled25D;
				}
				else if (Key == "EnableTiledLightCulling" || Key == "EnableClusteredLightCulling")
				{
					Settings.LightCullingMode = std::stoi(ValueStr) != 0 ? ELightCullingMode::Tiled : ELightCullingMode::Off;
				}
				else if (Key == "RenderingPath")
				{
					if (ValueStr == "Forward") Settings.RenderingPath = ERenderingPath::Forward;
					else if (ValueStr == "Deferred") Settings.RenderingPath = ERenderingPath::Deferred;
				}
				else if (Key == "ShadingModelOverride" || (Key == "LightingModel" && !bHasShadingModelOverride))
				{
					Settings.ShadingModelOverride = ParseLightingModel(ValueStr);
					bHasShadingModelOverride = Key == "ShadingModelOverride";
				}

				else if (Key == "CameraMoveSpeed") Settings.CameraSpeed = std::stof(ValueStr);
				else if (Key == "CameraRotateSensitivity") Settings.MouseSensitivity = std::stof(ValueStr);
				else if (Key == "GridSpacing") Settings.GridSpacing = std::stoi(ValueStr);
				else if (Key == "Horizontal") Settings.MultipleViewportsHorizontal = std::stof(ValueStr);
				else if (Key == "Vertical") Settings.MultipleViewportsVertical = std::stof(ValueStr);
				else if (Key == "Layout") Settings.bMultipleViewportsSingle = ValueStr == "Single";
				else if (Key == "SingleViewIndex")
				{
					const int32 ViewIndex = std::stoi(ValueStr);
					Settings.MultipleViewportsSingleViewIndex = ViewIndex >= 0 && ViewIndex < 4 ? ViewIndex : 0;
				}
				else if (Key == "DirectionalShadowMapSizeLevel")
				{
					Settings.DirectionalShadowMapSizeLevel = std::stoi(ValueStr);
				}
				else if (Key == "PointShadowMapSizeLevel")
				{
					Settings.PointShadowMapSizeLevel = std::stoi(ValueStr);
				}
				else if (Key == "SpotShadowMapSizeLevel")
				{
					Settings.SpotShadowMapSizeLevel = std::stoi(ValueStr);
				}
			}
		}
	}

	File.close();
	if (!std::isfinite(Settings.SceneDepthMaxRange))
		Settings.SceneDepthMaxRange = 50.0f;
	Settings.SceneDepthMaxRange = FMath::Clamp(Settings.SceneDepthMaxRange, 0.1f, 10000.0f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = LocationComponentMasks[Index] == 0x7;
		if (RotationComponentMasks[Index] == 0xF)
		{
			const FQuat& Rotation = Settings.ViewRotation[Index];
			const float LengthSquared = Rotation.X * Rotation.X + Rotation.Y * Rotation.Y +
				Rotation.Z * Rotation.Z + Rotation.W * Rotation.W;
			if (LengthSquared > 1.0e-12f)
			{
				Settings.ViewRotation[Index] = Rotation.Normalized();
				Settings.bViewRotationSaved[Index] = true;
			}
		}
	}
	Settings.DirectionalShadowMapSizeLevel = FMath::Clamp(Settings.DirectionalShadowMapSizeLevel, 0, 4);
	Settings.PointShadowMapSizeLevel = FMath::Clamp(Settings.PointShadowMapSizeLevel, 0, 4);
	Settings.SpotShadowMapSizeLevel = FMath::Clamp(Settings.SpotShadowMapSizeLevel, 0, 4);
	ApplyViewportSettings();
	return true;
}

// 뷰포트 설정 복사
void FSettingsPanel::ReadViewportSettings(FEditorSettings& Out) const
{
	if (!ViewportsPanel)
		return;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
		if (!Client)
			continue;

		Out.ViewFov[Index] = Client->GetViewFOV();
		Out.ViewOrthoWidth[Index] = Client->GetOrthoWidth();
		Out.ViewWireframe[Index] = Client->IsWireframe() ? 1 : 0;
		Out.ViewMode[Index] = static_cast<int32>(Client->GetViewportMode());
		Out.ViewLocation[Index] = Client->GetViewLocation();
		Out.ViewRotation[Index] = Client->GetViewRotation().Quaternion();
	}
}

// 종료 시 설정 갱신
void FSettingsPanel::CaptureViewportSettings() { ReadViewportSettings(Settings); }

// 뷰포트 설정 적용
void FSettingsPanel::ApplyViewportSettings()
{
	if (!ViewportsPanel)
		return;
	ApplySceneDepthRange();

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
		if (!Client)
			continue;

		if (Settings.ViewFov[Index] > 0.0f)
			Client->SetViewFOV(Settings.ViewFov[Index]);
		if (Settings.ViewWireframe[Index] >= 0)
			Client->SetViewportMode(Settings.ViewWireframe[Index] == 1 ? EViewportMode::Wireframe : EViewportMode::Solid);
		if (Settings.ViewMode[Index] >= 0)
			Client->SetViewportMode(static_cast<EViewportMode>(Settings.ViewMode[Index]));
	}
}

void FSettingsPanel::ApplySceneDepthRange()
{
	if (!ViewportsPanel)
		return;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index))
			Client->SetMaxRange(Settings.SceneDepthMaxRange);
	}
}

void FSettingsPanel::DrawDefaultPawnSettings()
{
    SettingRow("DefaultPawn Mesh", "Start PIE with a DefaultPawn to edit its mesh.");
    ADefaultPawn* Pawn = World ? Cast<ADefaultPawn>(World->GetPlayerPawn()) : nullptr;
    if (!Pawn)
    {
        ImGui::TextDisabled("PIE only");
        return;
    }

    const auto DrawMeshPicker = [](const char* Label, UStaticMesh*& SelectedMesh)
    {
        const auto GetMeshDisplayName = [](const UStaticMesh* Mesh) -> FString
        {
            if (!Mesh) return "None";
            const fs::path Path(Mesh->GetPath());
            return Path.stem() == "Atlas"
                ? Path.parent_path().filename().string()
                : Path.stem().string();
        };
        const FString Preview = GetMeshDisplayName(SelectedMesh);
        bool bChanged = false;
        const bool bOpen = ImGui::BeginCombo(Label, Preview.c_str());
        if (SelectedMesh && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", SelectedMesh->GetPath().c_str());
        if (bOpen)
        {
            if (ImGui::Selectable("None", SelectedMesh == nullptr))
            {
                SelectedMesh = nullptr;
                bChanged = true;
            }
            for (TObjectIterator<UStaticMesh> Mesh; Mesh; ++Mesh)
            {
                const FString& Path = Mesh->GetPath();
                if (!Path.empty() && UAssetManager::FindLoadedAsset(Path) != *Mesh) continue;
                const FString Name = GetMeshDisplayName(*Mesh);
                const bool bSelected = *Mesh == SelectedMesh;
                ImGui::PushID(*Mesh);
                if (ImGui::Selectable(Name.c_str(), bSelected))
                {
                    SelectedMesh = *Mesh;
                    bChanged = true;
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Mesh->GetPath().c_str());
                ImGui::PopID();
            }

            // 아직 로드되지 않은 메시는 경로만 보여 주고 고를 때 로드한다
            for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::StaticMesh))
            {
                ImGui::PushID(ItemPath.c_str());
                if (ImGui::Selectable(fs::path(ItemPath).stem().string().c_str(), false))
                {
                    if (UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>(ItemPath))
                    {
                        SelectedMesh = Mesh;
                        bChanged = true;
                    }
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return bChanged;
    };

    UStaticMeshComponent* PawnMesh = Pawn->GetStaticMeshComponent();
    if (PawnMesh)
    {
        UStaticMesh* Mesh = PawnMesh->GetStaticMesh();
        if (DrawMeshPicker("##PawnMesh", Mesh)) PawnMesh->SetStaticMesh(Mesh);
    }
}

void FSettingsPanel::DrawShadowMapSettings()
{
	if (!ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen) || !BeginSettingsTable("ShadowValues"))
		return;

	SettingRow("Cache Shadows");
	ImGui::Checkbox("##CacheShadows", &Settings.bCacheShadows);

	const char* QualityLevelStr[] = { "Very Low", "Low", "Middle", "High", "Very High" };
	SettingRow("Directional Quality");
	if (ImGui::Combo("##DirectionalShadowQuality", &Settings.DirectionalShadowMapSizeLevel, QualityLevelStr, IM_ARRAYSIZE(QualityLevelStr)))
	{
		Settings.DirectionalShadowMapSizeLevel = FMath::Clamp(Settings.DirectionalShadowMapSizeLevel, 0, IM_ARRAYSIZE(QualityLevelStr) - 1);
	}

	SettingRow("Point Quality");
	if (ImGui::Combo("##PointShadowQuality", &Settings.PointShadowMapSizeLevel, QualityLevelStr, IM_ARRAYSIZE(QualityLevelStr)))
	{
		Settings.PointShadowMapSizeLevel = FMath::Clamp(Settings.PointShadowMapSizeLevel, 0, IM_ARRAYSIZE(QualityLevelStr) - 1);
	}

	SettingRow("Spot Quality");
	if (ImGui::Combo("##SpotShadowQuality", &Settings.SpotShadowMapSizeLevel, QualityLevelStr, IM_ARRAYSIZE(QualityLevelStr)))
	{
		Settings.SpotShadowMapSizeLevel = FMath::Clamp(Settings.SpotShadowMapSizeLevel, 0, IM_ARRAYSIZE(QualityLevelStr) - 1);
	}

	const char* PcfModes[] = { "Off", "8 (Samples)", "16 (Samples)", "32 (Samples)", "PCSS" };
	int PcfModeIndex = static_cast<int>(Settings.PcfMode);
	SettingRow("Shadow Filter");
	if (ImGui::Combo("##ShadowFilter", &PcfModeIndex, PcfModes, IM_ARRAYSIZE(PcfModes)))
		Settings.PcfMode = static_cast<EPcfMode>(PcfModeIndex);

	SettingRow("Draw Cascade Bounds", "Cascade 0-3: red, yellow, green, blue.");
	ImGui::Checkbox("##DrawCascadeBounds", &Settings.bDrawCascadeBounds);
	ImGui::EndTable();
}
