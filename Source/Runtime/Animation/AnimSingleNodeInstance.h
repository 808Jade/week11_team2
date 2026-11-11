#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimSingleNodeInstance.generated.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

class UAnimationAsset;

// 기존 단일 애니메이션 재생도 공통 포즈 평가 경로에 Sequence Player 하나를 연결해 처리한다.
UCLASS()
class UAnimSingleNodeInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// 메시와 스켈레톤을 공유하는 시퀀스를 단일 플레이어에 지정한다.
	bool SetAnimationAsset(UAnimationAsset* NewAsset, bool Looping, float PlayRate, FString& OutError);
	// 현재 단일 재생기에 지정한 애니메이션 에셋을 반환한다.
	UAnimationAsset* GetAnimationAsset() const;
	// 단일 시퀀스의 재생 시각을 0으로 되돌린다.
	void PlayAnim();
    void SetPlaying(bool Playing);
    bool IsPlaying() const;
    void SetPosition(float Position);
    float GetCurrentTime() const;
    float GetPreviousTime() const;
    float GetLength() const;
    bool SetPlayRate(float PlayRate);
    float GetPlayRate() const;
    void SetLooping(bool Looping);
    bool IsLooping() const;
    bool DidWrapThisUpdate() const;

protected:
	// 멤버 Sequence Player를 공통 포즈 평가의 루트로 지정한다.
	void NativeInitializeAnimation() override;

private:
	// 현재 재생 설정에 사용한 공유 애니메이션 에셋의 비소유 포인터이다.
	UAnimationAsset* mCurrentAsset{};
	// 이 인스턴스가 직접 소유하는 단일 시퀀스 실행 노드이다.
	FAnimNode_SequencePlayer mSingleNode{};
};
