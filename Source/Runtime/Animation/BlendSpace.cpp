#include "EnginePCH.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimAssetArchive.h"
#include "Asset/AssetManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>

namespace {
    bool IsFiniteVector(const FVector& Value) {
        return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
    }

    constexpr uint32 BlendSpaceFormatVersion{1};

    bool IsValidParameter(const FBlendParameter& Parameter) {
        return std::isfinite(Parameter.mMin) && std::isfinite(Parameter.mMax) && Parameter.mMin < Parameter.mMax && Parameter.mGridDivisions >= 1 && Parameter.mGridDivisions <= 100;
    }
}

const FBlendParameter& UBlendSpace::GetBlendParameter(EBlendSpaceAxis Axis) const {
    const auto AxisIndex{static_cast<std::size_t>(Axis)};

    return mBlendParameters[std::min(AxisIndex, std::size(mBlendParameters) - 1)];
}

bool UBlendSpace::SetBlendParameter(EBlendSpaceAxis Axis, const FBlendParameter& Parameter) {
    const auto AxisIndex{static_cast<std::size_t>(Axis)};

    if (AxisIndex >= GetNumAxes() || AxisIndex >= std::size(mBlendParameters) || !IsValidParameter(Parameter)) {
        return false;
    }

    mBlendParameters[AxisIndex] = Parameter;

    return true;
}

const std::vector<FBlendSample>& UBlendSpace::GetSamples() const {
    return mSamples;
}

int32 UBlendSpace::AddSample(const UAnimSequenceBase* Animation, const FVector& SampleValue) {
    if (mSamples.size() >= MaxSamples || !IsFiniteVector(SampleValue)) {
        return -1;
    }

    mSamples.push_back({Animation, SampleValue});
    RebuildSampleOrder();

    return static_cast<int32>(mSamples.size() - 1);
}

bool UBlendSpace::RemoveSample(int32 Index) {
    if (Index < 0 || static_cast<std::size_t>(Index) >= mSamples.size()) {
        return false;
    }

    mSamples.erase(mSamples.begin() + Index);
    RebuildSampleOrder();

    return true;
}

bool UBlendSpace::SetSampleAnimation(int32 Index, const UAnimSequenceBase* Animation) {
    if (Index < 0 || static_cast<std::size_t>(Index) >= mSamples.size()) {
        return false;
    }

    mSamples[static_cast<std::size_t>(Index)].mAnimation = Animation;

    return true;
}

bool UBlendSpace::SetSampleValue(int32 Index, const FVector& SampleValue) {
    if (Index < 0 || static_cast<std::size_t>(Index) >= mSamples.size() || !IsFiniteVector(SampleValue)) {
        return false;
    }

    mSamples[static_cast<std::size_t>(Index)].mSampleValue = SampleValue;
    RebuildSampleOrder();

    return true;
}

FVector UBlendSpace::ClampInput(const FVector& Input) const {
    std::array<float, 3> Values{Input.X, Input.Y, Input.Z};

    for (std::size_t AxisIndex{}; AxisIndex < Values.size(); ++AxisIndex) {
        const float Value{std::isfinite(Values[AxisIndex]) ? Values[AxisIndex] : 0.0f};

        Values[AxisIndex] = AxisIndex < GetNumAxes() ? std::clamp(Value, mBlendParameters[AxisIndex].mMin, mBlendParameters[AxisIndex].mMax) : 0.0f;
    }

    return {Values[0], Values[1], Values[2]};
}

double UBlendSpace::GetBlendedPlayLength(const std::vector<FBlendSampleWeight>& Weights) const {
    double Length{};

    for (const FBlendSampleWeight& Weight : Weights) {
        if (Weight.mSampleIndex >= 0 && static_cast<std::size_t>(Weight.mSampleIndex) < mSamples.size()) {
            if (const UAnimSequenceBase* Animation{mSamples[static_cast<std::size_t>(Weight.mSampleIndex)].mAnimation}) {
                Length += Animation->GetPlayLength() * static_cast<double>(Weight.mWeight);
            }
        }
    }

    return Length;
}

float UBlendSpace::GetPlayLength() const {
    double Length{};

    for (const FBlendSample& Sample : mSamples) {
        if (Sample.mAnimation != nullptr) {
            Length = std::max(Length, static_cast<double>(Sample.mAnimation->GetPlayLength()));
        }
    }

    return static_cast<float>(Length);
}

