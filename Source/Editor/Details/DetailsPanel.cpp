#include "EnginePCH.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/AnimationEditor/FAnimationAssetPicker.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/Details/PropertyEditor.h"

#include "imgui_internal.h"
#include "Editor/HitoriEd/ComponentReregisterContext.h"
#include "Editor/HitoriEd/EditorDragDrop.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Components/TextRenderComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/SpotLightComponent.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"

#include "Components/RotatingMovementComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Components/FireBallComponent.h"
#include "Asset/AssetManager.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Text/Font.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"
#include "Core/EngineLog.h"

#include "Editor/HitoriEd/EditorFileUtils.h"
#include "Script/LuaScriptManager.h"


namespace
{
	struct FPropertyEdit
	{
		TArray<std::function<void()>> Changes;

		template <typename T, typename Draw>
		void DrawValue(T& Destination, Draw DrawWidget)
		{
			T Value = Destination;
			if (DrawWidget(Value))
			{
				Changes.Add([&Destination, Value = std::move(Value)]() mutable
				{
					Destination = std::move(Value);
				});
			}
		}
	};

	struct FDragSettings
	{
		float Speed;
		float Min;
		float Max;
	};

	FDragSettings GetDragSettings(const FPropertyMeta& Meta, float DefaultSpeed)
	{
		const float Min = (std::max)(Meta.UIMin, Meta.ClampMin);
		const float Max = (std::min)(Meta.UIMax, Meta.ClampMax);
		const bool bHasRange = Min > -FLT_MAX || Max < FLT_MAX;
		return { Meta.Delta > 0.0f ? Meta.Delta : DefaultSpeed, bHasRange ? Min : 0.0f, bHasRange ? Max : 0.0f };
	}

	int32 ToIntBound(float Value)
	{
		if (Value <= static_cast<float>(INT_MIN)) return INT_MIN;
		if (Value >= static_cast<float>(INT_MAX)) return INT_MAX;
		return static_cast<int32>(Value);
	}

	// 드래그 범위와 별개로, Ctrl+클릭으로 직접 입력한 값도 Clamp 범위로 자른다.
	void ClampToMeta(float* Values, int32 Count, const FPropertyMeta& Meta)
	{
		if (!Meta.HasClamp())
		{
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Values[Index] = std::clamp(Values[Index], Meta.ClampMin, Meta.ClampMax);
		}
	}

