#pragma once

#include "Animation/BoneContainer.h"

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

struct FAnimNode_Base
{
	virtual ~FAnimNode_Base() = default;
	virtual void Update(const FAnimationUpdateContext& Context) = 0;
	virtual void Evaluate(FPoseContext& Output) = 0;
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
