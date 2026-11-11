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

// 입력 축의 표시 범위와 에디터 격자 간격을 정의한다.
struct FBlendParameter {
public:

    // 에디터에서 축에 표시할 입력 이름이다.
    FString mDisplayName{"Speed"};

    // 허용할 축 입력과 에디터 표시 범위의 최솟값이다.
    float mMin{0.0f};

    // 허용할 축 입력과 에디터 표시 범위의 최댓값이다.
    float mMax{5.0f};

    // 축 구간을 나눌 격자 수이며 샘플 스냅 간격에도 사용한다.
    int32 mGridDivisions{4};
};

// 입력 좌표에 배치한 시퀀스이며, 재생 시각은 개별 플레이어가 관리한다.
struct FBlendSample {
public:

    // 이 좌표에서 재생할 공유 시퀀스 에셋의 비소유 포인터이다.
    const UAnimSequenceBase* mAnimation{};

    // 입력 공간에서 이 샘플이 배치된 좌표이다.
    FVector mSampleValue{};
};

struct FBlendSampleWeight {
public:

    // Blend Space의 원본 샘플 배열에서 사용할 항목의 인덱스이다.
    int32 mSampleIndex{-1};

    // 선택된 샘플 포즈가 혼합 결과에 기여할 비율이다.
    float mWeight{};
};

// 공유 에셋에는 축과 샘플 배치만 저장하고, 입력에 따른 샘플 가중치를 계산한다.
UCLASS()
class UBlendSpace : public UAnimationAsset {
    GENERATED_BODY()

public:

    // 샘플 저장과 정렬 버퍼에 허용하는 최대 샘플 개수이다.
    static constexpr uint32 MaxSamples{64};

public:
    // 축 기본값과 빈 샘플 목록을 가진 Blend Space 기반 에셋을 생성한다.
    UBlendSpace() = default;

    // 기반 에셋 포인터로 파생 Blend Space를 안전하게 소멸한다.
    ~UBlendSpace() override = default;

public:

    // 입력 좌표에서 사용할 샘플과 가중치를 파생 차원별 알고리즘으로 계산한다.
    virtual void GetSamplesToBlend(const FVector& Input, std::vector<FBlendSampleWeight>& OutWeights) const = 0;
    // 파생 Blend Space가 실제 사용하는 입력 축의 개수를 반환한다.
    virtual uint32 GetNumAxes() const = 0;

    // 원본 인덱스 순서의 샘플 목록을 읽기 전용으로 반환한다.
    const std::vector<FBlendSample>& GetSamples() const;

    // 시퀀스와 좌표를 추가하고 검색 순서를 갱신하며 실패 시 -1을 반환한다.
    int32 AddSample(const UAnimSequenceBase* Animation, const FVector& SampleValue);
    // 인덱스의 샘플을 제거하고 남은 샘플의 검색 순서를 갱신한다.
    bool RemoveSample(int32 Index);

    // 유효한 축 범위 및 격자 설정을 검사한 뒤 해당 축에 적용한다.
    bool SetBlendParameter(EBlendSpaceAxis Axis, const FBlendParameter& Parameter);
    // 기존 샘플의 좌표를 유지한 채 재생 시퀀스를 교체한다.
    bool SetSampleAnimation(int32 Index, const UAnimSequenceBase* Animation);
    // 샘플의 좌표를 바꾸고 입력 검색에 사용하는 순서를 재구성한다.
    bool SetSampleValue(int32 Index, const FVector& SampleValue);

    // 축의 이름, 입력 범위와 격자 설정을 반환한다.
    const FBlendParameter& GetBlendParameter(EBlendSpaceAxis Axis) const;
    // 선택한 샘플의 재생 길이를 가중 평균해 공통 재생 주기를 계산한다.
    double GetBlendedPlayLength(const std::vector<FBlendSampleWeight>& Weights) const;
    // 등록된 시퀀스 중 가장 긴 재생 길이를 에셋의 대표 길이로 반환한다.
    float GetPlayLength() const override;

    // 유효한 스켈레톤을 가진 시퀀스 샘플이 하나 이상 존재하는지 확인한다.
    bool IsValid() const;

    // 축 설정과 샘플 배치를 복사하고 파생 검색 순서를 다시 구성한다.
    void CopyFrom(const UBlendSpace& Source);
    // 축 및 시퀀스 경로와 샘플 좌표를 검증하며 저장하거나 불러온다.
    void Serialize(FAnimAssetArchive& Archive);

protected:
    // 유효 축 입력을 설정 범위로 제한하고 사용하지 않는 축은 0으로 만든다.
    FVector ClampInput(const FVector& Input) const;

private:
    // 샘플이 추가, 삭제 또는 이동했을 때 파생 타입의 검색 순서를 갱신한다.
    virtual void RebuildSampleOrder() = 0;

private:
    // 가능한 X, Y, Z축의 설정이며 실제 사용 축 수는 파생 타입이 결정한다.
    FBlendParameter mBlendParameters[static_cast<uint8>(EBlendSpaceAxis::Max)]{};

    // 공유 시퀀스 에셋과 입력 공간 좌표의 원본 샘플 목록이다.
    std::vector<FBlendSample> mSamples{};
};
