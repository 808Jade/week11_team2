#pragma once

#include "Animation/AnimationAsset.h"
#include "Animation/AnimNotifyTypes.h"
#include "AnimSequenceBase.generated.h"

class UAnimDataModel;
struct FAnimationPoseData;

UCLASS()
class UAnimSequenceBase : public UAnimationAsset
{
	GENERATED_BODY()

public:
	~UAnimSequenceBase() override;
	const UAnimDataModel* GetDataModel() const { return DataModel; }
	float GetPlayLength() const override;
	virtual void GetAnimationPose(FAnimationPoseData& OutPoseData,
		const FAnimExtractContext& ExtractionContext) const = 0;

	////////////////////////////////////////////////////////////
	// AnimNotify
	////////////////////////////////////////////////////////////

	// 재생으로 From → To 구간을 지나갔을 때 걸리는 Notify를 OutNotifies에 넣습니다. (UE GetAnimNotifiesFromDeltaPositions)
	// From > To면 역재생입니다. 진행 방향의 시작 쪽(From)은 열린 끝이라 경계의 Notify가 두 번 발동하지 않습니다.
	//   정방향 (From, To], 역방향 [To, From)
	// bIncludeFrom이면 From도 포함합니다. (재생 시작이나 루프로 감싸져 경계에서 새로 출발하는 경우)
	// NotifyState는 구간이 지나간 범위와 겹치면 넣습니다. Begin/Tick/End 판정은 UAnimInstance가 합니다.
	void GetAnimNotifiesFromDeltaPositions(double From, double To, bool bIncludeFrom,
		TArray<const FAnimNotifyEvent*>& OutNotifies) const;

	// 애니메이션에 붙은 Notify 목록. (UE와 같이 public. 에디터 Timeline이 직접 편집합니다)
	TArray<FAnimNotifyEvent> Notifies;

protected:
	void SetDataModel(UAnimDataModel* InDataModel) { DataModel = InDataModel; }

private:
	friend class UAnimDataModel;
	UAnimDataModel* DataModel = nullptr;
};
