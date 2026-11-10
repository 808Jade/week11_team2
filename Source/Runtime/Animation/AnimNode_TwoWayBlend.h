#pragma once

#include "Animation/AnimNode_Base.h"

struct FAnimNode_TwoWayBlend : public FAnimNode_Base {
public:

    static constexpr float RelevantWeightThreshold{0.00001f};

public:
    void Update(const FAnimationUpdateContext& Context) override;

    void Evaluate(FPoseContext& Output) override;

    const char* GetNodeType() const override;

    void GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs) override;

    void GetDebugValues(std::vector<FAnimNodeValue>& OutValues) override;

    float GetBlendAlpha() const;

public:

    FPoseLink mA{};

    FPoseLink mB{};

    float mAlpha{};

    const float* mAlphaBinding{};

    FString mAlphaBindingName{};

private:

    float mBlendAlpha{};
};
