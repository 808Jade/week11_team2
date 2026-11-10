#pragma once

#include "Animation/AnimNode_Base.h"

struct FAnimNode_AssetPlayerBase : public FAnimNode_Base {
public:

    void Initialize(UAnimInstance& AnimInstance) override;

    virtual double GetCurrentAssetLength() const = 0;

    double GetAccumulatedTime() const;

    void SetAccumulatedTime(double Time);

    bool IsFinished() const;

    void Restart();

protected:

    void AdvanceTime(float DeltaTime, double Length);

    void GetPlayerDebugValues(std::vector<FAnimNodeValue>& OutValues);

public:

    float mPlayRate{1.0f};

    bool mLoop{true};

protected:

    double mInternalTime{};

    bool mFinished{};
};
