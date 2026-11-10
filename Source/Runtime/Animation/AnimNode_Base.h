#pragma once

#include "Animation/BoneContainer.h"
#include "Math/Quat.h"

struct FAnimationUpdateContext
{
	explicit FAnimationUpdateContext(float InDeltaTime) : DeltaTime(InDeltaTime) {}
	float GetDeltaTime() const { return DeltaTime; }

private:
	float DeltaTime;
};

struct FPoseContext
{
	explicit FPoseContext(const FBoneContainer& InRequiredBones)
		: RequiredBones(InRequiredBones) {}

	void ResetToRefPose() { RequiredBones.FillWithRefPose(Pose); }

	const FBoneContainer& RequiredBones;
	TArray<FTransform> Pose;
};

// 한 프레임 동안의 root 이동량. 둘 다 컴포넌트 공간 기준이다.
struct FRootMotionDelta
{
	FVector Translation = FVector(0, 0, 0);
	FQuat Rotation = FQuat::Identity();
};

struct FAnimNode_Base
{
	virtual ~FAnimNode_Base() = default;
	virtual void Update(const FAnimationUpdateContext& Context) = 0;
	virtual void Evaluate(FPoseContext& Output) = 0;
	// 이번 Update에서 추출한 root 이동량. root 이동이 없는 노드는 이동 0, 단위 회전을 돌려준다.
	virtual FRootMotionDelta GetRootMotionDelta() const { return {}; }
};

struct FPoseLink
{
	void SetLinkNode(FAnimNode_Base* NewLinkNode) { LinkedNode = NewLinkNode; }
	FAnimNode_Base* GetLinkNode() const { return LinkedNode; }

	void Update(const FAnimationUpdateContext& Context) const
	{
		if (LinkedNode)
			LinkedNode->Update(Context);
	}

	void Evaluate(FPoseContext& Output) const
	{
		if (LinkedNode)
			LinkedNode->Evaluate(Output);
		else
			Output.ResetToRefPose();
	}

private:
	FAnimNode_Base* LinkedNode = nullptr; // Non-owning. The animation instance owns the nodes.
};
