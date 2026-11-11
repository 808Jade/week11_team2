#pragma once

#include "Core/Types.h"

#include <array>
#include <filesystem>
#include <memory>
#include <vector>
#include <unordered_map>

namespace ax::NodeEditor {
    struct EditorContext;
}

class UAnimGraph;


// 원본 에셋의 편집 복사본에서 포즈, 상태 머신, 전환 규칙을 탐색하고 루트 전체를 저장한다.
class FAnimGraphEditor {
private:
    enum class EGraphView {
        Pose,
        Machine,
        Rule
    };

    // 중첩 그래프의 경로와 선택을 저장해 상위 뷰로 돌아왔을 때 작업 위치를 복원한다.
    struct FGraphView {
        // 현재 뷰가 편집하는 루트 또는 상태 내부 그래프의 비소유 포인터이다.
        UAnimGraph* mGraph{};
        // 포즈, 상태 도표와 전환 조건 중 현재 표시할 캔버스 종류이다.
        EGraphView mMode{};
        // 상태 도표 또는 전환 조건이 속한 상태 머신 포즈 노드 ID이다.
        int32 mMachine{-1};
        // 전환 조건 뷰 또는 속성 패널에서 선택한 전환 ID이다.
        int32 mTransition{-1};
        // 이 뷰를 떠날 때 저장한 노드 선택이며 다시 돌아오면 복원한다.
        int32 mSelection{-2};
        // 경로 탐색 UI에서 표시할 뷰의 이름이다.
        FString mLabel{"AnimGraph"};
        // 중첩 경로와 뷰 종류 및 ID로 구성한 캔버스 컨텍스트의 고유 키이다.
        FString mKey{"Root"};
    };

public:

    // 외부 설정 파일 없이 노드 캔버스를 사용하도록 에디터 초기 상태를 준비한다.
    FAnimGraphEditor();

    // 중첩 뷰의 Node Editor 컨텍스트를 해제한다.
    ~FAnimGraphEditor();

    // 편집 복사본과 캔버스 컨텍스트가 중복 소유되지 않도록 복사 생성을 금지한다.
    FAnimGraphEditor(const FAnimGraphEditor&) = delete;

    // 편집 객체와 캔버스 소유권의 중복을 막기 위해 복사 대입을 금지한다.
    FAnimGraphEditor& operator=(const FAnimGraphEditor&) = delete;

public:

    // 원본 그래프를 깊게 복사해 루트 편집 뷰를 열고 기존 탐색 상태를 초기화한다.
    bool Open(const std::filesystem::path& Path, FString& Error);

    // 변수, 경로 탐색, 현재 그래프 및 선택 속성을 그리고 저장 입력을 처리한다.
    void Draw();

    // 이 편집 창 또는 하위 창이 키보드 탐색 포커스를 갖고 있는지 확인한다.
    bool IsFocused() const;
    // 현재 포즈 그래프의 지정 상태 머신 노드를 상태 도표로 연다.
    bool OpenStateMachine(int32 NodeId);
    // 현재 상태 도표에서 선택한 상태의 내부 포즈 그래프로 이동한다.
    bool OpenState(int32 StateId);
    // 현재 상태 도표에서 지정 전환의 조건 그래프를 연다.
    bool OpenTransitionRule(int32 TransitionId);
    // 탐색 경로의 직전 뷰로 돌아간다.
    bool NavigateBack();
    // 지정 경로 깊이의 상위 뷰로 이동하고 더 깊은 탐색 경로를 정리한다.
    bool NavigateTo(std::size_t Depth);

private:
    // 편집 창을 닫고 편집 그래프와 경로 참조를 해제한다.
    void Close();

    // 현재 내부 뷰와 관계없이 루트 편집 그래프 전체를 원래 에셋 경로에 저장한다.
    bool Save();

    // 루트 변수 목록의 추가, 삭제와 개별 속성 편집 UI를 그린다.
    void DrawVariables(bool SaveRequested);
    // 지정 변수의 이름, 타입, 공급 방식과 기본값 편집을 처리한다.
    bool DrawVariable(std::size_t Index, bool SaveRequested);

