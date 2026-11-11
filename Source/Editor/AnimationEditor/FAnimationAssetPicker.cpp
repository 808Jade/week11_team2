#include "EnginePCH.h"
#include "FAnimationAssetPicker.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxPath.h"
#include <imgui.h>

bool DrawAnimationAssetPicker(const char* Label, FString& Path, EAssetType Type, const char* Preview) {
    const FString Name{Path.empty() ? "None" : FbxPath::ToString(FbxPath::FromString(Path).stem())};
    bool Changed{};
    if (ImGui::BeginCombo(Label, Preview ? Preview : Name.c_str())) {
        if (ImGui::Selectable("None", Path.empty())) {
            Path.clear();
            Changed = true;
        }
        for (const FString& Candidate : UAssetManager::GetAnimationAssetPaths(Type)) {
            ImGui::PushID(Candidate.c_str());
            if (ImGui::Selectable(FbxPath::ToString(FbxPath::FromString(Candidate).stem()).c_str(), Candidate == Path)) {
                Path = Candidate;
                Changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Candidate.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginDragDropTarget()) {
        const char* PayloadType{Type == EAssetType::AnimGraph ? AnimGraphPayload : Type == EAssetType::BlendSpace ? BlendSpacePayload : AnimSequencePayload};
        if (const ImGuiPayload* Payload{ImGui::AcceptDragDropPayload(PayloadType)}; Payload && Payload->DataSize > 1 && static_cast<const char*>(Payload->Data)[Payload->DataSize - 1] == 0) {
            const FString Candidate{static_cast<const char*>(Payload->Data), static_cast<std::size_t>(Payload->DataSize - 1)};
            // 다른 UI에서 넘겨받은 파일 경로를 에셋 매니저가 사용하는 키로 정규화한다.
            Path = FAssetUtils::GetAssetKey(Candidate);
            Changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    return Changed;
}
