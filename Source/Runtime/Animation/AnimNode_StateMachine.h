#pragma once

#include "Animation/AnimStateMachineData.h"
#include "Animation/AnimNode_Base.h"

// 규칙 노드 ID에 대응하는 현재 값을 표시하며, monostate는 평가할 수 없는 입력을 나타낸다.
struct FAnimRuleDebugValue {
public:
    // 현재 값을 표시할 원본 규칙 노드의 ID이다.
    int32 mId{};
    // 평가된 Float 또는 Bool 값이며 유효한 결과가 없으면 monostate이다.
    std::variant<std::monostate, float, bool> mValue{};
};

// 상태 정의를 실행 포즈 링크와 연결하고, 이번 진입 이후의 경과 시간을 따로 보관한다.
struct FAnimStateRuntime {
public:
    // 이 실행 상태에 대응하는 상태 정의의 고유 ID이다.
    int32 mId{};
    // 실행 디버그에서 표시할 상태 이름이다.
    FString mName{};
    // 해당 상태의 실행 포즈 그래프 루트를 가리키는 비소유 링크이다.
    FPoseLink mPose{};
    // 진입 시 내부 포즈 그래프의 재생을 초기화할지 복사한 설정이다.
    bool mResetOnEntry{true};
    // 이번 진입 이후 이 상태가 활성 상태 또는 블렌딩 대상으로 갱신된 누적 시간이다.
    float mElapsed{};
};

// 인스턴스별 현재 상태를 갱신하고, 전환 중에는 출발과 도착 상태의 포즈를 혼합한다.
class FAnimNode_StateMachine final : public FAnimNode_Base {
public:
    // 시작 상태 ID와 전환 정의를 복사하고 전환을 우선순위 순으로 정렬한다.
    void Configure(const FAnimStateMachineData& Data);
    // 상태 정의를 별도로 구성한 실행 포즈 루트와 연결한다.
    void AddState(const FAnimStateData& Data, FAnimNode_Base* Root);

    // 하위 포즈를 초기화한 뒤 시작 상태로 진입하고 전환 및 시간 기록을 지운다.
    void Initialize(UAnimInstance& AnimInstance) override;
    // 활성 상태를 갱신하고 전환 조건을 검사하거나 진행 중인 블렌딩을 완료한다.
    void Update(const FAnimationUpdateContext& Context) override;
    // 현재 상태의 포즈를 출력하고 전환 중에는 도착 상태 포즈와 혼합한다.
    void Evaluate(FPoseContext& Output) override;
    FRootMotionDelta GetRootMotionDelta() const override;

    // 디버그 표시용 State Machine 타입 이름을 반환한다.
    const FString& GetNodeType() const override;
    // 모든 상태의 포즈 링크를 초기화와 그래프 탐색에 사용할 입력으로 노출한다.
    void GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs) override;
    // 현재 및 도착 상태, 전환 진행률과 최근 조건 판정을 디버그 목록에 추가한다.
    void GetDebugValues(std::vector<FAnimNodeValue>& OutValues) override;

    // 현재 상태의 정의 ID를 반환하며 유효한 상태가 없으면 -1을 반환한다.
    int32 GetCurrentState() const;
    // 블렌딩 중인 도착 상태의 정의 ID를 반환하며 전환 중이 아니면 -1을 반환한다.
    int32 GetTargetState() const;
    // 도착 상태의 혼합 비율을 반환하며 진행 중인 전환이 없으면 1을 반환한다.
    float GetBlendAlpha() const;
    // 현재 블렌딩을 일으킨 전환 ID를 반환하며 블렌딩 중이 아니면 -1이다.
    int32 GetActiveTransition() const;
    // 상태 정의 ID에 대응하는 실행 상태를 찾으며 없으면 nullptr를 반환한다.
    const FAnimStateRuntime* FindState(int32 Id) const;

private:
    // 상태 정의 ID를 실행 상태 배열의 인덱스로 변환한다.
    int32 FindStateIndex(int32 Id) const;

private:
    // 상태별 실행 포즈 링크와 경과 시간을 가진 이 인스턴스의 상태 목록이다.
    std::vector<FAnimStateRuntime> mStates{};
    // 우선순위로 안정 정렬한 이 인스턴스의 전환 정의 복사본이다.
    std::vector<FAnimTransitionData> mTransitions{};
    // 최근 전환 검사에서 수집한 전환별 허용 여부의 표시 값이다.
    std::vector<FAnimNodeValue> mRuleDebugValues{};

    // 초기 진입할 상태의 정의 ID이며 Initialize에서 실행 인덱스로 변환한다.
    int32 mEntry{-1};
    // 내부 실행 상태는 배열 인덱스로 접근하고, 외부 조회 함수에서는 정의의 ID로 변환한다.
    int32 mCurrent{-1};
    // 교차 블렌딩 중인 도착 상태의 실행 배열 인덱스이며 평상시에는 -1이다.
    int32 mTarget{-1};
    // 진행 중인 블렌딩에 해당하는 전환 정의 ID이다.
    int32 mActiveTransition{-1};

    // 현재 전환을 시작한 뒤 누적한 블렌딩 시간이다.
    float mBlendElapsed{};
    // 현재 전환을 완료할 때까지 사용할 전체 블렌딩 시간이다.
    float mBlendDuration{};
};

// 전환 규칙을 인스턴스 변수와 현재 상태의 재생 정보로 평가한다.
class FAnimTransitionEvaluator {
public:
    // 인스턴스 변수와 출발 상태의 재생 정보로 규칙의 최종 Bool 결과를 판정한다.
    static bool Evaluate(const FAnimTransitionGraph& Graph, const FAnimationUpdateContext& Context, FAnimNode_Base* StateRoot, float StateTime);
    // 규칙 노드별 현재 평가 값을 계산해 디버그 표시 목록을 채운다.
    static void GetDebugValues(const FAnimTransitionGraph& Graph, const FAnimationUpdateContext& Context, FAnimNode_Base* StateRoot, float StateTime, std::vector<FAnimRuleDebugValue>& OutValues);
};
