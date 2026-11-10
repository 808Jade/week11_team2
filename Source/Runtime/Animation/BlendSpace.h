#pragma once

#include "Animation/AnimationAsset.h"
#include "Math/Vector.h"
#include <vector>

#include "BlendSpace.generated.h"

class FAnimAssetArchive;
class UAnimSequenceBase;

enum class EBlendSpaceAxis : uint8 {
    X,
    Y,
    Z,
    Max
};

struct FBlendParameter {
public:

    FString mDisplayName{"Speed"};

    float mMin{0.0f};

    float mMax{5.0f};

    int32 mGridDivisions{4};
};

struct FBlendSample {
public:

    const UAnimSequenceBase* mAnimation{};

    FVector mSampleValue{};
};

struct FBlendSampleWeight {
public:

    int32 mSampleIndex{-1};

    float mWeight{};
};

UCLASS()
class UBlendSpace : public UAnimationAsset {
    GENERATED_BODY()

public:

    static constexpr uint32 MaxSamples{64};

public:
    UBlendSpace() = default;

    ~UBlendSpace() override = default;

public:

    virtual void GetSamplesToBlend(const FVector& Input, std::vector<FBlendSampleWeight>& OutWeights) const = 0;
    virtual uint32 GetNumAxes() const = 0;

    const std::vector<FBlendSample>& GetSamples() const;

    int32 AddSample(const UAnimSequenceBase* Animation, const FVector& SampleValue);
    bool RemoveSample(int32 Index);

    bool SetBlendParameter(EBlendSpaceAxis Axis, const FBlendParameter& Parameter);
    bool SetSampleAnimation(int32 Index, const UAnimSequenceBase* Animation);
    bool SetSampleValue(int32 Index, const FVector& SampleValue);

    const FBlendParameter& GetBlendParameter(EBlendSpaceAxis Axis) const;
    double GetBlendedPlayLength(const std::vector<FBlendSampleWeight>& Weights) const;
    float GetPlayLength() const override;

    bool IsValid() const;

    void CopyFrom(const UBlendSpace& Source);
    void Serialize(FAnimAssetArchive& Archive);

protected:
    FVector ClampInput(const FVector& Input) const;

private:
    virtual void RebuildSampleOrder() = 0;

private:
    FBlendParameter mBlendParameters[static_cast<uint8>(EBlendSpaceAxis::Max)]{};

    std::vector<FBlendSample> mSamples{};
};
