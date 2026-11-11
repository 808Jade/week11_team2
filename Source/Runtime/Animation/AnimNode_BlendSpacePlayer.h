#pragma once

#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/BlendSpace.h"

// Blend Space 에셋을 입력 X와 공통 재생 위상으로 평가하는 인스턴스별 재생 노드이다.
struct FAnimNode_BlendSpacePlayer : public FAnimNode_AssetPlayerBase {
public:
    // 샘플 가중치와 혼합 길이를 지우고 재생 방향에 맞춰 위상을 초기화한다.
    void Initialize(UAnimInstance& AnimInstance) override;

    // 입력 X로 샘플과 혼합 길이를 구한 뒤 공통 재생 위상을 진행한다.
    void Update(const FAnimationUpdateContext& Context) override;

    // 같은 위상의 샘플 포즈를 평가해 가중치에 따라 누적 혼합한다.
    void Evaluate(FPoseContext& Output) override;
    FRootMotionDelta GetRootMotionDelta() const override;

    // 디버그 표시용 Blend Space Player 타입 이름을 반환한다.
    const FString& GetNodeType() const override;

    // Blend Space 이름, 입력 X와 공통 플레이어 속성을 추가한다.
    void GetDebugValues(std::vector<FAnimNodeValue>& OutValues) override;
    // 변수 Get 노드에 바인딩할 수 있는 X 입력을 노출한다.
    void GetValueInputs(std::vector<FAnimNodeValueInput>& OutInputs) override;

    // 현재 샘플 가중치로 계산한 혼합 재생 길이를 반환한다.
    double GetCurrentAssetLength() const override;

    // 이번 갱신에서 선택한 샘플과 로컬 가중치를 반환한다.
    const std::vector<FBlendSampleWeight>& GetSampleWeights() const;

    // 각 샘플의 길이에 공통으로 적용할 0~1 재생 위상을 반환한다.
    double GetNormalizedTime() const;

public:

    // 축과 샘플 배치를 공유하는 Blend Space 에셋의 비소유 포인터이다.
    const UBlendSpace* mBlendSpace{};

    // 현재 평가할 축 입력이며 바인딩이 없으면 직접 설정한 값을 사용한다.
    float mX{};

    // 외부 Float 값 노드로부터 축 입력을 공급받는 링크이다.
    FAnimFloatLink mXInput{};

private:

    // 길이가 다른 클립도 같은 주기 비율에서 평가하도록 0~1의 재생 위상을 유지한다.
    double mNormalizedTime{};
    FRootMotionDelta mRootMotionDelta{};

    // 선택한 샘플 길이의 가중 평균이며 시간 진행의 재생 주기로 사용한다.
    double mBlendedLength{};

    // 이번 입력에서 실제 포즈 생성에 사용할 샘플 인덱스와 가중치 목록이다.
    std::vector<FBlendSampleWeight> mSampleWeights{};
};
