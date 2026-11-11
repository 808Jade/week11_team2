#pragma once

#include "Animation/AnimNode_Base.h"

// Sequence와 Blend Space 재생기가 시간, 루프, 역재생 및 종료 판정을 공유하는 기반이다.
struct FAnimNode_AssetPlayerBase : public FAnimNode_Base {
public:

    // 하위 입력을 초기화한 뒤 재생 방향에 맞는 시작 시각으로 되돌린다.
    void Initialize(UAnimInstance& AnimInstance) override;

    // 현재 재생 대상의 길이를 파생 플레이어에서 초 단위로 제공한다.
    virtual double GetCurrentAssetLength() const = 0;

    // 현재 재생 시각을 초 단위로 반환한다.
    double GetAccumulatedTime() const;

    // 유효한 재생 구간 안으로 시각을 지정하고 종료 상태를 해제한다.
    void SetAccumulatedTime(double Time);

    // 루프하지 않는 재생이 현재 방향의 끝에 도달했는지 반환한다.
    bool IsFinished() const;

    // 정재생은 처음, 역재생은 끝 시각으로 이동하고 종료 상태를 해제한다.
    void Restart();
    void SetPlaying(bool Playing);
    bool IsPlaying() const;
    bool SetPlayRate(float PlayRate);
    float GetPlayRate() const;
    void SetLooping(bool Looping);
    bool IsLooping() const;
    double GetPreviousAssetTime() const;
    bool DidWrapThisUpdate() const;

protected:

    // 배속을 적용해 시간을 진행하고 루프 순환 또는 종료 시각 고정을 처리한다.
    void AdvanceTime(float DeltaTime, double Length);

    // 재생 시간, 배속과 루프 여부를 공통 디버그 속성으로 추가한다.
    void GetPlayerDebugValues(std::vector<FAnimNodeValue>& OutValues);

public:

    // 재생 배속이며 음수는 역재생, 0은 시간 진행 정지를 의미한다.
    float mPlayRate{1.0f};

    // 끝에 도달했을 때 시간을 재생 구간 안으로 순환할지 지정한다.
    bool mLoop{true};

protected:

    // 현재 에셋 안에서 유지하는 재생 시각이다.
    double mInternalTime{};

    // 루프하지 않는 재생이 방향에 맞는 끝에 도달한 상태이다.
    bool mFinished{};
    bool mPlaying{true};
    double mPreviousTime{};
    bool mWrappedThisUpdate{};
    bool mIncludeStartOnNextAdvance{true};
    bool mIncludeStartThisUpdate{true};
    bool mAdvancedThisUpdate{};
};
