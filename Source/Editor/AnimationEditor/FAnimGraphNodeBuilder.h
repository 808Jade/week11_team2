#pragma once

#include <imgui.h>
#include <imgui_node_editor.h>

class FAnimGraphNodeBuilder {
public:
    void FitHeader(const char* Title, const char* Type);
    void FitText(const char* Text, float Padding = 0.0f);
    void FitPins(const char* Input, const char* Output = nullptr);
    void FitProperty(const char* Label, const char* Value);

    void Begin(ax::NodeEditor::NodeId Id, float MinWidth);

    void Header(const char* Title, const char* Type);

    void InputPin(ax::NodeEditor::PinId Id, const char* Label, bool Connected, const ImVec4& Color);

    void OutputPin(ax::NodeEditor::PinId Id, const char* Label, bool Connected, const ImVec4& Color, bool SameLine);

    void BeginBody();

    void DrawText(const char* Text, const ImVec4& Color) const;
    void DrawProperty(const char* Label, const char* Value, bool Bound = false) const;

    void End();

    float GetWidth() const;

    static ImVec4 PoseColor();
    static ImVec4 FloatColor();
    static void DrawFloatGetter(ax::NodeEditor::NodeId Node, ax::NodeEditor::PinId Pin, const char* Name, const char* Value, bool Connected);

    static void ApplyStyle();

private:
    void PinIcon(bool Connected, const ImVec4& Color);

private:
    ax::NodeEditor::NodeId mId{};

    float mWidth{};
    float mLabelWidth{};
    float mValueWidth{};

    float mStartX{};

    float mHeaderBottom{};

    ImVec4 mHeaderColor{};
};
