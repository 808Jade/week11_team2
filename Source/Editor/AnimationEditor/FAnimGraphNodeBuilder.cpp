#include "EnginePCH.h"
#include "FAnimGraphNodeBuilder.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace ed = ax::NodeEditor;

namespace {
    constexpr float IconSize{16.0f};
    constexpr float IconSpacing{6.0f};
    constexpr float HeaderInset{12.0f};
    constexpr float PropertySpacing{14.0f};
    constexpr ImVec4 TitleColor{0.94f, 0.96f, 1.0f, 1.0f};
    constexpr ImVec4 SecondaryColor{0.62f, 0.68f, 0.77f, 1.0f};
    constexpr ImVec4 ValueColor{0.86f, 0.90f, 0.96f, 1.0f};
    constexpr ImVec4 BindingColor{0.50f, 0.81f, 1.0f, 1.0f};

    ImVec4 TypeColor(std::string_view Type) {
        if (Type == "Sequence Player") {
            return {0.40f, 0.80f, 0.59f, 1.0f};
        }

        if (Type == "Blend Space Player") {
            return {0.32f, 0.79f, 0.87f, 1.0f};
        }

        if (Type == "Output Pose") {
            return {0.98f, 0.66f, 0.43f, 1.0f};
        }

        if (Type == "Float") {
            return FAnimGraphNodeBuilder::FloatColor();
        }

        return {0.67f, 0.61f, 0.96f, 1.0f};
    }

    float PinWidth(const char* Label) {
        return ImGui::CalcTextSize(Label).x + IconSpacing + IconSize;
    }
}

void FAnimGraphNodeBuilder::FitHeader(const char* Title, const char* Type) {
    FitText(Title, HeaderInset);
    FitText(Type, HeaderInset);
}

void FAnimGraphNodeBuilder::FitText(const char* Text, float Padding) {
    mWidth = std::max(mWidth, ImGui::CalcTextSize(Text).x + Padding);
}

void FAnimGraphNodeBuilder::FitPins(const char* Input, const char* Output) {
    const float InputWidth{Input != nullptr ? PinWidth(Input) : 0.0f};
    const float OutputWidth{Output != nullptr ? PinWidth(Output) : 0.0f};
    const float Spacing{Input != nullptr && Output != nullptr ? ImGui::GetStyle().ItemSpacing.x : 0.0f};

    mWidth = std::max(mWidth, InputWidth + Spacing + OutputWidth);
}

void FAnimGraphNodeBuilder::FitProperty(const char* Label, const char* Value) {
    // 속성별 최대 라벨 폭과 값 폭을 따로 모아 모든 행의 값 열을 맞춘다.
    mLabelWidth = std::max(mLabelWidth, ImGui::CalcTextSize(Label).x);
    mValueWidth = std::max(mValueWidth, ImGui::CalcTextSize(Value).x);
    mWidth = std::max(mWidth, mLabelWidth + PropertySpacing + mValueWidth);
}

void FAnimGraphNodeBuilder::Begin(ed::NodeId Id, float MinWidth) {
    mId = Id;
    mWidth = std::ceil(std::max(mWidth, MinWidth));
    ed::BeginNode(Id);
    ImGui::PushID(Id.AsPointer());
    mStartX = ImGui::GetCursorPosX();
}

void FAnimGraphNodeBuilder::Header(const char* Title, const char* Type) {
    const std::string_view TypeName{Type};
    mHeaderColor = TypeColor(TypeName);

    ImGui::BeginGroup();
    ImGui::Indent(HeaderInset);
    DrawText(Title, TitleColor);

    if (!TypeName.empty() && TypeName != Title) {
        DrawText(Type, SecondaryColor);
    }

    ImGui::Unindent(HeaderInset);
    ImGui::EndGroup();

    mHeaderBottom = ImGui::GetItemRectMax().y + 7.0f;
    ImGui::Dummy({mWidth, 9.0f});
}

