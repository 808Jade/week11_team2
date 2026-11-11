#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"

#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace ax::NodeEditor {
    struct EditorContext;
}

class UAnimInstance;
class UAnimGraph;
class UAnimGraphInstance;
class USkeletalMeshComponent;
struct FAnimNode_Base;
class FAnimNode_StateMachine;

// 선택한 메시의 실행 그래프를 표시하고 PIE 변수, 실제 평가 경로와 전환 상태를 관찰한다.
class FAnimGraphPanel final {
private:
    enum class EGraphView : uint8 {
        Pose,
        Machine,
        Rule
    };

    struct FGraphView {
        // 현재 실행 뷰의 원본 그래프 정의이며 인스턴스가 유효성을 확인한다.
        const UAnimGraph* mGraph{};
        // 현재 포즈 뷰의 평가 루트에 해당하는 실행 노드의 비소유 포인터이다.
        FAnimNode_Base* mRoot{};
        // 현재 상태 도표 또는 전환 조건을 관찰할 실행 상태 머신이다.
        FAnimNode_StateMachine* mMachine{};
        // 포즈, 상태 도표와 전환 규칙 중 현재 표시할 실행 뷰 종류이다.
        EGraphView mMode{};
        // 원본 그래프에서 상태 머신 정의를 찾을 포즈 노드 ID이다.
        int32 mMachineId{-1};
        // 현재 관찰하는 전환 조건 그래프의 전환 ID이다.
        int32 mTransition{-1};
        // 실행 그래프의 경로 탐색 UI에 표시할 뷰 이름이다.
        FString mLabel{"Anim Graph"};
        // 중첩 경로의 캔버스를 구분하는 고유 키이다.
        FString mKey{"Root"};
    };

    // 중첩 뷰마다 별도 캔버스와 배치 이력을 유지한다. 디버그 배치는 에셋을 수정하지 않는다.
    struct FGraphCanvas {
        // 이 중첩 뷰가 소유하는 Node Editor 컨텍스트이다.
        ax::NodeEditor::EditorContext* mEditor{};
        // 자동 배치를 마친 UI 노드 키를 기록해 사용자가 옮긴 위치를 유지한다.
        std::unordered_set<uint64> mPlacedNodes{};
        // 새 배치를 마친 뒤 캔버스의 전체 내용을 화면에 맞출지 나타낸다.
        bool mFitRequested{};
    };

public:
    // 실행 대상과 캔버스가 비어 있는 디버그 패널을 생성한다.
    FAnimGraphPanel();

    // 모든 중첩 디버그 뷰의 캔버스 컨텍스트를 소멸한다.
    ~FAnimGraphPanel();
    // 실행 포인터와 캔버스 소유권의 중복을 막기 위해 복사 생성을 금지한다.
    FAnimGraphPanel(const FAnimGraphPanel&) = delete;
    // 디버그 탐색 상태와 캔버스 소유권이 복제되지 않도록 복사 대입을 금지한다.
    FAnimGraphPanel& operator=(const FAnimGraphPanel&) = delete;

public:
    // 지정 메시의 실행 인스턴스를 디버그 창에 표시하고 창의 열림 상태를 반영한다.
    void Draw(USkeletalMeshComponent* Component, bool Playing, bool& Open);
    // 현재 포즈 뷰의 지정 상태 머신을 실행 상태 도표로 연다.
    bool OpenStateMachine(int32 NodeId);
    // 선택한 상태의 원본 그래프와 실행 루트를 대응시켜 포즈 뷰를 연다.
    bool OpenState(int32 StateId);
    // 선택한 전환의 규칙 정의와 실행 상태를 연결해 실시간 조건 뷰를 연다.
    bool OpenTransitionRule(int32 TransitionId);
    // 지정 경로 깊이의 상위 실행 뷰로 이동한다.
    bool NavigateTo(std::size_t Depth);

private:
    // 실행 대상 유효성을 확인하고 변수, 탐색과 현재 종류의 그래프를 그린다.
    void DrawContent(USkeletalMeshComponent* Component, bool Playing);

    // 인스턴스 변수를 표시하고 PIE 중 수정 가능한 값의 입력 UI를 제공한다.
    void DrawVariables(UAnimInstance& Instance, bool Playing);

    // 현재 포즈 뷰의 노드, 값 입력과 실제 평가 기여도에 따른 연결선을 그린다.
    void DrawGraph(UAnimInstance& Instance, bool Playing);
    // 실행 중인 상태, 전환과 블렌딩 비율을 원본 상태 도표 위에 표시한다.
    void DrawStateMachine(UAnimInstance& Instance, bool Playing);
    // 전환 규칙 노드별 현재 값과 최종 결과를 읽기 전용 그래프로 표시한다.
    void DrawTransitionRule(UAnimInstance& Instance, bool Playing);
    // 현재 실행 뷰의 중첩 경로를 그리고 상위 뷰 이동을 처리한다.
    void DrawNavigation();
    // 인스턴스, 원본 또는 실행 세대 변경 시 이전 뷰 참조를 초기화한다.
    void SynchronizeView(UAnimInstance& Instance);
    // 지정 실행 뷰의 캔버스를 만들거나 재사용해 활성화한다.
    void ActivateView(const FGraphView& View);
    // 현재 실행 뷰를 경로에 저장하고 하위 뷰로 이동한다.
    void Navigate(FGraphView View);
    // 모든 캔버스를 소멸하고 경로와 현재 캔버스 참조를 비운다.
    void ClearGraphContexts();
    // 현재 그래프 정의에 대응하는 실행 노드만 수집해 내부 그래프가 섞이지 않게 한다.
    void CollectViewNodes(std::vector<FAnimNode_Base*>& Nodes) const;

    // 미배치 노드를 원본 좌표 또는 평가 깊이에 배치하고 배치 발생 여부를 반환한다.
    bool PlaceNodes(const UAnimInstance& Instance, FAnimNode_Base* Root, bool All);

private:
    // 현재 관찰할 원본 정의와 실행 노드의 대응 및 뷰 종류이다.
    FGraphView mView{};
    // 현재 실행 뷰에 도달하기 전의 상위 뷰 경로이다.
    std::vector<FGraphView> mNavigation{};
    // 중첩 경로별 캔버스와 배치 이력을 소유하는 맵이다.
    std::unordered_map<FString, FGraphCanvas> mGraphContexts{};
    // 현재 뷰의 캔버스이며 경로별 컨텍스트 맵이 수명을 관리한다.
    FGraphCanvas* mCanvas{};
    // 현재 디버그 대상의 그래프 실행 인스턴스를 가리키는 비소유 포인터이다.
    UAnimGraphInstance* mGraphInstance{};
    // 현재 캔버스를 만들 때 대응시킨 원본 루트 그래프이다.
    const UAnimGraph* mSourceGraph{};
    // 인스턴스 교체와 그래프 재생성 모두 감지해 이전 실행 노드에 대한 참조를 폐기한다.
    uint32 mInstanceUUID{};
    // 실행 노드 재구성 시 이전 탐색 상태를 버리기 위해 기록한 세대 번호이다.
    uint64 mGraphGeneration{};

    // 다음 그리기에서 현재 포즈 뷰의 노드를 다시 배치할지 나타낸다.
    bool mLayoutRequested{};

    // 디버그 창의 변수 열에 적용할 초기 폭이다.
    float mVariablesWidth{260.0f};
};
