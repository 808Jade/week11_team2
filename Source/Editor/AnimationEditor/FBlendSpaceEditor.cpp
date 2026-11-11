#include "EnginePCH.h"
#include "FBlendSpaceEditor.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "FAnimationAssetPicker.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace1D.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "UObject/UObjectGlobals.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace {
    constexpr char WindowId[]{"###BlendSpaceEditor"};

    FString SampleAssetName(const FString& AnimationPath) {
        return FbxPath::ToString(FbxPath::FromString(AnimationPath).stem());
    }

    std::size_t CommonNamePrefix(const std::vector<FBlendSample>& Samples) {
        std::vector<FString> Names{};

        for (const FBlendSample& Sample : Samples) {
            if (Sample.mAnimation != nullptr) {
                Names.push_back(SampleAssetName(Sample.mAnimation->GetPath()));
            }
        }

        if (Names.size() < 2) {
            return 0;
        }

        std::size_t Length{Names[0].size()};

        for (const FString& Name : Names) {
            Length = std::min<std::size_t>(Length, static_cast<std::size_t>(std::mismatch(Names[0].begin(), Names[0].begin() + static_cast<std::ptrdiff_t>(std::min(Length, Name.size())), Name.begin()).first - Names[0].begin()));
        }

        const std::size_t Cut{Length == 0 ? FString::npos : Names[0].rfind('_', Length - 1)};

        return Cut == FString::npos ? 0 : Cut + 1;
    }

    FString SampleName(const FBlendSample& Sample, std::size_t Prefix = 0) {
        if (Sample.mAnimation != nullptr) {
            const FString Name{SampleAssetName(Sample.mAnimation->GetPath())};

            return Prefix < Name.size() ? Name.substr(Prefix) : Name;
        }

        return "None";
    }
}

FBlendSpaceEditor::FBlendSpaceEditor() = default;

FBlendSpaceEditor::~FBlendSpaceEditor() = default;

bool FBlendSpaceEditor::Open(const std::filesystem::path& Path, FString& Error) {
    UBlendSpace* Source{UAssetManager::GetAssetByPath<UBlendSpace>(FAssetUtils::GetAssetKey(FbxPath::ToString(Path)))};

    if (Source == nullptr) {
        Error = "Cannot load animation asset: " + FbxPath::ToString(Path);
        return false;
    }

    // 저장하기 전까지 샘플 이동이 실행 중인 공유 에셋을 바꾸지 않도록 편집 복사본을 사용한다.
    mEditing.reset(NewObject<UBlendSpace1D>());
    mEditing->CopyFrom(*Source);

    mPath = Path;
    mDirty = false;
    mOpen = true;
    mError.clear();
    mSelectedSample = -1;
    mDraggingSample = -1;
    mDraggingPreview = false;
    mRevealSample = false;

    const FBlendParameter& Parameter{mEditing->GetBlendParameter(EBlendSpaceAxis::X)};

    mPreviewInput = std::clamp(mPreviewInput, Parameter.mMin, Parameter.mMax);
    ImGui::SetWindowFocus(WindowId);

    return true;
}

bool FBlendSpaceEditor::IsFocused() const {
    ImGuiWindow* Window{mOpen ? ImGui::FindWindowByName(WindowId) : nullptr};
    ImGuiWindow* Focused{ImGui::GetCurrentContext()->NavWindow};

    return Window != nullptr && Focused != nullptr && ImGui::IsWindowChildOf(Focused, Window, true, false);
}

void FBlendSpaceEditor::Close() {
    mOpen = false;
    mEditing.reset();
}

bool FBlendSpaceEditor::Save() {
    if (mEditing == nullptr) {
        return false;
    }

    if (!UAssetManager::SaveBlendSpace(FbxPath::ToString(mPath), *mEditing, mError)) {
        return false;
    }

    mDirty = false;
    mError.clear();

    return true;
}

float FBlendSpaceEditor::Snap(float Value, bool Free) const {
    // Ctrl로 자유 배치를 요청한 경우를 제외하고 축의 격자 간격에 샘플 좌표를 맞춘다.
    const FBlendParameter& Parameter{mEditing->GetBlendParameter(EBlendSpaceAxis::X)};
    float Result{std::clamp(Value, Parameter.mMin, Parameter.mMax)};

    if (mSnapToGrid && !Free) {
        const float Step{(Parameter.mMax - Parameter.mMin) / static_cast<float>(std::max(Parameter.mGridDivisions, 1))};

        Result = Parameter.mMin + std::round((Result - Parameter.mMin) / Step) * Step;
    }

    return std::clamp(Result, Parameter.mMin, Parameter.mMax);
}

