#pragma once

#include <imgui.h>
#include <imgui_node_editor.h>

// 편집기와 디버그 패널의 노드 모양을 공유한다. Fit 계열로 폭을 측정한 뒤 실제 내용을 그린다.
class FAnimGraphNodeBuilder {
public:
    // 헤더 제목과 타입 이름이 들어갈 최소 폭을 측정한다.
    void FitHeader(const char* Title, const char* Type);
    // 문자열과 여백이 들어갈 폭으로 현재 노드의 필요 폭을 확장한다.
    void FitText(const char* Text, float Padding = 0.0f);
    // 같은 행의 입력 및 출력 핀 이름과 아이콘이 차지할 폭을 측정한다.
    void FitPins(const char* Input, const char* Output = nullptr);
    // 모든 속성 행에서 라벨과 값 열이 정렬되도록 최대 폭을 누적한다.
    void FitProperty(const char* Label, const char* Value);

    // 측정 폭과 최소 폭을 적용하고 지정 ID의 노드 그리기를 시작한다.
    void Begin(ax::NodeEditor::NodeId Id, float MinWidth);

    // 노드 타입 색상에 맞춰 제목과 보조 타입 이름을 그린다.
    void Header(const char* Title, const char* Type);

    // 입력 핀 아이콘과 라벨을 그리고 연결 여부를 표시한다.
    void InputPin(ax::NodeEditor::PinId Id, const char* Label, bool Connected, const ImVec4& Color);

    // 출력 핀을 오른쪽에 배치하며 필요하면 입력과 같은 행에 그린다.
    void OutputPin(ax::NodeEditor::PinId Id, const char* Label, bool Connected, const ImVec4& Color, bool SameLine);

    // 헤더와 핀 영역 다음에 본문 표시를 시작할 여백을 준비한다.
    void BeginBody();

    // 노드 본문에 지정 색상의 텍스트를 그린다.
    void DrawText(const char* Text, const ImVec4& Color) const;
    // 라벨과 값 열을 맞춰 속성을 그리고 바인딩된 값은 구분 색상을 사용한다.
    void DrawProperty(const char* Label, const char* Value, bool Bound = false) const;

    // 노드 크기를 확정하고 타입 색상의 헤더 배경과 구분선을 그린다.
    void End();

    // 측정 결과와 최소 폭을 반영한 현재 노드 폭을 반환한다.
    float GetWidth() const;

    // 포즈 입력과 출력에 공통으로 사용할 핀 색상을 반환한다.
    static ImVec4 PoseColor();
    // Float 값 입력과 출력에 공통으로 사용할 핀 색상을 반환한다.
    static ImVec4 FloatColor();
    // 변수 이름과 현재 값을 표시하는 공통 Float Get 노드를 그린다.
    static void DrawFloatGetter(ax::NodeEditor::NodeId Node, ax::NodeEditor::PinId Pin, const char* Name, const char* Value, bool Connected);

    // 편집기와 디버그 캔버스에서 공유하는 Node Editor 스타일을 적용한다.
    static void ApplyStyle();

private:
    // 핀 종류의 색상과 연결 여부에 맞는 아이콘을 그린다.
    void PinIcon(bool Connected, const ImVec4& Color);

private:
    // 현재 그리고 있는 Node Editor 노드의 고유 UI 키이다.
    ax::NodeEditor::NodeId mId{};

    // 헤더, 핀과 본문 측정으로 결정한 전체 노드 폭이다.
    float mWidth{};
    // 속성 행의 라벨 열을 정렬하기 위해 측정한 최대 라벨 폭이다.
    float mLabelWidth{};
    // 속성 행에서 값 문자열에 필요한 최대 폭이다.
    float mValueWidth{};

    // 노드 내용 배치를 시작한 로컬 가로 위치이다.
    float mStartX{};

    // 배경색을 칠할 헤더 영역의 아래쪽 화면 좌표이다.
    float mHeaderBottom{};

    // 현재 노드 타입에 따라 선택한 헤더 강조 색상이다.
    ImVec4 mHeaderColor{};
};
