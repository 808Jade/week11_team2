#pragma once

#include "Animation/AnimNode_Base.h"

// Alpha가 0이면 A, 1이면 B를 출력하고 중간 값에서는 두 포즈를 혼합한다.
struct FAnimNode_TwoWayBlend : public FAnimNode_Base {
public:

    // 기여도가 사실상 0인 입력의 갱신과 평가를 생략하는 경계값이다.
    static constexpr float RelevantWeightThreshold{0.00001f};

public:
    // Alpha 입력을 읽고 유효 범위로 제한한 뒤 기여하는 포즈 입력만 갱신한다.
    void Update(const FAnimationUpdateContext& Context) override;

    // Alpha에 따라 A 또는 B를 출력하거나 두 입력의 본 포즈를 혼합한다.
    void Evaluate(FPoseContext& Output) override;
    FRootMotionDelta GetRootMotionDelta() const override;

    // 디버그 표시용 Two-Way Blend 타입 이름을 반환한다.
    const FString& GetNodeType() const override;

    // 그래프 연결과 탐색에 사용할 A 및 B 포즈 입력을 노출한다.
    void GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs) override;

    // 현재 Alpha를 디버그 패널에서 확인하고 조절할 수 있게 노출한다.
    void GetDebugValues(std::vector<FAnimNodeValue>& OutValues) override;
    // 변수 Get 노드에 바인딩할 수 있는 Alpha 입력을 노출한다.
    void GetValueInputs(std::vector<FAnimNodeValueInput>& OutInputs) override;

    // 이번 Update에서 실제 평가에 사용하도록 확정한 혼합 비율을 반환한다.
    float GetBlendAlpha() const;

public:

    // Alpha가 0일 때 최종 출력이 되는 첫 번째 포즈 입력이다.
    FPoseLink mA{};

    // Alpha가 1일 때 최종 출력이 되는 두 번째 포즈 입력이다.
    FPoseLink mB{};

    // 직접 지정하거나 값 입력에서 받은 요청 혼합 비율이다.
    float mAlpha{};

    // 외부 Float 값 노드로부터 Alpha를 공급받는 링크이다.
    FAnimFloatLink mAlphaInput{};

private:

    // 유효성 검사와 0~1 제한을 마친 이번 프레임의 실제 혼합 비율이다.
    float mBlendAlpha{};
};
