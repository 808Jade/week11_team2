#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include <vector>

#include <memory>

class FAnimAssetArchive;

class UAnimGraph;

enum class EAnimRuleNodeType : uint8 {
    Float,
    Bool,
    GetFloat,
    GetBool,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Equal,
    NotEqual,
    And,
    Or,
    Not,
    StateTime,
    TimeRemaining,
    Finished,
    Count
};

// 전환 조건의 상수, 변수 조회 또는 연산 하나를 저장한다. 입력은 다른 규칙 노드의 ID이다.
struct FAnimRuleNode {
public:
    // 연결과 에디터 선택에 사용하는 규칙 노드의 고유 ID이다.
    int32 mId{};
    // 상수, 변수 조회, 비교 또는 논리 연산 중 수행할 규칙 종류이다.
    EAnimRuleNodeType mType{};
    // -1은 연결되지 않은 입력이다. 실제 필요한 입력 개수는 규칙 노드 타입에 따라 달라진다.
    int32 mInputA{-1};
    // 두 번째 입력을 공급하는 규칙 노드 ID이며 연결이 없으면 -1이다.
    int32 mInputB{-1};
    // 변수 조회에는 변수 이름, 재생 시간 및 종료 조회에는 대상 플레이어 이름을 사용한다.
    FString mName{};
    // Float 상수 노드가 출력할 값이다.
    float mValue{};
    // Bool 상수 노드가 출력할 값이다.
    bool mBoolValue{};
    // 전환 규칙 편집기에 저장할 노드의 가로 좌표이다.
    float mX{};
    // 전환 규칙 편집기에 저장할 노드의 세로 좌표이다.
    float mY{};
};

// 최종 Bool 결과로 전환 가능 여부를 나타내는 조건 그래프의 편집 및 저장 데이터이다.
class FAnimTransitionGraph {
public:
    // 전환 조건을 구성하는 규칙 노드 목록을 반환한다.
    const std::vector<FAnimRuleNode>& GetNodes() const;
    // 배열 위치와 독립된 고유 ID로 규칙 노드를 찾으며 없으면 nullptr를 반환한다.
    FAnimRuleNode* FindNode(int32 Id);
    // 배열 위치와 독립된 고유 ID로 규칙 노드를 찾으며 없으면 nullptr를 반환한다.
    const FAnimRuleNode* FindNode(int32 Id) const;
    // 유효한 타입과 좌표로 규칙 노드를 추가하고 새 ID 또는 실패 시 -1을 반환한다.
    int32 AddNode(EAnimRuleNodeType Type, float X, float Y);
    // 규칙 노드와 이를 참조하는 입력 및 최종 결과 연결을 제거한다.
    bool RemoveNode(int32 Id);
    // 입력 타입, 핀 개수와 순환 여부를 확인한 뒤 규칙 노드 사이를 연결한다.
    bool Connect(int32 From, int32 To, int32 Pin);
    // 대상 규칙 노드의 지정 입력 연결을 해제한다.
    bool Disconnect(int32 To, int32 Pin);
    // 최종 Bool 결과 노드를 지정하며 -1이면 결과 연결을 해제한다.
    bool SetResult(int32 Id);
    // 전환 가능 여부를 출력할 규칙 노드 ID를 반환한다.
    int32 GetResult() const;
    // 모든 노드의 ID, 값, 타입과 연결 및 순환 여부를 검사한다.
    bool Validate() const;
    // 변수 조회 노드가 참조하는 이름을 부모 그래프의 변경에 맞춰 바꾼다.
    void RenameVariable(const FString& OldName, const FString& NewName);
    // 결과 ID와 규칙 노드를 저장하거나 읽고 그래프 유효성을 검사한다.
    void Serialize(FAnimAssetArchive& Archive);

    // 지정 타입의 규칙 노드가 Bool 결과를 출력하는지 반환한다.
    static bool IsBool(EAnimRuleNodeType Type);
    // 지정 규칙 타입이 요구하는 입력 핀 개수를 반환한다.
    static int32 GetInputCount(EAnimRuleNodeType Type);
    // 지정 규칙 타입이 입력으로 Bool 값을 받는지 반환한다.
    static bool IsBoolInput(EAnimRuleNodeType Type);
    // 규칙 노드 타입에 대응하는 에디터 제목을 반환한다.
    static const char* GetTitle(EAnimRuleNodeType Type);

private:
    // 입력 의존 관계를 따라 다른 노드에 도달하는지 검사해 순환을 판별한다.
    bool Reaches(int32 From, int32 To) const;

private:
    // 이 전환 조건을 구성하는 상수, 조회 및 연산 노드 목록이다.
    std::vector<FAnimRuleNode> mNodes{};
    // 다음 규칙 노드에 발급할 ID이며 편집 중 삭제한 ID를 재사용하지 않는다.
    int32 mNextId{};
    // 최종 전환 조건을 출력하는 Bool 노드의 ID이며 미연결 상태는 -1이다.
    int32 mResult{-1};
};

// 상태 이름과 에디터 배치, 재진입 정책 및 해당 상태의 포즈 그래프를 보관한다.
struct FAnimStateData {
public:
    // 시작 상태와 전환이 참조하는 상태의 고유 ID이다.
    int32 mId{};
    // 상태 도표와 실행 디버그에서 표시할 상태 이름이다.
    FString mName{};
    // 상태 도표에 저장할 가로 배치 좌표이다.
    float mX{};
    // 상태 도표에 저장할 세로 배치 좌표이다.
    float mY{};
    // 이 상태에 다시 진입할 때 내부 포즈 노드의 재생 상태를 초기화할지 지정한다.
    bool mResetOnEntry{true};
    // 상태는 단일 클립뿐 아니라 Blend Space나 중첩 상태 머신을 포함한 포즈 그래프를 가진다.
    std::shared_ptr<UAnimGraph> mGraph{};
};