void FBlendSpaceEditor::AddSampleAt(const FString& AnimationPath, float Value) {
    const UAnimSequenceBase* Animation{AnimationPath.empty() ? nullptr : UAssetManager::GetAssetByPath<UAnimSequenceBase>(AnimationPath)};
    if (!AnimationPath.empty() && Animation == nullptr) {
        mError = "Cannot load animation sequence: " + AnimationPath;
        return;
    }

    const int32 Index{mEditing->AddSample(Animation, {Value, 0.0f, 0.0f})};

    if (Index < 0) {
        mError = "Sample limit reached.";

        return;
    }

    mSelectedSample = Index;
    mRevealSample = true;
    mDirty = true;
    mError.clear();
}

void FBlendSpaceEditor::Draw() {
    if (!mOpen || mEditing == nullptr) {
        return;
    }

    const FString Title{std::format("Blend Space - {}{}{}", FbxPath::ToString(mPath.stem()), mDirty ? " *" : "", WindowId)};
    bool Visible{true};

    ImGui::SetNextWindowSize({960.0f, 640.0f}, ImGuiCond_FirstUseEver);

    const bool Expanded{ImGui::Begin(Title.c_str(), &Visible)};

    if (!Visible) {
        ImGui::End();
        Close();

        return;
    }

    if (Expanded) {
        ImGui::Checkbox("Snap to grid", &mSnapToGrid);
        ImGui::SameLine();
        ImGui::Checkbox("Sample names", &mShowSampleNames);

        if (!mError.empty()) {
            ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", mError.c_str());
        }

        DrawCanvas();

        ImGui::Spacing();

        if (ImGui::BeginTable("BlendSpaceDetails", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
            ImGui::TableSetupColumn("Axis", ImGuiTableColumnFlags_WidthFixed, 240.0f);
            ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            if (ImGui::BeginChild("BlendAxis")) {
                DrawAxis();
            }

            ImGui::EndChild();
            ImGui::TableSetColumnIndex(1);

            if (ImGui::BeginChild("BlendSamples")) {
                DrawSamples();
            }

            ImGui::EndChild();
            ImGui::EndTable();
        }
    }

    if (mDirty && IsFocused() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        Save();
    }

    ImGui::End();
}

void FBlendSpaceEditor::DrawAxis() {
    ImGui::SeparatorText("Axis");

    FBlendParameter Parameter{mEditing->GetBlendParameter(EBlendSpaceAxis::X)};
    std::array<char, 64> Name{};

    std::copy_n(Parameter.mDisplayName.begin(), std::min(Parameter.mDisplayName.size(), Name.size() - 1), Name.begin());

    bool Changed{ImGui::InputText("Name", Name.data(), Name.size())};

    Changed |= ImGui::DragFloat("Min", &Parameter.mMin, 0.05f, 0.0f, 0.0f, "%.2f");
    Changed |= ImGui::DragFloat("Max", &Parameter.mMax, 0.05f, 0.0f, 0.0f, "%.2f");
    Changed |= ImGui::SliderInt("Grid divisions", &Parameter.mGridDivisions, 1, 32);

    if (Changed) {
        Parameter.mDisplayName = Name.data();

        if (mEditing->SetBlendParameter(EBlendSpaceAxis::X, Parameter)) {
            mDirty = true;
            mError.clear();
            mPreviewInput = std::clamp(mPreviewInput, Parameter.mMin, Parameter.mMax);
        } else {
            mError = "Axis minimum must be below its maximum.";
        }
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Grid spacing %.3f", (Parameter.mMax - Parameter.mMin) / static_cast<float>(std::max(Parameter.mGridDivisions, 1)));
}

void FBlendSpaceEditor::DrawSamples() {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Samples  %zu", mEditing->GetSamples().size());
    ImGui::SameLine();
    ImGui::BeginDisabled(mEditing->GetSamples().size() >= UBlendSpace::MaxSamples);

    if (ImGui::Button("+ Empty sample")) {
        AddSampleAt({}, Snap(mPreviewInput, false));
    }

    ImGui::EndDisabled();

    const FBlendParameter& Parameter{mEditing->GetBlendParameter(EBlendSpaceAxis::X)};
    const auto& Samples{mEditing->GetSamples()};
    const std::size_t CommonPrefix{CommonNamePrefix(Samples)};
    const float RowHeight{ImGui::GetFrameHeight() + 2.0f * ImGui::GetStyle().CellPadding.y};
    int32 Remove{-1};

    if (ImGui::BeginTable("SampleList", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Animation", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn(Parameter.mDisplayName.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (std::size_t Index{}; Index < Samples.size(); ++Index) {
            const FBlendSample& Sample{Samples[Index]};
            const bool Selected{mSelectedSample == static_cast<int32>(Index)};

            ImGui::PushID(static_cast<int>(Index));
            ImGui::TableNextRow(ImGuiTableRowFlags_None, RowHeight);

            if (Selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header));
            }

            if (ImGui::TableSetColumnIndex(0)) {
                FString AnimationPath{Sample.mAnimation != nullptr ? Sample.mAnimation->GetPath() : FString{}};
                const FString Name{SampleName(Sample, CommonPrefix)};

                if (DrawAnimationAssetPicker("##Animation", AnimationPath, EAssetType::AnimSequence, Name.c_str())) {
                    const auto* Animation{AnimationPath.empty() ? nullptr : UAssetManager::GetAssetByPath<UAnimSequenceBase>(AnimationPath)};
                    mEditing->SetSampleAnimation(static_cast<int32>(Index), Animation);
                    mSelectedSample = static_cast<int32>(Index);
                    mDirty = true;
                }
                if (ImGui::IsItemClicked()) {
                    mSelectedSample = static_cast<int32>(Index);
                }
            }

            if (ImGui::TableSetColumnIndex(1)) {
                float Value{Sample.mSampleValue.X};

                ImGui::SetNextItemWidth(-1.0f);

                if (ImGui::DragFloat("##Value", &Value, 0.01f, Parameter.mMin, Parameter.mMax, "%.4g")) {
                    mEditing->SetSampleValue(static_cast<int32>(Index), {std::clamp(Value, Parameter.mMin, Parameter.mMax), 0.0f, 0.0f});
                    mSelectedSample = static_cast<int32>(Index);
                    mDirty = true;
                }
            }

            if (ImGui::TableSetColumnIndex(2) && ImGui::Button("×")) {
                Remove = static_cast<int32>(Index);
            }

            if (Selected && mRevealSample) {
                ImGui::SetScrollFromPosY(ImGui::GetCursorPosY() + RowHeight * 0.5f, 0.5f);
                mRevealSample = false;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (Remove >= 0) {
        mEditing->RemoveSample(Remove);
        mSelectedSample = -1;
        mDirty = true;
    }
}

void FBlendSpaceEditor::DrawCanvas() {
    const FBlendParameter& Parameter{mEditing->GetBlendParameter(EBlendSpaceAxis::X)};
    const auto& Samples{mEditing->GetSamples()};
    const ImGuiIO& IO{ImGui::GetIO()};

    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const float TextHeight{ImGui::GetTextLineHeight()};
    const ImVec2 Size{std::max(ImGui::GetContentRegionAvail().x, 1.0f), 7.5f * TextHeight};
    const ImVec2 End{Origin.x + Size.x, Origin.y + Size.y};
    const FString MinText{std::format("{:.5g}", Parameter.mMin)};
    const FString MaxText{std::format("{:.5g}", Parameter.mMax)};
    const float TickWidth{std::max(ImGui::CalcTextSize(MinText.c_str()).x, ImGui::CalcTextSize(MaxText.c_str()).x)};
    const float Margin{std::min(Size.x * 0.25f, std::max(TextHeight * 1.6f, 0.5f * TickWidth + 8.0f))};
    const float TrackWidth{Size.x - 2.0f * Margin};
    const float AxisY{End.y - 2.5f * TextHeight};
    const double Range{static_cast<double>(Parameter.mMax) - Parameter.mMin};
    // 축 값과 화면 좌표의 변환을 공유해 그리기, 드롭과 드래그 위치가 일치하게 한다.
    const auto ToScreen{[&](float Value) {
        const double Alpha{std::clamp((static_cast<double>(Value) - Parameter.mMin) / Range, 0.0, 1.0)};

        return Origin.x + Margin + TrackWidth * static_cast<float>(Alpha);
    }};
    const auto ToValue{[&](float ScreenX) {
        const float Alpha{std::clamp((ScreenX - Origin.x - Margin) / TrackWidth, 0.0f, 1.0f)};

        return static_cast<float>(Parameter.mMin + Alpha * Range);
    }};

    ImGui::InvisibleButton("BlendSpaceCanvas", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const bool CanvasHovered{ImGui::IsItemHovered()};
    const ImVec2 Mouse{IO.MousePos};

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* Payload{ImGui::AcceptDragDropPayload(AnimSequencePayload)}; Payload != nullptr && Payload->Data != nullptr && Payload->DataSize > 1 && static_cast<const char*>(Payload->Data)[Payload->DataSize - 1] == '\0') {
            const FString Candidate{static_cast<const char*>(Payload->Data), static_cast<std::size_t>(Payload->DataSize - 1)};
            AddSampleAt(FAssetUtils::GetAssetKey(Candidate), Snap(ToValue(Mouse.x), IO.KeyCtrl));
        }

        ImGui::EndDragDropTarget();
    }

    int32 Hovered{-1};
    float HoveredDistance{TextHeight * 0.75f};

    if (CanvasHovered && std::abs(Mouse.y - AxisY) < TextHeight) {
        for (std::size_t Index{}; Index < Samples.size(); ++Index) {
            const float Distance{std::abs(Mouse.x - ToScreen(Samples[Index].mSampleValue.X))};

            if (Distance < HoveredDistance || (Distance == HoveredDistance && static_cast<int32>(Index) == mSelectedSample)) {
                HoveredDistance = Distance;
                Hovered = static_cast<int32>(Index);
            }
        }
    }

    if (CanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (Hovered >= 0) {
            mSelectedSample = Hovered;
            mDraggingSample = Hovered;
            mRevealSample = true;
        } else {
            mDraggingPreview = true;
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        mDraggingSample = -1;
        mDraggingPreview = false;
    }

    if (mDraggingSample >= 0 && static_cast<std::size_t>(mDraggingSample) < Samples.size()) {
        const float Value{Snap(ToValue(Mouse.x), IO.KeyCtrl)};

        if (Value != Samples[static_cast<std::size_t>(mDraggingSample)].mSampleValue.X) {
            mEditing->SetSampleValue(mDraggingSample, {Value, 0.0f, 0.0f});
            mDirty = true;
        }
    }

    if (mDraggingPreview || (CanvasHovered && IO.KeyCtrl && mDraggingSample < 0)) {
        mPreviewInput = ToValue(Mouse.x);
    }

    if (CanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (Hovered >= 0) {
            mSelectedSample = Hovered;
            mRevealSample = true;
            ImGui::OpenPopup("BlendSampleMenu");
        } else {
            mAddValue = Snap(ToValue(Mouse.x), IO.KeyCtrl);
            mAddSearch.fill('\0');
            ImGui::OpenPopup("BlendAddSample");
        }
    }

    ImDrawList* DrawList{ImGui::GetWindowDrawList()};
    const ImU32 GridColor{IM_COL32(49, 62, 79, 255)};
    const ImU32 TextColor{ImGui::GetColorU32(ImGuiCol_Text)};
    const ImU32 TickColor{IM_COL32(163, 179, 198, 255)};
    const ImU32 SampleColor{IM_COL32(185, 199, 219, 255)};
    const ImU32 ActiveColor{IM_COL32(255, 193, 100, 255)};
    const ImU32 PreviewColor{IM_COL32(105, 235, 169, 255)};
    const ImU32 SelectionColor{IM_COL32(111, 195, 255, 255)};

    // 런타임과 같은 샘플 선택 함수를 사용해 미리보기 가중치가 실제 재생과 일치하게 한다.
    std::vector<FBlendSampleWeight> Weights{};
    mEditing->GetSamplesToBlend({mPreviewInput, 0.0f, 0.0f}, Weights);

    DrawList->PushClipRect(Origin, End, true);
    DrawList->AddRectFilled(Origin, End, IM_COL32(19, 25, 34, 255), 6.0f);
    DrawList->AddRect(Origin, End, CanvasHovered ? IM_COL32(95, 113, 139, 255) : GridColor, 6.0f);

    const int32 Divisions{std::max(Parameter.mGridDivisions, 1)};
    const float TickGap{TickWidth + TextHeight};
    const float TrackEnd{ToScreen(Parameter.mMax)};
    float LastTickX{ToScreen(Parameter.mMin) - TickGap};

    for (int32 Line{}; Line <= Divisions; ++Line) {
        const float Value{static_cast<float>(Parameter.mMin + Range * Line / Divisions)};
        const float X{ToScreen(Value)};
        const bool Endpoint{Line == 0 || Line == Divisions};

        DrawList->AddLine({X, Origin.y + TextHeight * 2.2f}, {X, AxisY + 5.0f}, GridColor, Endpoint ? 1.5f : 1.0f);

        if (Endpoint || (X - LastTickX >= TickGap && TrackEnd - X >= TickGap)) {
            const FString Label{std::format("{:.5g}", Value)};
            const float HalfWidth{0.5f * ImGui::CalcTextSize(Label.c_str()).x};

            DrawList->AddText({X - HalfWidth, AxisY + 0.7f * TextHeight}, TickColor, Label.c_str());
            LastTickX = X;
        }
    }

    DrawList->AddLine({ToScreen(Parameter.mMin), AxisY}, {TrackEnd, AxisY}, TickColor, 2.0f);

    if (Weights.size() == 2) {
        const float Left{ToScreen(Samples[static_cast<std::size_t>(Weights.front().mSampleIndex)].mSampleValue.X)};
        const float Right{ToScreen(Samples[static_cast<std::size_t>(Weights.back().mSampleIndex)].mSampleValue.X)};

        DrawList->AddLine({Left, AxisY}, {Right, AxisY}, ActiveColor, 3.0f);
    }

    const float PreviewX{ToScreen(mPreviewInput)};
    const FString PreviewLabel{std::format("{:.5g}", mPreviewInput)};
    const ImVec2 PreviewSize{ImGui::CalcTextSize(PreviewLabel.c_str())};
    const float PreviewLabelX{std::clamp(PreviewX - PreviewSize.x * 0.5f, Origin.x + 8.0f, std::max(Origin.x + 8.0f, End.x - PreviewSize.x - 8.0f))};
    const float LabelY{Origin.y + 0.5f * TextHeight};

    DrawList->AddLine({PreviewX, LabelY + TextHeight + 4.0f}, {PreviewX, AxisY + 6.0f}, PreviewColor, 2.0f);
    DrawList->AddRectFilled({PreviewLabelX - 6.0f, LabelY - 3.0f}, {PreviewLabelX + PreviewSize.x + 6.0f, LabelY + TextHeight + 3.0f}, IM_COL32(26, 65, 58, 255), 5.0f);
    DrawList->AddText({PreviewLabelX, LabelY}, PreviewColor, PreviewLabel.c_str());
    DrawList->AddText({Origin.x + Margin, End.y - 1.15f * TextHeight}, TextColor, Parameter.mDisplayName.c_str());

    std::vector<int32> Order(Samples.size());
    std::array<float, UBlendSpace::MaxSamples> SampleWeights{};

    for (const FBlendSampleWeight& Weight : Weights) {
        SampleWeights[static_cast<std::size_t>(Weight.mSampleIndex)] = Weight.mWeight;
    }

    for (std::size_t Index{}; Index < Samples.size(); ++Index) {
        Order[Index] = static_cast<int32>(Index);
    }

    const auto Priority{[&](int32 Index) {
        return Index == Hovered || Index == mSelectedSample ? 2 : SampleWeights[static_cast<std::size_t>(Index)] > 0.0f ? 1 : 0;
    }};
    std::stable_sort(Order.begin(), Order.end(), [&Priority](int32 Left, int32 Right) {
        return Priority(Left) > Priority(Right);
    });

    const std::size_t CommonPrefix{CommonNamePrefix(Samples)};
    std::array<std::vector<ImVec2>, 2> LabelBounds{};

    for (const int32 Index : Order) {
        const auto SampleIndex{static_cast<std::size_t>(Index)};
        const FBlendSample& Sample{Samples[SampleIndex]};
        const float X{ToScreen(Sample.mSampleValue.X)};
        const float Weight{SampleWeights[SampleIndex]};
        const bool Highlight{Priority(Index) == 2};
        const float Radius{TextHeight * (Highlight ? 0.49f : 0.36f)};
        const ImU32 Fill{Weight > 0.0f ? ActiveColor : Sample.mAnimation == nullptr ? IM_COL32(200, 90, 80, 255) : SampleColor};

        DrawList->AddQuadFilled({X, AxisY - Radius}, {X + Radius, AxisY}, {X, AxisY + Radius}, {X - Radius, AxisY}, Fill);

        if (Highlight) {
            DrawList->AddQuad({X, AxisY - Radius - 2.0f}, {X + Radius + 2.0f, AxisY}, {X, AxisY + Radius + 2.0f}, {X - Radius - 2.0f, AxisY}, SelectionColor, 2.0f);
        }

        if (mShowSampleNames) {
            const FString Name{SampleName(Sample, CommonPrefix)};
            const float NameWidth{ImGui::CalcTextSize(Name.c_str()).x};
            const float Left{std::clamp(X - NameWidth * 0.5f, Origin.x + 6.0f, std::max(Origin.x + 6.0f, End.x - NameWidth - 6.0f))};
            const ImVec2 Bounds{Left - 5.0f, Left + NameWidth + 5.0f};

            for (std::size_t Lane{}; Lane < LabelBounds.size(); ++Lane) {
                const auto& Placed{LabelBounds[Lane]};

                if (Bounds.y > End.x || std::any_of(Placed.begin(), Placed.end(), [Bounds](const ImVec2& Other) {
                    return Bounds.x < Other.y && Other.x < Bounds.y;
                })) {
                    continue;
                }

                const float Y{AxisY - TextHeight * (1.55f + static_cast<float>(Lane) * 1.25f)};

                DrawList->AddRectFilled({Bounds.x, Y - 1.0f}, {Bounds.y, Y + TextHeight + 1.0f}, IM_COL32(26, 34, 47, 255), 3.0f);
                DrawList->AddText({Left, Y}, Highlight ? SelectionColor : Weight > 0.0f ? ActiveColor : TextColor, Name.c_str());
                LabelBounds[Lane].push_back(Bounds);

                break;
            }
        }

        if (Index == Hovered && mDraggingSample < 0) {
            ImGui::SetTooltip("%s\n%s = %.4g   ·   %.1f%%", SampleName(Sample).c_str(), Parameter.mDisplayName.c_str(), Sample.mSampleValue.X, Weight * 100.0f);
        }
    }

    DrawList->PopClipRect();

    if (mDraggingSample >= 0 && static_cast<std::size_t>(mDraggingSample) < Samples.size()) {
        ImGui::SetTooltip("%s = %.4g", Parameter.mDisplayName.c_str(), Samples[static_cast<std::size_t>(mDraggingSample)].mSampleValue.X);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Preview Weights");
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f s", mEditing->GetBlendedPlayLength(Weights));

    if (ImGui::BeginTable("PreviewWeights", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("Percent", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("100.0%").x);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ActiveColor);

        for (std::size_t Index{}; Index < 2; ++Index) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, TextHeight + 2.0f * ImGui::GetStyle().CellPadding.y);
            ImGui::TableSetColumnIndex(0);

            if (Index < Weights.size()) {
                const FBlendSampleWeight& Weight{Weights[Index]};
                const FBlendSample& Sample{Samples[static_cast<std::size_t>(Weight.mSampleIndex)]};

                ImGui::TextUnformatted(SampleName(Sample, CommonPrefix).c_str());
                ImGui::SetItemTooltip("%s", SampleName(Sample).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::ProgressBar(Weight.mWeight, {-1.0f, TextHeight}, "");
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.1f%%", Weight.mWeight * 100.0f);
            } else if (Index == 0) {
                ImGui::TextDisabled("—");
            }
        }

        ImGui::PopStyleColor();
        ImGui::EndTable();
    }

    DrawSampleMenus();
}

void FBlendSpaceEditor::DrawSampleMenus() {
    if (ImGui::BeginPopup("BlendSampleMenu")) {
        const bool Valid{mSelectedSample >= 0 && static_cast<std::size_t>(mSelectedSample) < mEditing->GetSamples().size()};

        if (ImGui::MenuItem("Preview this sample", nullptr, false, Valid)) {
            mPreviewInput = mEditing->GetSamples()[static_cast<std::size_t>(mSelectedSample)].mSampleValue.X;
        }

        if (ImGui::MenuItem("Delete sample", nullptr, false, Valid)) {
            mEditing->RemoveSample(mSelectedSample);
            mSelectedSample = -1;
            mDirty = true;
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("BlendAddSample")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputTextWithHint("##Search", "Search sequences", mAddSearch.data(), mAddSearch.size());
        ImGui::Separator();

        const ImGuiTextFilter Filter{mAddSearch.data()};

        if (ImGui::BeginChild("##Sequences", {280.0f, 240.0f})) {
            for (const FString& Path : UAssetManager::GetAnimationAssetPaths(EAssetType::AnimSequence)) {
                if (!Filter.PassFilter(Path.c_str())) {
                    continue;
                }
                ImGui::PushID(Path.c_str());
                if (ImGui::Selectable(FbxPath::ToString(FbxPath::FromString(Path).stem()).c_str())) {
                    AddSampleAt(Path, mAddValue);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", Path.c_str());
                }
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

}