    // 포즈 노드와 Get 노드의 표시, 연결, 삭제, 생성 및 배치 변경을 처리한다.
    void DrawGraph();
    // 변수 Get 노드를 그리며 기존 바인딩에 필요한 명시적 배치도 준비한다.
    void DrawValueGetters();
    // 파라미터의 변수 바인딩을 Float 입력 연결선으로 그린다.
    void DrawValueLinks();
    // 루트 변수 목록에서 이름에 대응하는 인덱스를 찾는다.
    int32 FindVariableIndex(const FString& Name) const;

    // 현재 선택한 포즈 노드의 이름과 타입별 파라미터 편집 UI를 그린다.
    void DrawSelection();
    // 현재 중첩 경로를 버튼으로 표시하고 상위 뷰 이동을 처리한다.
    void DrawNavigation();
    // Entry, Any State, 상태와 전환을 표시하고 상태 도표 편집을 처리한다.
    void DrawStateMachine();
    // 타입이 구분된 규칙 노드와 최종 Bool 결과의 연결 편집을 처리한다.
    void DrawTransitionRule();
    // 선택 상태의 이름 및 재진입 정책 또는 전환 시간과 우선순위를 편집한다.
    void DrawMachineSelection();
    // 선택한 규칙 노드의 상수, 변수 또는 대상 플레이어 속성을 편집한다.
    void DrawRuleSelection();
    // 현재 정의의 이름을 사용해 경로 탐색에 표시할 뷰 이름을 구한다.
    FString GetViewLabel(const FGraphView& View, const FGraphView* Parent) const;
    // 현재 선택을 경로에 저장하고 지정한 내부 그래프 종류로 이동한다.
    void Navigate(UAnimGraph* Graph, EGraphView Mode, int32 Machine, int32 Transition, const FString& Label);
    // 지정 뷰를 활성화하고 경로별 캔버스 및 저장된 선택을 복원한다.
    void ActivateView(const FGraphView& View);
    // 보관 중인 모든 Node Editor 컨텍스트를 소멸하고 현재 참조를 비운다.
    void ClearGraphContexts();

private:

    // 현재 활성 뷰의 노드 캔버스 컨텍스트이며 경로별 맵이 수명을 관리한다.
    ax::NodeEditor::EditorContext* mEditor{};

    // 저장 전 변경을 모으는 전체 그래프의 독립된 편집 복사본을 소유한다.
    std::unique_ptr<UAnimGraph> mRootEditing{};
    // 루트 복사본 또는 상태 내부 그래프 중 현재 편집할 그래프를 가리킨다.
    UAnimGraph* mEditing{};
    // 현재 그래프 종류, 경로 키와 선택을 나타내는 뷰 상태이다.
    FGraphView mView{};
    // 현재 뷰에 도달하기 전에 방문한 상위 뷰의 경로 스택이다.
    std::vector<FGraphView> mNavigation{};
    // 그래프 경로별로 캔버스를 분리해 선택, 확대율과 스크롤 상태가 서로 섞이지 않게 한다.
    std::unordered_map<FString, ax::NodeEditor::EditorContext*> mGraphContexts{};

    // 열려 있는 우클릭 메뉴가 적용될 Node Editor 노드 키이다.
    std::uintptr_t mContextNode{};

    // 편집한 루트 그래프를 저장할 실제 파일 경로이다.
    std::filesystem::path mPath{};

    // 현재 선택한 정의 노드 ID이며 선택이 없을 때는 -2를 사용한다.
    int32 mSelectedNode{-2};

    // 새 캔버스에 에셋의 저장 좌표를 한 번 적용해야 하는지 나타낸다.
    bool mApplyPositions{};

    // 노드 생성과 초기 배치 이후 캔버스를 전체 내용에 맞출지 나타낸다.
    bool mFitRequested{};

    // 마지막 저장 이후 편집 복사본에 저장할 변경이 생겼는지 나타낸다.
    bool mDirty{};

    // 편집 창이 열려 있는지 나타낸다.
    bool mOpen{};

    // 우클릭 메뉴 등으로 새 노드 또는 상태를 만들 가로 좌표이다.
    float mCreateX{};
    // 새 노드 또는 상태를 만들 세로 좌표이다.
    float mCreateY{};

    // 새 변수 이름을 입력하는 고정 크기 UI 버퍼이다.
    std::array<char, 64> mNewVariable{};

    // 최근 에셋 로드, 편집 또는 저장 실패를 창에 표시할 메시지이다.
    FString mError{};
};
