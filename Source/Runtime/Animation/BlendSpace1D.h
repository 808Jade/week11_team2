#pragma once

#include "Animation/BlendSpace.h"

#include <array>

#include "BlendSpace1D.generated.h"

UCLASS()
class UBlendSpace1D final : public UBlendSpace {
    GENERATED_BODY()

public:
    UBlendSpace1D() = default;

    ~UBlendSpace1D() override = default;

public:
    void GetSamplesToBlend(const FVector& Input, std::vector<FBlendSampleWeight>& OutWeights) const override;

    uint32 GetNumAxes() const override;

private:
    void RebuildSampleOrder() override;

private:
    std::array<int32, MaxSamples> mSortedSampleIndices{};
};
