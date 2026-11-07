#pragma once

#include "Render/SkeletalMeshData.h"
#include "UObject/Object.h"
#include "AnimInstance.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

UCLASS()
class UAnimInstance : public UObject {
	GENERATED_BODY()
	friend class USkeletalMeshComponent;

public:
	UAnimInstance() = default;
	~UAnimInstance() override;
	UAnimInstance(const UAnimInstance&) = delete;
	UAnimInstance& operator=(const UAnimInstance&) = delete;

public:
	bool Play(UAnimSequence* Sequence, const TArray<FBoneInfo>& RefSkeleton, bool Looping, FString& OutError);
	void Pause();
	void Resume();
	void Stop();

	bool SetPosition(float Position);
	bool SetPlayRate(float PlayRate);
	void SetLooping(bool Looping);
	float GetPosition() const;
	float GetPlayRate() const;
	bool IsPlaying() const;
	bool IsLooping() const;
	UAnimSequence* GetSequence() const;
	USkeletalMeshComponent* GetOwningComponent() const;

	bool Update(float DeltaTime);
	bool EvaluatePose(TArray<FTransform>& OutPose) const;

private:
	void Initialize(USkeletalMeshComponent* Component);
	bool BuildBoneMapping(const UAnimSequence& Sequence, const TArray<FBoneInfo>& RefSkeleton, TArray<int32>& OutMapping, FString& OutError) const;

private:
	USkeletalMeshComponent* mOwningComponent{};
	UAnimSequence* mSequence{};
	uint64 mSequenceRevision{};
	TArray<FBoneInfo> mRefSkeleton{};
	TArray<int32> mTrackToBoneIndices{};
	double mPosition{};
	float mPlayRate{ 1.0f };
	bool mPlaying{};
	bool mLooping{};
	bool mPoseDirty{};
};