	void DrawScriptSection(AActor* Actor)
	{
		ImGui::SeparatorText("Lua Script");

		if (Actor->ScriptPath.empty())
		{
			if (ImGui::Button("Create Script"))
			{
				const FString& ScenePath = FEditorFileUtils::CurrentScenePath;
				if (ScenePath.empty())
				{
					HTR_LOG(Warning, "[Lua] Save the scene first. The scene name will be included in the script name.");
				}
				else
				{
					const FString SceneName = FLuaScriptManager::FromPath(FLuaScriptManager::ToPath(ScenePath).stem());
					const FString NewPath = FLuaScriptManager::MakeScriptPath(SceneName, Actor->GetName());
					if (FLuaScriptManager::Get().CreateScriptFromTemplate(NewPath))
					{
						Actor->ScriptPath = NewPath;   // 리플렉션 프로퍼티라 씬 저장 때 같이 저장된다
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Choose Script..."))
			{
				const FString Selected = FEditorFileUtils::ChooseLuaScriptFile();
				if (!Selected.empty()) Actor->ScriptPath = Selected;   // 리플렉션 프로퍼티라 씬 저장 때 같이 저장된다
			}
			return;
		}

		ImGui::TextWrapped("%s", Actor->ScriptPath.c_str());
		if (ImGui::Button("Edit Script"))
		{
			FLuaScriptManager::Get().OpenScriptInEditor(Actor->ScriptPath);
		}
		ImGui::SameLine();
		if (ImGui::Button("Change..."))
		{
			const FString Selected = FEditorFileUtils::ChooseLuaScriptFile();
			if (!Selected.empty()) Actor->ScriptPath = Selected;
		}
		ImGui::SameLine();
		if (ImGui::Button("Detach"))
		{
			Actor->ScriptPath.clear();   // 연결만 끊고 파일은 지우지 않는다
		}
	}

	bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
	{
		bool isValueChanged = false;

		float lineHeight = GImGui->Font->LegacySize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
		ImVec2 buttonSize = { lineHeight + 2.0f , lineHeight };

		// 각 버튼의 색상 설정
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

		// 굵은 폰트 적용
		if (ImGui::Button(_label.c_str(), buttonSize))
		{
			_value = _resetValue;
			isValueChanged = true;
		}
		ImGui::PopStyleColor(3);

		// 드래그 슬라이더
		ImGui::SameLine();
		std::string dragID = "##" + _label;
		isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, "%.2f");

		return isValueChanged;

	}

	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue, float _columnWidth)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;

		bool isVectorChanged = false;

		ImGui::PushID(_label.c_str());
		if (ImGui::BeginTable(_label.c_str(), 2, flags)) // 고유 ID, 열 2개, 플래그
		{
			// ImGuiTableColumnFlags_WidthFixed: 초기 너비 고정
			// ImGuiTableColumnFlags_WidthStretch: 창 크기에 따라 너비 조절 (기본값)
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, _columnWidth);
			ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text(_label.c_str()); // 왼쪽 열: 레이블

			ImGui::TableSetColumnIndex(1);
			// [컨트롤러 UI 코드] // 오른쪽 열: 컨트롤러
			ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 2,0 });
			ImGui::PushFont(boldFont);

			isVectorChanged |= DrawAxisControl("X", _values[0], 0.1f, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Y", _values[1], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Z", _values[2], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::PopFont();
			ImGui::PopStyleVar();
			ImGui::EndTable();
		}

		ImGui::Columns(1);

		ImGui::PopID();

		return isVectorChanged;
	}

	// FRotator의 V는 (Pitch, Yaw, Roll) 순서지만 엔진은 Roll=X, Pitch=Y, Yaw=Z축이므로
	// X/Y/Z 칸이 실제 회전축과 맞도록 재배열해 편집한다.
	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f, 55.0f);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}

	void DrawTextureSlot(FPropertyEdit& Edit, UMaterial** MaterialPtr, int32 SlotIndex)
	{
		const float ThumbnailSize = 64.0f;

		UMaterial* Material = *MaterialPtr;
		UTexture2D* Texture = nullptr;
		if (Material && SlotIndex < static_cast<int32>(Material->Textures.Num()))
		{
			Texture = Material->Textures[SlotIndex];
		}

		ImGui::PushID(SlotIndex);

		if (Texture && Texture->GetResource())
		{
			ImGui::Image(
				(ImTextureID)Texture->GetResource()->GetSRV(),
				{ ThumbnailSize, ThumbnailSize });
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		// 바로 위 위젯이 드롭을 받는다
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* DroppedTexture = *static_cast<UTexture2D**>(Payload->Data);
				if (Texture != DroppedTexture)
				{
					Edit.Changes.Add([MaterialPtr, SlotIndex, DroppedTexture]
					{
						UMaterial* Editable = *MaterialPtr;
						if (!Editable)
							Editable = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
						else if (!Editable->bIsInstance)
							Editable = UMaterial::CreateInstance(Editable);
						if (Editable)
						{
							*MaterialPtr = Editable;
							while (!Editable->Textures.IsValidIndex(SlotIndex))
								Editable->Textures.Add(nullptr);
							Editable->Textures[SlotIndex] = DroppedTexture;
						}
					});
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();

		ImGui::BeginGroup();
		ImGui::Text("Slot %d", SlotIndex);

		if (Texture)
		{
			ImGui::TextWrapped("%s", Texture->GetPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("(비어 있음)");
		}
		ImGui::EndGroup();

		ImGui::PopID();
	}

	FString GetAssetDisplayName(const FString& Path)
	{
		fs::path AssetPath(Path);
		if (AssetPath.stem() == "Atlas")
		{
			return AssetPath.parent_path().filename().string();
		}
		return AssetPath.stem().string();
	}

	UFont* DrawFontSlot(UFont* Current)
	{
		UFont* Picked = nullptr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##Font", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UFont> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected) && !bSelected)
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					Picked = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}

			// 아직 로드되지 않은 폰트는 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Font))
			{
				ImGui::PushID(ItemPath.c_str());
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
					Picked = UAssetManager::GetAssetByPath<UFont>(ItemPath);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return Picked;
	}

	void DrawSkeletalMeshSlot(USkinnedMeshComponent* Component)
	{
		USkeletalMesh* Current = Component->GetSkeletalMesh();
		const FString CurrentPath = Current ? Current->GetPath() : "";
		const FString CurrentName = Current ? (CurrentPath.empty() ? Current->GetName() : GetAssetDisplayName(CurrentPath)) : "None";
		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen = ImGui::BeginCombo("##SkeletalMesh", CurrentName.c_str());
		if (ImGui::IsItemHovered() && Current) ImGui::SetTooltip("%s", CurrentPath.c_str());
		if (!bOpen) return;
		USkeletalMesh* Picked = Current;
		if (ImGui::Selectable("None", Current == nullptr)) Picked = nullptr;
		for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
		{
			const FString Path = Itr->GetPath();
			if (Path.empty() || UAssetManager::FindLoadedAsset(Path) != *Itr) continue;
			const bool bSelected = *Itr == Current;
			ImGui::PushID(*Itr);
			if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected)) Picked = *Itr;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
			if (bSelected) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}

		// 아직 로드되지 않은 메시는 경로만 보여 주고 고를 때 로드한다
		for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::SkeletalMesh))
		{
			ImGui::PushID(ItemPath.c_str());
			if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
			{
				if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(ItemPath))
				{
					Picked = Mesh;
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
			ImGui::PopID();
		}
		ImGui::EndCombo();
		if (Picked != Current)
		{
			FComponentReregisterContext Reregister(Component);
			Component->SetSkeletalMesh(Picked);
		}
	}

	// 애니메이션 에셋 선택. Owner가 SkeletalMeshComponent면 메시와 같은 Skeleton의 애니메이션만 보여 준다
	// (재생 시 UAnimSingleNodeInstance가 같은 Skeleton 에셋인지 검사하므로 다른 것은 골라도 재생되지 않는다).
	bool DrawAnimationSlot(UObject* Owner, UAnimationAsset** AnimPtr)
	{
		const USkeletalMeshComponent* MeshComponent = Cast<USkeletalMeshComponent>(Owner);
		const USkeletalMesh* Mesh = MeshComponent ? MeshComponent->GetSkeletalMesh() : nullptr;
		const USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
		auto IsCompatible = [MeshComponent, Skeleton](const UAnimationAsset* Anim)
		{
			return !MeshComponent || (Skeleton && Anim->GetSkeleton() == Skeleton);
		};

		UAnimationAsset* Current = *AnimPtr;
		const FString CurrentPath = Current ? Current->GetPath() : "";
		const FString CurrentName = Current ? GetAssetDisplayName(CurrentPath) : "None";
		bool bChanged = false;

		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen = ImGui::BeginCombo("##AnimToPlay", CurrentName.c_str());
		if (ImGui::IsItemHovered() && Current) ImGui::SetTooltip("%s", CurrentPath.c_str());
		if (bOpen)
		{
			if (ImGui::Selectable("None", Current == nullptr))
			{
				*AnimPtr = nullptr;
				bChanged = true;
			}
			if (MeshComponent && !Skeleton)
			{
				ImGui::TextDisabled("Set a skeletal mesh first.");
			}
			else
			{
				// Skeleton은 파일을 읽어야 알 수 있으므로, 목록을 열 때 아직 로드되지 않은 애니메이션을 로드한다.
				for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::AnimSequence))
					UAssetManager::GetAssetByPath<UAnimationAsset>(ItemPath);

				int32 Shown = 0;
				for (TObjectIterator<UAnimationAsset> Itr; Itr; ++Itr)
				{
					const FString Path = Itr->GetPath();
					if (Path.empty() || UAssetManager::FindLoadedAsset(Path) != *Itr || !IsCompatible(*Itr)) continue;
					const bool bSelected = *Itr == Current;
					ImGui::PushID(*Itr);
					if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected))
					{
						*AnimPtr = *Itr;
						bChanged = true;
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
					if (bSelected) ImGui::SetItemDefaultFocus();
					ImGui::PopID();
					++Shown;
				}
				if (Shown == 0) ImGui::TextDisabled("No animation uses this mesh's Skeleton.");
			}
			ImGui::EndCombo();
		}

		// 메시를 바꿔 Skeleton이 달라졌으면 알려 준다.
		if (*AnimPtr && !IsCompatible(*AnimPtr))
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Animation uses a different Skeleton; it will not play.");
		return bChanged;
	}

	UStaticMesh* DrawMeshSlot(UStaticMesh* Current)
	{
		UStaticMesh* Picked = nullptr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##StaticMesh", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UStaticMesh> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);
				if (!ItemPath.empty() && UAssetManager::FindLoadedAsset(ItemPath) != *Itr) continue;

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected) && !bSelected)
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					Picked = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}

			// 아직 로드되지 않은 메시는 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::StaticMesh))
			{
				ImGui::PushID(ItemPath.c_str());
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
					Picked = UAssetManager::GetAssetByPath<UStaticMesh>(ItemPath);
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return Picked;
	}

	UMaterial* EnsureMaterialOverride(UMeshComponent* MeshComponent, int32 Slot, UMaterial* Effective, UMaterial* Override)
	{
		// 이 컴포넌트 전용 인스턴스가 아니면 지금 적용 중인 머티리얼을 복제해서 덮어쓰기로 만든다.
		// 메시의 머티리얼은 같은 메시를 쓰는 모든 액터가 공유하므로 직접 고치면 안 된다.
		if (Override && Override->bIsInstance)	return Override;

		UMaterial* Source = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

		Override = UMaterial::CreateInstance(Source);
		MeshComponent->SetMaterial(Slot, Override);

		return Override;
	}

	bool SetMaterialTexture(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, UTexture2D* Texture, UMaterial*& Effective, UMaterial*& Override) {
		UTexture2D* Current{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };
		if (Current == Texture) {
			return false;
		}

		FComponentReregisterContext Reregister(MeshComponent);
		Override = EnsureMaterialOverride(MeshComponent, MaterialSlot, Effective, Override);
		if (!Override) {
			return false;
		}

		while (!Override->Textures.IsValidIndex(TextureSlot)) {
			Override->Textures.Add(nullptr);
		}
		Override->Textures[TextureSlot] = Texture;
		Effective = Override;
		return true;
	}

	bool DrawTexturePicker(UTexture2D* Current, const char* Label,
		const std::function<void(UTexture2D*)>& OnPick)
	{
		const float ThumbnailSize{ 64.0f };
		const float ItemThumbnailSize{ 24.0f };

		bool bChanged{ false };

		if (Current && Current->GetResource())
		{
			ImGui::Image((ImTextureID)Current->GetResource()->GetSRV(), { ThumbnailSize, ThumbnailSize });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Current->GetPath().c_str());
			}
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload * Payload{ ImGui::AcceptDragDropPayload(EditorDragDrop::Texture) })
			{
				UTexture2D* DroppedTexture{ *static_cast<UTexture2D**>(Payload->Data) };
				if (DroppedTexture != Current)
				{
					OnPick(DroppedTexture);
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		const FString CurrentPath{ Current ? Current->GetPath() : "" };
		const FString CurrentFileName{ Current ? GetAssetDisplayName(CurrentPath) : "None" };

		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen{ ImGui::BeginCombo(Label, CurrentFileName.c_str()) };
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			if (ImGui::Selectable("None", Current == nullptr) && Current != nullptr)
			{
				OnPick(nullptr);
				bChanged = true;
			}

			// 경로 없는 런타임 텍스처(렌더타겟 등)는 목록에서 뺀다.
			for (TObjectIterator<UTexture2D> Iterator{}; Iterator; ++Iterator)
			{
				UTexture2D* ItemTexture{ *Iterator };
				if (!ItemTexture->GetResource() || ItemTexture->GetPath().empty() ||
					UAssetManager::FindLoadedAsset(ItemTexture->GetPath()) != ItemTexture)
				{
					continue;
				}

				const bool bSelected{ ItemTexture == Current };
				const FString ItemPath{ ItemTexture->GetPath() };
				const FString ItemFileName{ GetAssetDisplayName(ItemPath) };

				ImGui::PushID(ItemTexture);
				ImGui::Image((ImTextureID)ItemTexture->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(ItemFileName.c_str(), bSelected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize })
					&& !bSelected)
				{
					OnPick(ItemTexture);
					bChanged = true;
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}

			// 아직 로드되지 않은 텍스처는 썸네일 없이 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Texture))
			{
				ImGui::PushID(ItemPath.c_str());
				ImGui::Dummy({ ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize }))
				{
					if (UTexture2D* ItemTexture{ UAssetManager::GetAssetByPath<UTexture2D>(ItemPath) })
					{
						OnPick(ItemTexture);
						bChanged = true;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		return bChanged;
	}

	void DrawMaterialTextureSlot(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, const char* Label, UMaterial*& Effective, UMaterial*& Override) {
		const float ThumbnailSize{ 64.0f };
		const float ItemThumbnailSize{ 24.0f };
		UTexture2D* Texture{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::PushID(TextureSlot);

		DrawTexturePicker(Texture, "##Texture",
			[&](UTexture2D* Picked)
			{
				SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, Picked, Effective, Override);
			});

		ImGui::PopID();
	}

	// 메시 컴포넌트의 머티리얼 슬롯을 한 줄씩 그린다.
	// 텍스처를 드롭하면 그 슬롯의 덮어쓰기에 반영하고, 메시 에셋의 머티리얼은 건드리지 않는다.
	void DrawMaterialSlots(UMeshComponent* MeshComponent)
	{
		const int32 NumSlots = MeshComponent->GetNumMaterials();
		if (NumSlots <= 0)
		{
			return;
		}

		if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		// 메쉬 내의 매터리얼 별 Slot 순회
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			ImGui::PushID(Slot);

			FString SlotLabel = std::format("[{}] {}", Slot, MeshComponent->GetMaterialSlotName(Slot));
			bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

			UMaterial* Override = MeshComponent->GetOverrideMaterial(Slot);
			UMaterial* Effective = MeshComponent->GetMaterial(Slot);

			ImGui::SameLine();

			if (Override)
			{
				ImGui::TextDisabled("Override");
				ImGui::SameLine();

				if (ImGui::SmallButton("Reset"))
				{
					FComponentReregisterContext Reregister(MeshComponent);
					MeshComponent->SetMaterial(Slot, nullptr);
					Override = nullptr;
					Effective = MeshComponent->GetMaterial(Slot);
				}
			}
			else
			{
				ImGui::TextDisabled("Default");
			}

			if (bOpen)
			{
				if (ImGui::BeginTable("MaterialProperties", 2))
				{
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted("Shading Model");
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					const char* Models[] = { "Gouraud", "Lambert", "Blinn-Phong", "Unlit" };
					int ModelIndex = static_cast<int>(Effective ? Effective->ShadingModel : ELightingModel::BlinnPhong);
					if (ImGui::Combo("##ShadingModel", &ModelIndex, Models, IM_ARRAYSIZE(Models)))
					{
						FComponentReregisterContext Reregister(MeshComponent);
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->ShadingModel = static_cast<ELightingModel>(ModelIndex);
							Effective = Override;
						}
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Used by opaque meshes in Forward when Shading Model Override is None.");

					DrawMaterialTextureSlot(MeshComponent, Slot, 0, "Texture", Effective, Override);
					DrawMaterialTextureSlot(MeshComponent, Slot, 1, "Normal Map", Effective, Override);

					ImGui::TableNextRow();

					// 3: Base Color
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Base Color");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector4 TempColor = Effective ? Effective->BaseColor : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					if (ImGui::ColorEdit4("##BaseColor", &TempColor.X))
					{
						FComponentReregisterContext Reregister(MeshComponent);
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->BaseColor = TempColor;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Shininess");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					float TempShininess = Effective ? Effective->Shininess : 32.0f;
					if (ImGui::DragFloat("##Shininess", &TempShininess, 0.5f, 1.0f, 256.0f))
					{
						FComponentReregisterContext Reregister(MeshComponent);
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->Shininess = TempShininess;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 4: UV Scroll Speed
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("UV Scroll");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector2 TempUVScrollSpeed = Effective ? Effective->UVScrollSpeed : FVector2();
					if (ImGui::DragFloat2("##UVScrollSpeed", &TempUVScrollSpeed.X, 0.01f))
					{
						FComponentReregisterContext Reregister(MeshComponent);
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->UVScrollSpeed = TempUVScrollSpeed;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 5: Sampler State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Sampler");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
					int SamplerIndex = Effective ? static_cast<int>(Effective->SamplerState) : static_cast<int>(ESamplerState::LinearClamp);
					if (ImGui::BeginCombo("##SamplerState", SamplerItems[SamplerIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(SamplerItems); ++i)
						{
							if (ImGui::Selectable(SamplerItems[i], SamplerIndex == i) && SamplerIndex != i)
							{
								FComponentReregisterContext Reregister(MeshComponent);
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SamplerState = static_cast<ESamplerState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::TableNextRow();

					// 6: Blend State
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("Blend Mode");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* BlendItems[] = { "Opaque", "Alpha Blend" };
					int BlendIndex = Effective ? static_cast<int>(Effective->BlendState) : static_cast<int>(EBlendState::Opaque);
					if (ImGui::BeginCombo("##BlendState", BlendItems[BlendIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
						{
							if (ImGui::Selectable(BlendItems[i], BlendIndex == i) && BlendIndex != i)
							{
								FComponentReregisterContext Reregister(MeshComponent);
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->BlendState = static_cast<EBlendState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();

			ImGui::Separator();
		}
	}

	uint32 ToUIntBound(float Value)
	{
		if (Value <= 0.0f) return 0;
		if (Value >= static_cast<float>(UINT_MAX)) return UINT_MAX;
		return static_cast<uint32>(Value);
	}

	void DrawPropertyRow(FPropertyEdit& Edit, UObject* Object, void* Container, const FProperty& Property, ImFont* CustomFont);

	// Draw against copies so unregister still sees the previous value.
	void DrawPropertyValue(FPropertyEdit& Edit, UObject* Object, const FProperty& Property,
		void* ValuePtr, const char* Label, ImFont* CustomFont)
	{
		if (const auto* FloatProperty = CastField<FFloatProperty>(&Property))
		{
			Edit.DrawValue(FloatProperty->GetPropertyValue(ValuePtr), [&](float& Value)
			{
				const FDragSettings Drag = GetDragSettings(FloatProperty->Meta, 0.1f);
				const bool bChanged = ImGui::DragFloat(Label, &Value, Drag.Speed, Drag.Min, Drag.Max);
				if (bChanged) ClampToMeta(&Value, 1, FloatProperty->Meta);
				return bChanged;
			});
		}
		else if (const auto* IntProperty = CastField<FIntProperty>(&Property))
		{
			Edit.DrawValue(IntProperty->GetPropertyValue(ValuePtr), [&](int32& Value)
			{
				const FPropertyMeta& Meta = IntProperty->Meta;
				const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
				const bool bChanged = ImGui::DragInt(Label, &Value, Drag.Speed, ToIntBound(Drag.Min), ToIntBound(Drag.Max));
				if (bChanged && Meta.HasClamp())
					Value = ToIntBound(std::clamp(static_cast<float>(Value), Meta.ClampMin, Meta.ClampMax));
				return bChanged;
			});
		}
		else if (const auto* UIntProperty = CastField<FUInt32Property>(&Property))
		{
			Edit.DrawValue(UIntProperty->GetPropertyValue(ValuePtr), [&](uint32& Value)
			{
				const FPropertyMeta& Meta = UIntProperty->Meta;
				const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
				const uint32 Min = ToUIntBound(Drag.Min);
				const uint32 Max = Drag.Min < Drag.Max ? ToUIntBound(Drag.Max) : UINT_MAX;
				const bool bChanged = ImGui::DragScalar(Label, ImGuiDataType_U32, &Value, Drag.Speed, &Min, &Max);
				if (bChanged && Meta.HasClamp())
					Value = ToUIntBound(std::clamp(static_cast<float>(Value), Meta.ClampMin, Meta.ClampMax));
				return bChanged;
			});
		}
		else if (const auto* VectorProperty = CastField<FVectorProperty>(&Property))
		{
			Edit.DrawValue(VectorProperty->GetPropertyValue(ValuePtr), [&](FVector& Value)
			{
				const FDragSettings Drag = GetDragSettings(VectorProperty->Meta, 0.1f);
				const bool bChanged = ImGui::DragFloat3(Label, Value.V, Drag.Speed, Drag.Min, Drag.Max);
				if (bChanged) ClampToMeta(Value.V, 3, VectorProperty->Meta);
				return bChanged;
			});
		}
		else if (const auto* RotatorProperty = CastField<FRotatorProperty>(&Property))
		{
			Edit.DrawValue(RotatorProperty->GetPropertyValue(ValuePtr), [&](FRotator& Value)
			{
				// Show the axes in the same order as the transform editor.
				float Euler[3] = { Value.Roll, Value.Pitch, Value.Yaw };
				const FDragSettings Drag = GetDragSettings(RotatorProperty->Meta, 0.1f);
				if (!ImGui::DragFloat3(Label, Euler, Drag.Speed, Drag.Min, Drag.Max))
					return false;
				ClampToMeta(Euler, 3, RotatorProperty->Meta);
				Value.Roll = Euler[0];
				Value.Pitch = Euler[1];
				Value.Yaw = Euler[2];
				return true;
			});
		}
		else if (const auto* Vector4Property = CastField<FVector4Property>(&Property))
		{
			Edit.DrawValue(Vector4Property->GetPropertyValue(ValuePtr), [&](FVector4& Value)
			{
				const FDragSettings Drag = GetDragSettings(Vector4Property->Meta, 0.1f);
				const bool bChanged = ImGui::DragFloat4(Label, &Value.X, Drag.Speed, Drag.Min, Drag.Max);
				if (bChanged) ClampToMeta(&Value.X, 4, Vector4Property->Meta);
				return bChanged;
			});
		}
		else if (const auto* BoolProperty = CastField<FBoolProperty>(&Property))
		{
			Edit.DrawValue(BoolProperty->GetPropertyValue(ValuePtr),
				[&](bool& Value) { return ImGui::Checkbox(Label, &Value); });
		}
		else if (const auto* EnumProperty = CastField<FEnumProperty>(&Property))
		{
			const FEnumInfo* EnumInfo = EnumProperty->Enum;
			if (!EnumInfo)
			{
				ImGui::TextDisabled("(No enum info)");
				return;
			}

			const int64 Current = EnumProperty->GetValue(ValuePtr);
			const FString Preview = EnumInfo->ToName(Current);
			if (ImGui::BeginCombo(Label, Preview.c_str()))
			{
				for (const FEnumEntry& Entry : EnumInfo->Entries)
				{
					if (Entry.bHidden)
						continue;
					const bool bSelected = Entry.Value == Current;
					if (ImGui::Selectable(Entry.Name.c_str(), bSelected) && !bSelected)
					{
						Edit.Changes.Add([EnumProperty, ValuePtr, Value = Entry.Value]
						{
							EnumProperty->SetValue(ValuePtr, Value);
						});
					}
					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		else if (const auto* ColorProperty = CastField<FColorProperty>(&Property))
		{
			Edit.DrawValue(ColorProperty->GetPropertyValue(ValuePtr), [&](FColor& Value)
			{
				float V[4] = { Value.R / 255.f, Value.G / 255.f, Value.B / 255.f, Value.A / 255.f };
				if (!ImGui::ColorEdit4(Label, V))
					return false;
				Value = FColor(uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
					uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f));
				return true;
			});
		}
		else if (const auto* LinearColorProperty = CastField<FLinearColorProperty>(&Property))
		{
			Edit.DrawValue(LinearColorProperty->GetPropertyValue(ValuePtr),
				[&](FLinearColor& Value) { return ImGui::ColorEdit4(Label, Value.RGBA); });
		}
		else if (const auto* StrProperty = CastField<FStrProperty>(&Property))
		{
			Edit.DrawValue(StrProperty->GetPropertyValue(ValuePtr), [&](FString& Value)
			{
				char Buffer[256] = {};
				strncpy_s(Buffer, Value.c_str(), sizeof(Buffer) - 1);
				if (CustomFont) ImGui::PushFont(CustomFont);
				const bool bChanged = ImGui::InputText(Label, Buffer, sizeof(Buffer));
				if (CustomFont) ImGui::PopFont();
				if (bChanged) Value = Buffer;
				return bChanged;
			});
		}
		else if (const auto* TransformProperty = CastField<FTransformProperty>(&Property))
		{
			Edit.DrawValue(TransformProperty->GetPropertyValue(ValuePtr), [&](FTransform& Value)
			{
				ImGui::NewLine();
				bool bChanged = DrawVector3Controller("Location", Value.Location.V, 0.0f, 55.0f);
				bChanged |= DrawRotatorAsXYZ("Rotation", Value.Rotation);
				bChanged |= DrawVector3Controller("Scale", Value.Scale.V, 1.0f, 55.0f);
				return bChanged;
			});
		}
		else if (const auto* ObjectProperty = CastField<FObjectProperty>(&Property))
		{
			UObject** ObjPtr = &ObjectProperty->GetPropertyValue(ValuePtr);
			const UClass* PropertyClass = ObjectProperty->PropertyClass;
			if (PropertyClass == UMaterial::StaticClass())
			{
				DrawTextureSlot(Edit, reinterpret_cast<UMaterial**>(ObjPtr), 0);
			}
			else if (PropertyClass == UFont::StaticClass())
			{
				if (UFont* Picked = DrawFontSlot(Cast<UFont>(*ObjPtr)))
				{
					Edit.Changes.Add([Object, ObjPtr, Picked]
					{
						if (auto* Component = Cast<UTextRenderComponent>(Object))
							Component->SetFont(Picked);
						else
							*ObjPtr = Picked;
					});
				}
			}
			else if (PropertyClass == UStaticMesh::StaticClass())
			{
				if (UStaticMesh* Picked = DrawMeshSlot(Cast<UStaticMesh>(*ObjPtr)))
				{
					Edit.Changes.Add([Object, ObjPtr, Picked]
					{
						if (auto* Component = Cast<UStaticMeshComponent>(Object))
							Component->SetStaticMesh(Picked);
						else
							*ObjPtr = Picked;
					});
				}
			}
			else if (PropertyClass == UAnimationAsset::StaticClass())
			{
				// main의 지연 적용 방식(FPropertyEdit)에 맞춘다: 복사본에 고른 뒤 Changes로 반영한다.
				Edit.DrawValue(*reinterpret_cast<UAnimationAsset**>(ObjPtr), [&](UAnimationAsset*& Value)
				{
					return DrawAnimationSlot(Object, &Value);
				});
			}
			else if (PropertyClass == UTexture2D::StaticClass())
			{
				DrawTexturePicker(Cast<UTexture2D>(*ObjPtr), Label, [&](UTexture2D* Picked)
				{
					if (*ObjPtr != Picked)
						Edit.Changes.Add([ObjPtr, Picked] { *ObjPtr = Picked; });
				});
			}
		}
		else if (const auto* StructProperty = CastField<FStructProperty>(&Property))
		{
			// Struct members use the same edit as their enclosing property.
			ImGui::NewLine();
			ImGui::Indent();
			ImGui::PushID(Label);

			TArray<const UStruct*> StructChain;
			for (const UStruct* Struct = StructProperty->Struct; Struct; Struct = Struct->GetSuperStruct())
				StructChain.Add(Struct);

			for (auto It = StructChain.rbegin(); It != StructChain.rend(); ++It)
			{
				for (const FProperty* Member : (*It)->GetProperties())
				{
					if (!Member->HasAnyFlags(CPF_Edit))
						continue;
					const bool bReadOnly = Member->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					DrawPropertyRow(Edit, Object, ValuePtr, *Member, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}

			ImGui::PopID();
			ImGui::Unindent();
		}
		else
		{
			ImGui::TextDisabled("(Unsupported %s)", Property.GetClass()->GetName());
		}
	}

	// Container is either the UObject or the address of a reflected struct.
	void DrawPropertyRow(FPropertyEdit& Edit, UObject* Object, void* Container, const FProperty& Property, ImFont* CustomFont)
	{
		void* ValuePtr = Property.ContainerPtrToValuePtr(Container);
		const FString Label = "##" + Property.Name;
		const char* DisplayName = Property.Name.c_str();

		const float StartX = ImGui::GetCursorPosX();
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		const float LabelWidth = AvailableWidth * 0.45f;
		const bool bInline = !Property.IsA<FTransformProperty>() && !Property.IsA<FStructProperty>() && AvailableWidth >= 260.0f && ImGui::CalcTextSize(DisplayName).x < LabelWidth;

		ImGui::AlignTextToFramePadding();
		ImGui::TextWrapped("%s", DisplayName);
		if (bInline)
			ImGui::SameLine(StartX + LabelWidth);

		ImGui::SetNextItemWidth(-1.0f);
		DrawPropertyValue(Edit, Object, Property, ValuePtr, Label.c_str(), CustomFont);
	}

	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		FPropertyEdit Edit;
		DrawPropertyRow(Edit, Object, Object, Property, CustomFont);
		if (!Edit.Changes.IsEmpty())
		{
			FComponentReregisterContext Reregister(Cast<UActorComponent>(Object));
			for (const auto& Change : Edit.Changes)
				Change();
			Object->PostEditChangeProperty(Property);
		}
	}

	// 클래스 계층을 따라 올라가며 각 단계의 프로퍼티를 표시
	void DrawProperties(UObject* Object, ImFont* CustomFont)
	{
		if (!Object)
		{
			return;
		}

		TArray<UClass*> ClassChain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			ClassChain.Add(Class);
		}

		// 기반 클래스부터 표시
		for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
		{
			UClass* Class = *It;

			// 노출할 프로퍼티가 하나도 없으면 헤더도 그리지 않는다
			bool bHasVisibleProperty = false;
			for (const FProperty* Property : Class->GetProperties())
			{
				if (Property->HasAnyFlags(CPF_Edit))
				{
					bHasVisibleProperty = true;
					break;
				}
			}
			if (!bHasVisibleProperty)
			{
				continue;
			}

			ImGui::PushID(Class->Name.c_str());
			if (ImGui::CollapsingHeader(Class->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const FProperty* Property : Class->GetProperties())
				{
					if (!Property->HasAnyFlags(CPF_Edit))
					{
						continue;
					}

					const bool bReadOnly = Property->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					DrawProperty(Object, *Property, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}
			ImGui::PopID();
		}
	}

	void DrawActorHeader(AActor* Actor, UActorComponent*& OutSelectedComponent)
	{
		if (!Actor) { return; }

		static char ComponentNameInputBuf[256] = {};

		ImGui::Text("Actor: %s", Actor->GetName().c_str());
		ImGui::Text("UUID: %u", Actor->GetUUID());

		if (ImGui::Button("Add Component"))
		{
			ImGui::OpenPopup("AddComponentPopup");
		}

		if (ImGui::BeginPopup("AddComponentPopup"))
		{
			ImGui::Text("Component Name:");
			ImGui::SameLine();
			ImGui::InputText("##Component Name", ComponentNameInputBuf, sizeof(ComponentNameInputBuf));
			TArray<UClass*> Classes;
			GetDerivedClasses(UActorComponent::StaticClass(), Classes);
			// 이름순 정렬하면 메뉴가 안정적
			for (UClass* Class : Classes)
			{
				//if (!IsSpawnableComponent(Class)) continue;
				if (ImGui::MenuItem(Class->Name.c_str()))
				{
					OutSelectedComponent = Actor->AddComponent(Class, FString(ComponentNameInputBuf));
					ComponentNameInputBuf[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
		DrawScriptSection(Actor);
	}
}

// UI 에디터처럼 다른 패널도 같은 속성 편집 UI를 쓰도록 위 함수들을 공개한다.
// 보조 함수들이 이 파일의 익명 네임스페이스에 있어 구현은 옮기지 않고 넘겨 준다.
namespace FPropertyEditor
{
	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		::DrawProperty(Object, Property, CustomFont);
	}

	void DrawObjectProperties(UObject* Object, ImFont* CustomFont)
	{
		DrawProperties(Object, CustomFont);
	}
}

bool FDetailsPanel::Init()
{
	ImGuiIO& io = ImGui::GetIO();

	io.Fonts->AddFontDefault();

	const char* fontPath = "HMKMRHD.ttf";
	float fontSize = 15.0f;
	const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

	CustomFont = io.Fonts->AddFontFromFileTTF(fontPath, fontSize, nullptr, koreanRanges);
	assert(CustomFont != nullptr);

	return true;
}

void FDetailsPanel::Tick(float DeltaTime)
{
}

const ULightComponent* FDetailsPanel::GetSelectedLight() const
{
	UActorComponent* Component = TargetComponent;
	while (Component && Component->IsHiddenInDetails())
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		Component = SceneComponent ? SceneComponent->GetAttachParent() : nullptr;
	}
	return Cast<ULightComponent>(Component);
}

void FDetailsPanel::DrawShadowPreview()
{
	const ULightComponent* Light = GetSelectedLight();
	if (!Light || !QueryShadowPreview || !ImGuiRenderer) return;
	if (!ImGui::CollapsingHeader("Shadow Maps", ImGuiTreeNodeFlags_DefaultOpen)) return;
	FTexture2DArray* Atlas = nullptr;
	TArray<FLightShadowRenderData> Shadows;
	if (!QueryShadowPreview(Light, Atlas, Shadows) || !Atlas || !Atlas->GetWidth() || !Atlas->GetHeight())
	{
		ImGui::TextDisabled("No shadow map available for this light in the active view.");
		return;
	}
	ImGui::SliderFloat("Preview Contrast", &ShadowPreviewContrast, 1.0f, 256.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	const float Width = float(Atlas->GetWidth());
	const float Height = float(Atlas->GetHeight());
	const float DisplayWidth = ImGui::GetContentRegionAvail().x;
	if (DisplayWidth <= 0) return;
	static const char* PointFaces[] = { "+X", "-X", "+Z", "-Z", "+Y", "-Y" };
	for (int32 i = 0; i < Shadows.Num(); ++i)
	{
		const auto& Shadow = Shadows[i];
		const FRect& Rect = Shadow.AtlasRect;
		if (Rect.Width <= 0 || Rect.Height <= 0) continue;
		ImGui::PushID(i);
		if (Light->GetLightType() == ELightType::Point)
			ImGui::Text("Face %s", PointFaces[i % 6]);
		else if (Light->GetLightType() == ELightType::Directional)
			ImGui::Text("Cascade %d", i + 1);
		ImGui::TextDisabled("Slice %u | %.0f x %.0f", Shadow.AtlasIndex, Rect.Width, Rect.Height);

		const ImVec2 UV0(Rect.X / Width, Rect.Y / Height);
		const ImVec2 UV1((Rect.X + Rect.Width) / Width, (Rect.Y + Rect.Height) / Height);

		if (!ImGuiRenderer->DrawShadowMap(Atlas->GetSRV(), Shadow.AtlasIndex,
			ImVec2(DisplayWidth, DisplayWidth * Rect.Height / Rect.Width), UV0, UV1, ShadowPreviewContrast))
			ImGui::TextDisabled("Shadow preview shader is unavailable.");
		ImGui::PopID();
	}
}


void FDetailsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);

	ImGui::Begin("Details");

	if (TargetActor)
	{
		// 빌보드처럼 디테일에 숨겨진 컴포넌트가 선택되면 노출되는 부모로 올린다.
		// (예: 라이트는 빌보드를 클릭해서 고르지만 수치는 SpotLight 쪽에 있다)
		while (TargetComponent && TargetComponent->IsHiddenInDetails())
		{
			USceneComponent* HiddenComponent = Cast<USceneComponent>(TargetComponent);
			TargetComponent = HiddenComponent ? HiddenComponent->GetAttachParent() : nullptr;
		}

		UActorComponent* SelectCandidateComponent = TargetComponent;

		DrawActorHeader(TargetActor, TargetComponent);
		DrawCompoenetList(TargetActor, TargetComponent);
		ImGui::Separator();

		// 액터 -> 컴포넌트 순으로, 클래스별 프로퍼티 표시
		ImGui::PushID(TargetActor);
		DrawProperties(TargetActor, CustomFont);
		ImGui::PopID();

		if (TargetComponent)
		{
			ImGui::PushID(TargetComponent);
			ImGui::SeparatorText("Component Properties");
			DrawProperties(TargetComponent, CustomFont);
			if (auto* Component = Cast<USkinnedMeshComponent>(TargetComponent))
			{
				ImGui::TextUnformatted("Skeletal Mesh");
				DrawSkeletalMeshSlot(Component);
				if (auto* SkeletalMesh{Cast<USkeletalMeshComponent>(Component)}) {
					DrawAnimGraphSlot(SkeletalMesh);
				}
			}
			DrawShadowPreview();

			if (SelectCandidateComponent != TargetComponent)
			{
				SelectComponent(TargetComponent);
			}
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TargetComponent))
			{
				DrawMaterialSlots(MeshComponent);
			}
			ImGui::PopID();

		}

		if (PendingDeleteComponent)
		{
			UActorComponent* Component = PendingDeleteComponent;
			PendingDeleteComponent = nullptr;
			if (DeleteCallback)
				DeleteCallback(Component);
		}
	}
	ImGui::End();
}


void FDetailsPanel::SelectComponent(UActorComponent* Component)
{
	if (!Component)
	{
		TargetComponent = nullptr;
		EditingNameComponent = nullptr;
		bFocusNameEdit = false;

		if (Callback)
			Callback(nullptr);

		return;
	}

	TargetComponent = Component;

	if (Callback)
		Callback(TargetComponent);
}

namespace
{
	void DrawComponentRowTooltip(const UActorComponent* Component)
	{
		if (ImGui::GetDragDropPayload() || !ImGui::BeginItemTooltip())
			return;

		const AActor* Owner = Component->GetOwner();
		if (Component->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
		{
			ImGui::TextUnformatted("Default subobject: Yes");
			ImGui::TextUnformatted("This component cannot be deleted in the editor.");
			if (Owner && Owner->GetRootComponent() == Component)
				ImGui::TextUnformatted("This class-defined root cannot be replaced in the editor.");
		}
		else
		{
			ImGui::TextUnformatted("Default subobject: No");
			if (Owner && Owner->IsDefaultSceneRoot(Component))
				ImGui::TextUnformatted("This fallback root is removed when another component is made the root.");
		}
		ImGui::EndTooltip();
	}

	USceneComponent* FindSceneComponentById(AActor* Actor, uint32 Id)
	{
		for (UActorComponent* Component : Actor->GetComponents())
			if (Component && Component->GetUUID() == Id)
				return Cast<USceneComponent>(Component);
		return nullptr;
	}
}

void FDetailsPanel::DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent)
{
	if (!SelectedActor) { return; }

	USceneComponent* Root = SelectedActor->GetRootComponent();

	const bool bShowComponents = ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen);
	if (!Root && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
		{
			PendingDroppedComponentId = *static_cast<const uint32*>(Payload->Data);
			PendingDragComponent = nullptr;
		}
		ImGui::EndDragDropTarget();
	}

	if (bShowComponents)
	{
		DrawSceneComponentNode(Root, Root, OutSelectedComponent);
		for (UActorComponent* Component : SelectedActor->GetComponents())
		{
			auto* Scene = Cast<USceneComponent>(Component);
			if (Scene && Scene != Root)
			{
				USceneComponent* Parent = Scene->GetAttachParent();
				if (!Parent || Parent->GetOwner() != SelectedActor || Parent->IsHiddenInDetails())
					DrawSceneComponentNode(Scene, Root, OutSelectedComponent);
			}
		}
	}

	ImGui::Separator();

	// Draw ActorComponent
	for (UActorComponent* ActorComp : SelectedActor->GetComponents())
	{
		if (!ActorComp || Cast<USceneComponent>(ActorComp))
		{
			continue;
		}

		ImGui::PushID(ActorComp);
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (ActorComp == OutSelectedComponent)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		FString Label = ActorComp->GetName();
		const ImVec2 RowStart = ImGui::GetCursorScreenPos();
		const float RowWidth = ImGui::GetContentRegionAvail().x;

		ImGui::TreeNodeEx("ActorComponentNode", Flags, "%s", Label.c_str());
		DrawComponentRowTooltip(ActorComp);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			OutSelectedComponent = ActorComp;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, SelectedActor->CanDeleteComponentInEditor(ActorComp)))
				PendingDeleteComponent = ActorComp;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", ActorComp->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y), IM_COL32(200, 200, 200, 255), ClassName.c_str());

		ImGui::PopID();
	}

	HandleComponentDrop(SelectedActor, OutSelectedComponent);
}

void FDetailsPanel::DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent)
{
	// 빌보드 같은 에디터 표시용 컴포넌트는 트리에 노출하지 않는다
	if (!Component || Component->IsHiddenInDetails())
		return;

	// 숨겨진 자식만 있는 노드가 펼침 화살표를 갖지 않도록 노출되는 자식만 센다
	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (Child && Child->GetOwner() == Component->GetOwner() && !Child->IsHiddenInDetails())
		{
			Children.Add(Child);
		}
	}

	const bool bIsLeaf = Children.IsEmpty();
	bIsEditingName = (EditingNameComponent == Component);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
	if (Component == OutSelectedComponent)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	FString Label = Component->GetName();
	if (Component == Root)
		Label += " (Root)";

	ImGui::PushID(Component);
	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float RowWidth = ImGui::GetContentRegionAvail().x;
	const bool bOpen = ImGui::TreeNodeEx("ComponentNode", Flags, "%s", bIsEditingName ? "" : Label.c_str());

	AActor* Owner = Component->GetOwner();
	const bool bCanMove = Owner && Owner->CanDeleteComponentInEditor(Component) && Component != Root;
	if (bCanMove && ImGui::BeginDragDropSource())
	{
		const uint32 Id = Component->GetUUID();
		ImGui::SetDragDropPayload(EditorDragDrop::Component, &Id, sizeof(Id));
		ImGui::TextUnformatted(Label.c_str());
		ImGui::EndDragDropSource();
	}
	// target: 모든 행이 가능, payload를 받아 현재 Component를 부모로 사용
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(
			EditorDragDrop::Component, ImGuiDragDropFlags_AcceptBeforeDelivery))
		{
			const uint32 Id = *static_cast<const uint32*>(Payload->Data);
			if (Payload->IsDelivery())
			{
				PendingDroppedComponentId = Id;
				PendingDragComponent = Component;
			}
			else if (Component == Root && Owner)
			{
				if (USceneComponent* Dropped = FindSceneComponentById(Owner, Id))
				{
					if (Owner->CanMakeRootInEditor(Dropped))
						ImGui::SetTooltip("Drop to make this component the root");
					else if (Root->HasAnyFlags(EObjectFlags::RF_DefaultSubObject))
						ImGui::SetTooltip("The class-defined root cannot be replaced");
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (!bIsEditingName)
	{
		DrawComponentRowTooltip(Component);
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			OutSelectedComponent = Component;

		if (bCanMove && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			EditingNameComponent = Component;
			strncpy_s(ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), Component->GetName().c_str(), _TRUNCATE);
			bFocusNameEdit = true;
		}

		if (ImGui::BeginPopupContextItem("Component Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, Owner && Owner->CanDeleteComponentInEditor(Component)))
				PendingDeleteComponent = Component;
			ImGui::EndPopup();
		}

		const FString ClassName = std::format("({})", Component->GetClass()->Name);
		const float TextX = RowStart.x + RowWidth - ImGui::CalcTextSize(ClassName.c_str()).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(TextX, RowStart.y), IM_COL32(200, 200, 200, 255), ClassName.c_str());
	}
	else
	{
		ImGui::SameLine();
		if (bFocusNameEdit)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusNameEdit = false;
		}

		const bool bConfirmed = ImGui::InputText("Rename", ComponentNameEditBuffer,
			sizeof(ComponentNameEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
		const bool bCancelled = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
		const bool bFinished = bConfirmed || ImGui::IsItemDeactivatedAfterEdit();
		if (bCancelled || bFinished)
		{
			if (!bCancelled && ComponentNameEditBuffer[0] != '\0')
			{
				FName NewName(ComponentNameEditBuffer);
				UObject* Existing = StaticFindObjectFast(nullptr, Component->GetOuter(), NewName);
				if (!Existing || Existing == Component)
				{
					Component->SetName(NewName);
				}
				else
				{
					HTR_LOG(Warning, "A component with this name already exists under the same Outer.");
				}
			}
			EditingNameComponent = nullptr;
		}
	}

	if (bOpen && !bIsLeaf)
	{
		for (USceneComponent* Child : Children)
		{
			DrawSceneComponentNode(Child, Root, OutSelectedComponent);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FDetailsPanel::HandleComponentDrop(AActor* Actor, UActorComponent*& Selection)
{
	USceneComponent* Component = PendingDroppedComponentId ? FindSceneComponentById(Actor, PendingDroppedComponentId) : nullptr;
	USceneComponent* Target = PendingDragComponent;
	PendingDroppedComponentId = 0;
	PendingDragComponent = nullptr;

	USceneComponent* Root = Actor->GetRootComponent();
	if (Component && Actor->CanDeleteComponentInEditor(Component) && Component != Root
		&& Component != Target && (!Target || Target->GetOwner() == Actor))
	{
		const bool bCanMakeRoot = Target == Root && Actor->CanMakeRootInEditor(Component);
		const bool bCanAttach = Target && Component->GetAttachParent() != Target;
		if (bCanMakeRoot && bCanAttach)
		{
			RootDropCandidateId = Component->GetUUID();
			ImGui::OpenPopup("Component Drop");
		}
		else if (bCanMakeRoot && Actor->MakeRootInEditor(Component))
		{
			Selection = Component;
		}
		else if (bCanAttach)
		{
			Component->SetupAttachment(Target, EAttachmentRule::KeepWorld);
		}
	}

	if (ImGui::BeginPopup("Component Drop"))
	{
		if (auto* Scene = FindSceneComponentById(Actor, RootDropCandidateId); Actor->CanMakeRootInEditor(Scene))
		{
			if (ImGui::MenuItem("Make New Root") && Actor->MakeRootInEditor(Scene))
				Selection = Scene;
			else if (Root && ImGui::MenuItem("Attach"))
				Scene->SetupAttachment(Root, EAttachmentRule::KeepWorld);
		}
		ImGui::EndPopup();
	}
}

FDetailsPanel::~FDetailsPanel()
{
	//delete transform;
}

UActorComponent* FDetailsPanel::GetSelectedComponent() const {
    return TargetComponent;
}

void FDetailsPanel::SetOpenAnimGraphDebugCallback(std::function<void()> Callback) {
    mOpenAnimGraphDebug = std::move(Callback);
}

void FDetailsPanel::DrawAnimGraphSlot(USkeletalMeshComponent* Component) {
    // 컴포넌트의 그래프 지정과 현재 선택 메시의 PIE 디버그 창 열기를 한곳에 제공한다.
    FString Path{Component->GetAnimGraph()};
    if (DrawAnimationAssetPicker("Anim Graph", Path, EAssetType::AnimGraph)) {
        Component->SetAnimGraph(Path, mAnimGraphError);
    }
    if (!mAnimGraphError.empty()) {
        ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "%s", mAnimGraphError.c_str());
    }
    if (mOpenAnimGraphDebug && ImGui::Button("Anim Graph Debug")) {
        mOpenAnimGraphDebug();
    }
}