void FAnimGraphNodeBuilder::PinIcon(bool Connected, const ImVec4& Color) {
    const ImVec2 Cursor{ImGui::GetCursorScreenPos()};
    const float Offset{std::max((ImGui::GetTextLineHeight() - IconSize) * 0.5f, 0.0f)};
    const ImVec2 Min{Cursor.x, Cursor.y + Offset};
    const ImVec2 Max{Min.x + IconSize, Min.y + IconSize};

    ImGui::Dummy({IconSize, std::max(IconSize, ImGui::GetTextLineHeight())});

    const ImVec2 Anchor{(Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f};

    ed::PinRect(Min, Max);
    ed::PinPivotRect(Anchor, Anchor);

    ImDrawList* DrawList{ImGui::GetWindowDrawList()};
    const ImU32 IconColor{ImGui::GetColorU32(Connected ? Color : SecondaryColor)};

    DrawList->AddCircle(Anchor, 5.0f, IconColor, 16, 1.5f);

    if (Connected) {
        DrawList->AddCircleFilled(Anchor, 2.6f, IconColor, 12);
    }
}

void FAnimGraphNodeBuilder::InputPin(ed::PinId Id, const char* Label, bool Connected, const ImVec4& Color) {
    ImGui::SetCursorPosX(mStartX);
    ed::BeginPin(Id, ed::PinKind::Input);
    PinIcon(Connected, Color);
    ImGui::SameLine(0.0f, IconSpacing);
    ImGui::TextColored(Connected ? ValueColor : SecondaryColor, "%s", Label);
    ed::EndPin();
}

void FAnimGraphNodeBuilder::OutputPin(ed::PinId Id, const char* Label, bool Connected, const ImVec4& Color, bool SameLine) {
    const float Width{PinWidth(Label)};
    float X{mStartX + mWidth - Width};

    if (SameLine) {
        ImGui::SameLine();
        X = std::max(X, ImGui::GetCursorPosX());
    }

    ImGui::SetCursorPosX(X);
    ed::BeginPin(Id, ed::PinKind::Output);
    ImGui::TextColored(Connected ? ValueColor : SecondaryColor, "%s", Label);
    ImGui::SameLine(0.0f, IconSpacing);
    PinIcon(Connected, Color);
    ed::EndPin();
}

void FAnimGraphNodeBuilder::BeginBody() {
    const ImVec2 Position{ImGui::GetCursorScreenPos()};

    ImGui::GetWindowDrawList()->AddLine({Position.x, Position.y + 2.0f}, {Position.x + mWidth, Position.y + 2.0f}, ImGui::GetColorU32(ImVec4{0.40f, 0.48f, 0.61f, 0.28f}));
    ImGui::Dummy({mWidth, 6.0f});
}

void FAnimGraphNodeBuilder::DrawText(const char* Text, const ImVec4& Color) const {
    const ImVec2 Position{ImGui::GetCursorScreenPos()};
    const ImVec2 Size{ImGui::CalcTextSize(Text)};

    ImGui::Dummy(Size);
    ImGui::GetWindowDrawList()->AddText(Position, ImGui::GetColorU32(Color), Text);
}

void FAnimGraphNodeBuilder::DrawProperty(const char* Label, const char* Value, bool Bound) const {
    const ImVec2 Position{ImGui::GetCursorScreenPos()};
    const float ValueOffset{mLabelWidth + PropertySpacing};
    const float Height{std::max(ImGui::CalcTextSize(Label).y, ImGui::CalcTextSize(Value).y)};
    ImDrawList* DrawList{ImGui::GetWindowDrawList()};

    ImGui::Dummy({mWidth, Height});
    DrawList->AddText(Position, ImGui::GetColorU32(SecondaryColor), Label);
    DrawList->AddText({Position.x + ValueOffset, Position.y}, ImGui::GetColorU32(Bound ? BindingColor : ValueColor), Value);
}

void FAnimGraphNodeBuilder::End() {
    ImGui::PopID();
    ed::EndNode();

    // EndNode 이후 확정된 크기로 헤더 배경을 그려 긴 내용에도 배경 폭이 맞게 한다.
    ImDrawList* DrawList{ed::GetNodeBackgroundDrawList(mId)};
    const ImVec2 Position{ed::GetNodePosition(mId)};
    const ImVec2 Size{ed::GetNodeSize(mId)};

    if (DrawList == nullptr || Size.x <= 0.0f) {
        return;
    }

    const ed::Style& Style{ed::GetStyle()};
    const float Bottom{std::min(mHeaderBottom, Position.y + Size.y)};
    const ImVec4 Fill{mHeaderColor.x, mHeaderColor.y, mHeaderColor.z, 0.13f};
    const ImVec4 Divider{mHeaderColor.x, mHeaderColor.y, mHeaderColor.z, 0.30f};
    const float AccentX{Position.x + Style.NodePadding.x};

    DrawList->AddRectFilled({Position.x + 1.0f, Position.y + 1.0f}, {Position.x + Size.x - 1.0f, Bottom}, ImGui::GetColorU32(Fill), Style.NodeRounding - 1.0f, ImDrawFlags_RoundCornersTop);
    DrawList->AddLine({Position.x + 1.0f, Bottom}, {Position.x + Size.x - 1.0f, Bottom}, ImGui::GetColorU32(Divider));
    DrawList->AddRectFilled({AccentX, Position.y + Style.NodePadding.y + 2.0f}, {AccentX + 3.0f, Bottom - 7.0f}, ImGui::GetColorU32(mHeaderColor), 1.5f);
}

float FAnimGraphNodeBuilder::GetWidth() const {
    return mWidth;
}

ImVec4 FAnimGraphNodeBuilder::PoseColor() {
    return {0.76f, 0.85f, 0.98f, 1.0f};
}

ImVec4 FAnimGraphNodeBuilder::FloatColor() {
    return {0.47f, 0.88f, 0.60f, 1.0f};
}

void FAnimGraphNodeBuilder::DrawFloatGetter(ed::NodeId Node, ed::PinId Pin, const char* Name, const char* Value, bool Connected) {
    const std::string Title{std::string{"Get "} + Name};
    FAnimGraphNodeBuilder Builder{};

    Builder.FitHeader(Title.c_str(), "Float");
    Builder.FitPins(nullptr, Value);
    Builder.Begin(Node, 125.0f);
    Builder.Header(Title.c_str(), "Float");
    Builder.OutputPin(Pin, Value, Connected, FloatColor(), false);
    Builder.End();
}

void FAnimGraphNodeBuilder::ApplyStyle() {
    ed::Style& Style{ed::GetStyle()};

    Style.NodePadding = {11.0f, 9.0f, 11.0f, 9.0f};
    Style.NodeRounding = 9.0f;
    Style.NodeBorderWidth = 1.0f;
    Style.HoveredNodeBorderWidth = 1.5f;
    Style.SelectedNodeBorderWidth = 2.0f;
    Style.PinRounding = 6.0f;
    Style.LinkStrength = 96.0f;
    Style.SnapLinkToPinDir = 1.0f;
    Style.HighlightConnectedLinks = 1.0f;
    Style.Colors[ed::StyleColor_Bg] = {0.075f, 0.088f, 0.112f, 1.0f};
    Style.Colors[ed::StyleColor_Grid] = {0.31f, 0.37f, 0.46f, 0.18f};
    Style.Colors[ed::StyleColor_NodeBg] = {0.115f, 0.135f, 0.171f, 1.0f};
    Style.Colors[ed::StyleColor_NodeBorder] = {0.28f, 0.34f, 0.43f, 1.0f};
    Style.Colors[ed::StyleColor_HovNodeBorder] = {0.52f, 0.66f, 0.85f, 1.0f};
    Style.Colors[ed::StyleColor_SelNodeBorder] = {0.51f, 0.76f, 1.0f, 1.0f};
    Style.Colors[ed::StyleColor_HovLinkBorder] = ValueColor;
    Style.Colors[ed::StyleColor_SelLinkBorder] = BindingColor;
    Style.Colors[ed::StyleColor_HighlightLinkBorder] = {0.64f, 0.77f, 0.95f, 0.70f};
    Style.Colors[ed::StyleColor_PinRect] = {0.38f, 0.63f, 0.95f, 0.22f};
    Style.Colors[ed::StyleColor_PinRectBorder] = {0.51f, 0.76f, 1.0f, 0.52f};
}
