#include "EnginePCH.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimSequenceBase.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <numeric>
#include <span>
#include <utility>

void UBlendSpace1D::GetSamplesToBlend(const FVector& Input, std::vector<FBlendSampleWeight>& OutWeights) const {
    OutWeights.clear();

    const auto& Samples{GetSamples()};
    const std::span<const int32> Sorted{mSortedSampleIndices.data(), Samples.size()};
    const auto IsUsable{[&Samples](int32 Index) {
        const UAnimSequenceBase* Animation{Samples[static_cast<std::size_t>(Index)].mAnimation};

        return Animation != nullptr && Animation->GetSkeleton() != nullptr;
    }};

    const auto First{std::ranges::find_if(Sorted, IsUsable)};

    if (First == Sorted.end()) {
        return;
    }

    const auto SampleX{[&Samples](int32 Index) {
        return Samples[static_cast<std::size_t>(Index)].mSampleValue.X;
    }};

    const float X{ClampInput(Input).X};

    if (X <= SampleX(*First)) {
        OutWeights.push_back({*First, 1.0f});

        return;
    }

    const auto Last{std::find_if(Sorted.rbegin(), Sorted.rend(), IsUsable)};

    if (X >= SampleX(*Last)) {
        OutWeights.push_back({*Last, 1.0f});

        return;
    }

    const auto Split{std::ranges::lower_bound(First, Last.base(), X, std::ranges::less{}, SampleX)};
    const int32 Low{*std::find_if(std::make_reverse_iterator(Split), Sorted.rend(), IsUsable)};
    const int32 High{*std::find_if(Split, Last.base(), IsUsable)};
    const float LowX{SampleX(Low)};
    const float Range{SampleX(High) - LowX};
    const float Alpha{Range > 0.0f ? std::clamp((X - LowX) / Range, 0.0f, 1.0f) : 0.0f};

    if (Alpha < 1.0f) {
        OutWeights.push_back({Low, 1.0f - Alpha});
    }

    if (Alpha > 0.0f) {
        OutWeights.push_back({High, Alpha});
    }
}

uint32 UBlendSpace1D::GetNumAxes() const {
    return 1;
}

void UBlendSpace1D::RebuildSampleOrder() {
    const auto& Samples{GetSamples()};
    const std::span<int32> Sorted{mSortedSampleIndices.data(), Samples.size()};

    std::iota(Sorted.begin(), Sorted.end(), int32{});
    std::ranges::sort(Sorted, std::ranges::less{}, [&Samples](int32 Index) {
        return std::pair{Samples[static_cast<std::size_t>(Index)].mSampleValue.X, Index};
    });
}