bool UBlendSpace::IsValid() const {
    return std::any_of(mSamples.begin(), mSamples.end(), [](const FBlendSample& Sample) {
        return Sample.mAnimation != nullptr && Sample.mAnimation->GetSkeleton() != nullptr;
    });
}

void UBlendSpace::CopyFrom(const UBlendSpace& Source) {
    if (this != &Source) {
        auto Samples{Source.mSamples};

        std::copy_n(Source.mBlendParameters, std::size(mBlendParameters), mBlendParameters);
        mSamples = std::move(Samples);
        RebuildSampleOrder();
    }
}

void UBlendSpace::Serialize(FAnimAssetArchive& Archive) {
    uint32 Version{BlendSpaceFormatVersion};

    Archive.Serialize("FormatVersion", Version);

    if (Archive.HasError()) {
        return;
    }

    if (Version != BlendSpaceFormatVersion) {
        Archive.SetError("Unsupported blend space version");

        return;
    }

    uint32 Count{GetNumAxes()};

    if (!Archive.BeginArray("Axes", Count)) {
        return;
    }

    if (Archive.IsLoading() && Count != GetNumAxes()) {
        Archive.SetError("Blend space axis count does not match its type");
        Archive.EndArray();

        return;
    }

    for (uint32 AxisIndex{}; AxisIndex < Count && !Archive.HasError(); ++AxisIndex) {
        if (!Archive.BeginObject(std::to_string(AxisIndex))) {
            break;
        }

        FBlendParameter Parameter{mBlendParameters[AxisIndex]};

        Archive.Serialize("Name", Parameter.mDisplayName);
        Archive.Serialize("Min", Parameter.mMin);
        Archive.Serialize("Max", Parameter.mMax);

        if (!Archive.IsLoading() || Archive.HasField("Divisions")) {
            Archive.Serialize("Divisions", Parameter.mGridDivisions);
        }

        if (Archive.IsLoading() && !Archive.HasError()) {
            if (!IsValidParameter(Parameter)) {
                Archive.SetError("Invalid blend space axis range");
            } else {
                mBlendParameters[AxisIndex] = Parameter;
            }
        }

        Archive.EndObject();
    }

    Archive.EndArray();

    if (Archive.HasError()) {
        return;
    }

    Count = static_cast<uint32>(mSamples.size());

    if (!Archive.BeginArray("Samples", Count)) {
        return;
    }

    std::vector<FBlendSample> LoadedSamples{};

    if (Archive.IsLoading()) {
        if (Count > MaxSamples) {
            Archive.SetError("Too many blend space samples");
            Archive.EndArray();

            return;
        }

        LoadedSamples.resize(Count);
    }

    auto& Samples{Archive.IsLoading() ? LoadedSamples : mSamples};

    for (uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        FBlendSample& Sample{Samples[Index]};

        FString AnimationPath{Sample.mAnimation != nullptr ? Sample.mAnimation->GetPath() : FString{}};
        Archive.Serialize("Animation", AnimationPath);
        Archive.Serialize("X", Sample.mSampleValue.X);
        Archive.Serialize("Y", Sample.mSampleValue.Y);
        Archive.Serialize("Z", Sample.mSampleValue.Z);

        if (Archive.IsLoading() && !Archive.HasError()) {
            if (!IsFiniteVector(Sample.mSampleValue)) {
                Archive.SetError("Invalid blend space sample value");
            } else if (!AnimationPath.empty()) {
                Sample.mAnimation = Cast<UAnimSequenceBase>(UAssetManager::FindLoadedAsset(AnimationPath));

                if (Sample.mAnimation == nullptr && UAssetManager::GetUnloadedAssetType(AnimationPath) == EAssetType::AnimSequence) {
                    Sample.mAnimation = UAssetManager::GetAssetByPath<UAnimSequenceBase>(AnimationPath);
                }
            }
        }

        Archive.EndObject();
    }

    Archive.EndArray();

    if (Archive.IsLoading() && !Archive.HasError()) {
        mSamples = std::move(LoadedSamples);
        RebuildSampleOrder();
    }
}
