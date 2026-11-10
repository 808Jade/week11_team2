#pragma once

#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/BlendSpace.h"

struct FAnimNode_BlendSpacePlayer : public FAnimNode_AssetPlayerBase {
public:
    void Initialize(UAnimInstance& AnimInstance) override;

    void Update(const FAnimationUpdateContext& Context) override;

    void Evaluate(FPoseContext& Output) override;

    const char* GetNodeType() const override;

    void GetDebugValues(std::vector<FAnimNodeValue>& OutValues) override;

    double GetCurrentAssetLength() const override;

    const std::vector<FBlendSampleWeight>& GetSampleWeights() const;

    double GetNormalizedTime() const;

public:

    const UBlendSpace* mBlendSpace{};

    float mX{};

    const float* mXBinding{};

    FString mXBindingName{};

private:

    double mNormalizedTime{};

    double mBlendedLength{};

    std::vector<FBlendSampleWeight> mSampleWeights{};
};
