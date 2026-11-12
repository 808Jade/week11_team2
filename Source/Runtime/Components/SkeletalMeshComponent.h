#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNotifyTypes.h"
#include "Core/Delegate/Delegate.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimationAsset;
class UAnimSingleNodeInstance;
class USkeletalMeshComponent;
struct FRootMotionDelta;

DECLARE_DELEGATE(FAnimNotifySignature, USkeletalMeshComponent*, const FAnimNotifyEvent&, EAnimNotifyEventType);

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent {
	GENERATED_BODY()

public:
	using Super::Serialize;

public:
	// 컴포넌트가 소유한 현재 애니메이션 인스턴스를 해제한다.
	~USkeletalMeshComponent() override;

public:
	// 컴포넌트 등록 이후 저장된 그래프 경로로 실행 인스턴스를 복원한다.
	void OnRegister() override;
    void BeginPlay() override;
	// 재생 상태와 본 로컬 포즈를 평가한 뒤 상위 스키닝 갱신을 수행한다.
	void TickComponent(float DeltaTime) override;
	// 단일 시퀀스 재생 인스턴스를 준비해 재생하고 초기 포즈를 즉시 반영한다.
	bool PlayAnimation(UAnimationAsset* NewAnimToPlay, bool Looping, float PlayRate, FString& OutError, float BlendTime = 0.0f, float StartTime = 0.0f);
	// 그래프 경로는 직렬화하고, 실행 인스턴스는 메시마다 별도로 생성한다.
	bool SetAnimGraph(const FString& Path, FString& OutError);
	// 컴포넌트에 저장한 그래프 에셋 경로를 반환한다.
	const FString& GetAnimGraph() const;
	// 현재 컴포넌트가 소유한 애니메이션 인스턴스의 비소유 포인터를 반환한다.
	UAnimInstance* GetAnimInstance() const;
    UAnimSingleNodeInstance* GetSingleNodeInstance() const;
    void SetRootMotionEnabled(bool Enabled, bool ApplyToActor = true);
    FVector ConsumeRootMotionTranslation();
	// Lua와 디버그 UI에서 인스턴스 변수 및 이름이 유일한 재생 노드를 제어하는 진입점이다.
	bool SetAnimGraphFloat(const FString& Name, float Value);
	// 현재 그래프 인스턴스에서 지정 Bool 변수의 값을 변경한다.
	bool SetAnimGraphBool(const FString& Name, bool Value);
	// 이름이 유일한 그래프 플레이어를 찾아 재생 상태를 다시 초기화한다.
	bool RestartAnimGraphPlayer(const FString& Name);
	// 이름이 유일한 그래프 플레이어가 재생을 완료했는지 확인한다.
	bool IsAnimGraphPlayerFinished(const FString& Name) const;

	// 현재 메시 스켈레톤의 본 정보 목록을 반환한다.
	const TArray<FBoneInfo>& GetBones() const;
	// 본 이름에 대응하는 스켈레톤 인덱스를 찾는다.
	int32 FindBoneIndex(const FString& Name) const;
	// 지정 본의 현재 로컬 변환을 반환하며 잘못된 인덱스는 기본 변환을 반환한다.
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	// 지정 본의 로컬 변환을 바꾸고 파생 본 행렬을 갱신 대상으로 표시한다.
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	// 현재 계산된 본의 컴포넌트 공간 행렬을 반환하며 잘못된 인덱스는 항등 행렬을 반환한다.
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	// 본의 컴포넌트 공간 행렬과 메시 월드 변환을 합성한다.
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;
	// 메시 변경 경로를 통해 재생 인스턴스와 본 포즈를 재설정하고 파생 데이터를 갱신 대상으로 표시한다.
	void ResetPose();
	// Tick을 기다리지 않고 본 행렬 및 메시 스키닝 데이터를 즉시 갱신한다.
	void RefreshBoneTransformsNow();

	// 그래프 경로와 이름별 본 포즈를 직렬화하고 로드 후 실행 인스턴스를 복원한다.
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

public:
    FAnimNotifySignature OnAnimNotify{};

protected:
	// 부모 본 행렬을 순서대로 합성해 로컬 포즈를 컴포넌트 공간으로 변환한다.
	void RefreshBoneTransforms() override;
	// 기존 재생기를 해제하고 새 메시의 바인드 포즈 및 그래프 실행 상태를 준비한다.
	void OnSkeletalMeshChanged() override;

private:
	friend class UAnimInstance;
	// 메시와 저장 경로가 준비된 경우에만 아직 없는 그래프 실행 인스턴스를 생성한다.
	void RestoreAnimGraph();
    void ApplyRootMotion(const FRootMotionDelta& Delta);
    void DispatchAnimNotifies();
    void ApplyAnimationBlend(float DeltaTime);

private:
	// 단일 시퀀스 또는 그래프 재생을 수행하며 이 컴포넌트가 소유하는 인스턴스이다.
    UPROPERTY(EditAnywhere)
    UAnimationAsset* AnimToPlay{};

    UPROPERTY(EditAnywhere)
    bool bLoopAnimation{true};

    UPROPERTY(EditAnywhere, meta=(ClampMin=0.01, UIMin=0.01, UIMax=5, Delta=0.01))
    float AnimPlayRate{1.0f};

    UPROPERTY(EditAnywhere)
    bool bEnableRootMotion{};

    UPROPERTY(EditAnywhere)
    bool mApplyRootMotionToActor{true};

	UAnimInstance* mAnimScriptInstance{};
	// 현재 메시의 본 인덱스 순서로 보관하는 로컬 포즈 배열이다.
	TArray<FTransform> mBoneSpaceTransforms{};
	// 씬 저장 및 메시 변경 후 실행 복원에 사용할 그래프 에셋 경로이다.
	FString mAnimGraphPath{};

    TArray<FTransform> mBlendSourcePose{};
    float mAnimationBlendTime{};
    float mAnimationBlendElapsed{};
    FVector mPendingRootMotionTranslation{};

};
