#pragma once

#include "Animation/AnimNode_AssetPlayerBase.h"

class UAnimSequenceBase;

// 단일 시퀀스의 시간을 갱신하고 해당 시점의 본 포즈를 평가한다.
struct FAnimNode_SequencePlayer : public FAnimNode_AssetPlayerBase {
public:
    // 유효한 시퀀스와 양의 배속을 검증한 뒤 재생 설정을 바꾸고 다시 시작한다.
    bool SetSequence(const UAnimSequenceBase* Sequence, bool Looping, float PlayRate, FString& OutError);
    // 일시 정지가 아니면 공통 플레이어 로직으로 시퀀스 시간을 진행한다.
    void Update(const FAnimationUpdateContext& Context) override;

    // 현재 시각의 시퀀스 포즈를 출력하며 에셋이 없으면 기준 포즈를 사용한다.
    void Evaluate(FPoseContext& Output) override;

    // 디버그 표시용 Sequence Player 타입 이름을 반환한다.
    const FString& GetNodeType() const override;

    // 시퀀스 이름과 공통 플레이어 속성을 디버그 목록에 추가한다.
    void GetDebugValues(std::vector<FAnimNodeValue>& OutValues) override;

    // 지정된 시퀀스의 재생 길이를 반환하며 에셋이 없으면 0을 반환한다.
    double GetCurrentAssetLength() const override;

    // Update에서 재생 시간을 진행할지 일시 정지할지 지정한다.
    void SetPaused(bool Paused);
    FRootMotionDelta GetRootMotionDelta() const override;

public:

    // 현재 샘플링할 공유 시퀀스 에셋의 비소유 포인터이다.
    const UAnimSequenceBase* mSequence{};

private:
    // 시퀀스 포즈를 유지한 채 재생 시간 갱신을 멈출지 나타낸다.
    FRootMotionDelta mRootMotionDelta{};
};