// 출발과 도착 상태의 ID, 블렌딩 시간, 우선순위와 조건 그래프를 보관한다.
struct FAnimTransitionData {
public:
    // 상태 도표의 연결 선택과 실행 디버그에 사용하는 전환의 고유 ID이다.
    int32 mId{};
    // 출발 상태 ID이며 AnyStateId이면 모든 현재 상태에서 이 전환을 검사한다.
    int32 mFromState{};
    // 조건이 참일 때 진입할 도착 상태 ID이다.
    int32 mToState{};

    // 출발 포즈에서 도착 포즈로 교차 블렌딩할 시간이며 초 단위이다.
    float mDuration{0.2f};
    // 작은 값부터 검사하며, 같은 우선순위에서는 전환 목록의 순서를 유지한다.
    int32 mPriority{};
    
    // 전환 가능 여부를 Bool 결과로 계산하는 조건 그래프이다.
    FAnimTransitionGraph mRule{};
};

// 공유할 상태 머신 구성만 관리한다. 현재 상태와 재생 시간은 FAnimNode_StateMachine이 가진다.
class FAnimStateMachineData {
public:
    // 한 상태 머신 정의에 허용할 최대 상태 개수이다.
    static constexpr uint32 MaxStates{254};
    // 어느 현재 상태에서도 출발 가능한 전환을 나타내는 예약 출발 ID이다.
    static constexpr int32 AnyStateId{-1};

    // 상태 목록을 읽기 전용으로 반환한다.
    const std::vector<FAnimStateData>& GetStates() const;
    // 작성 순서의 전환 목록을 읽기 전용으로 반환한다.
    const std::vector<FAnimTransitionData>& GetTransitions() const;
    
    // 고유 ID로 상태를 찾으며 존재하지 않으면 nullptr를 반환한다.
    FAnimStateData* FindState(int32 Id);
    // 고유 ID로 상태를 찾으며 존재하지 않으면 nullptr를 반환한다.
    const FAnimStateData* FindState(int32 Id) const;
    // 고유 ID로 전환을 찾으며 존재하지 않으면 nullptr를 반환한다.
    FAnimTransitionData* FindTransition(int32 Id);
    // 고유 ID로 전환을 찾으며 존재하지 않으면 nullptr를 반환한다.
    const FAnimTransitionData* FindTransition(int32 Id) const;
    
    // 고유 이름과 ID를 가진 상태 및 부모 변수를 복사한 포즈 그래프를 생성한다.
    int32 AddState(const FString& Name, float X, float Y, const UAnimGraph& Owner);
    // 상태와 연결된 전환을 삭제하고 필요하면 시작 상태를 다시 지정한다.
    bool RemoveState(int32 Id);
    // 출발 상태 또는 Any State에서 기존 도착 상태로 향하는 기본 전환을 추가한다.
    int32 AddTransition(int32 From, int32 To);
    // 해당 ID의 전환을 제거하고 실제 삭제 여부를 반환한다.
    bool RemoveTransition(int32 Id);
    // 존재하는 상태를 머신 초기화 시 처음 진입할 상태로 지정한다.
    bool SetEntryState(int32 Id);
    // 시작 상태의 고유 ID를 반환하며 상태가 없으면 -1을 반환한다.
    int32 GetEntryState() const;

    // 유한한 좌표를 검사하고 Any State 표시 노드의 에디터 위치를 변경한다.
    bool SetAnyStatePosition(float X, float Y);
    // Any State 표시 노드의 저장된 가로 좌표를 반환한다.
    float GetAnyStateX() const;
    // Any State 표시 노드의 저장된 세로 좌표를 반환한다.
    float GetAnyStateY() const;

    // 상태, 전환과 ID 카운터를 복사하고 내부 포즈 그래프는 깊게 복사한다.
    void CopyFrom(const FAnimStateMachineData& Source);
    // 상태와 전환의 구성 및 시작 상태를 검증하며 저장하거나 불러온다.
    void Serialize(FAnimAssetArchive& Archive);

private:
    // 이 상태 머신이 소유하는 상태 정의와 내부 포즈 그래프 목록이다.
    std::vector<FAnimStateData> mStates{};
    // 상태 사이의 전환 정의이며 같은 우선순위에서는 이 목록 순서를 사용한다.
    std::vector<FAnimTransitionData> mTransitions{};

    // Any State 표시 노드의 에디터 가로 좌표이며 실제 재생 상태 좌표와 무관하다.
    float mAnyStateX{-180.0f};
    // Any State 표시 노드의 에디터 세로 좌표이다.
    float mAnyStateY{180.0f};
   
    // 처음 진입할 상태의 ID이며, 상태가 하나도 없으면 -1이다.
    int32 mEntryState{-1};
    // 편집 중 삭제된 ID를 다시 발급하지 않아 기존 참조와 새 항목이 섞이지 않게 한다.
    int32 mNextStateId{};
    // 다음에 생성할 전환의 고유 ID이다. 재생할 다음 상태를 의미하지 않는다.
    int32 mNextTransitionId{};
};
