#pragma once

#include "Animation/BlendSpace.h"

#include <array>

#include "BlendSpace1D.generated.h"

// X축에서 입력을 둘러싼 두 유효 샘플을 찾아 거리 비율로 선형 블렌딩한다.
UCLASS()
class UBlendSpace1D final : public UBlendSpace {
    GENERATED_BODY()

public:
    // 한 개의 축으로 샘플을 검색하는 Blend Space 에셋을 생성한다.
    UBlendSpace1D() = default;

    // 1차원 샘플 검색 버퍼와 기반 에셋을 소멸한다.
    ~UBlendSpace1D() override = default;

public:
    // 입력 X를 둘러싼 유효 샘플을 선택하고 거리 비율로 가중치를 계산한다.
    void GetSamplesToBlend(const FVector& Input, std::vector<FBlendSampleWeight>& OutWeights) const override;

    // 이 타입이 X축 하나를 사용함을 반환한다.
    uint32 GetNumAxes() const override;

private:
    // 원본 샘플 인덱스를 X좌표와 인덱스 순으로 정렬한다.
    void RebuildSampleOrder() override;

private:
    // 원본 샘플 인덱스를 유지하면서 좌표순 검색만 가능하도록 별도 순서를 보관한다.
    std::array<int32, MaxSamples> mSortedSampleIndices{};
};
