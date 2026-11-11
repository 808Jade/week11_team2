#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimNotifyTypes.h"
#include "Core/Delegate/Delegate.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimationAsset;
class USkeletalMeshComponent;
struct FRootMotionDelta;

// AnimNotify를 받는 델리게이트. Type으로 Notify / StateBegin / StateTick / StateEnd를 구분합니다.
DECLARE_DELEGATE(FAnimNotifySignature, USkeletalMeshComponent*, const FAnimNotifyEvent&, EAnimNotifyEventType);

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
	~USkeletalMeshComponent() override;
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;
	bool PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping, float PlayRate, FString& OutError);

	UAnimInstance* GetAnimInstance() const { return AnimScriptInstance; }
	// PlayAnimation으로 재생 중일 때의 단일 노드 인스턴스. 재생 제어(정지·위치·배속)에 씁니다. 없으면 nullptr. (UE와 같은 이름)
	class UAnimSingleNodeInstance* GetSingleNodeInstance() const;

	// 재생 중 지나간 AnimNotify를 받습니다. 매 Tick의 애니메이션 Update·포즈 계산이 끝난 뒤 호출됩니다.
	// 액터에 Lua 스크립트가 있으면 OnAnimNotify(name) / OnAnimNotifyBegin(name) / OnAnimNotifyEnd(name)도 부릅니다. (Tick은 Lua로 보내지 않음)
	FAnimNotifySignature OnAnimNotify;

	// UE의 Animation Mode = Use Animation Asset. 게임 시작 시 AnimToPlay를 자동 재생한다(스크립트 불필요).
	// 메시와 같은 Skeleton의 애니메이션만 재생된다. Details 패널은 같은 Skeleton의 애니메이션만 보여 준다.
	UPROPERTY(EditAnywhere)
	UAnimationAsset* AnimToPlay = nullptr;

	UPROPERTY(EditAnywhere)
	bool bLoopAnimation = true;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0.01, UIMin=0.01, UIMax=5, Delta=0.01))
	float AnimPlayRate = 1.0f;

	// UE의 Enable Root Motion. 켜면 Skeleton root 본(0번)의 이동·회전을 Pose에서 빼서 Actor에 적용한다.
	// 움직임 전용 root 본이 있는 애니메이션(UAL1_Standard_RM 등)에서만 켠다.
	// 골반이 root인 Skeleton(Mixamo 등)에서 켜면 골반의 흔들림이 Actor 전체로 옮겨 가고, root 트랙이 없는 애니메이션에는 효과가 없다.
	UPROPERTY(EditAnywhere)
	bool bEnableRootMotion = false;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;

	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

	void ResetPose();
	void RefreshBoneTransformsNow();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

protected:
	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

private:
	// 인스턴스가 쌓아 둔 Notify를 델리게이트와 Lua로 보냅니다.
	void DispatchAnimNotifies();

	friend class UAnimInstance;
	UAnimInstance* AnimScriptInstance = nullptr;
	TArray<FTransform> BoneSpaceTransforms;

	void ApplyRootMotion(const FRootMotionDelta& Delta);
};
