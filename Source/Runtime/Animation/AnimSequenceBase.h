#pragma once

#include "Animation/AnimationAsset.h"
#include "Animation/AnimNotifyTypes.h"
#include "Math/Quat.h"
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
	virtual void GetAnimationPose(FAnimationPoseData& OutPoseData, const FAnimExtractContext& ExtractionContext) const = 0;
	virtual FVector GetRootLocationAtTime(double Time) const { return FVector(0, 0, 0); }
	virtual FQuat GetRootRotationAtTime(double Time) const { return FQuat::Identity(); }

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

	// Notify는 애니메이션 에셋 옆의 사이드카 파일에 저장합니다. .fbxbin은 FBX를 다시 임포트하면 새로 써지므로 따로 둡니다.
	//   Assets/Imported/X/AS_Run.fbxbin  →  Assets/Imported/X/AS_Run.notifies.json
	// AssetPath는 애셋 키("Imported/X/AS_Run.fbxbin")나 파일 경로입니다. 해석할 수 없으면 빈 문자열입니다.
	static FString GetNotifiesFilePath(const FString& AssetPath);
	// 사이드카 파일을 읽어 Notifies를 바꿉니다. 파일이 없으면 Notifies를 비우고 true입니다.
	// 형식이 틀리면 Notifies를 건드리지 않고 false입니다. 잘못된 항목 하나는 건너뛰고 OutError에 남깁니다.
	bool LoadNotifiesFromFile(const FString& AssetPath, FString& OutError);
	// Notifies를 사이드카 파일에 씁니다. Notify가 하나도 없으면 파일을 지웁니다.
	bool SaveNotifiesToFile(const FString& AssetPath, FString& OutError) const;

protected:
	void SetDataModel(UAnimDataModel* InDataModel) { DataModel = InDataModel; }

private:
	friend class UAnimDataModel;
	UAnimDataModel* DataModel = nullptr;
};
