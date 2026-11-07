#pragma once

struct FBoneAnimationTrack
{
	FString BoneName;
	TArray<FVector> Positions;
	TArray<FQuat> Rotations;
	TArray<FVector> Scales;
};

struct FAnimationClipData
{
	FString Name;
	// 이 클립을 만든 뼈대의 스켈레톤 파일입니다. 예전 클립(Version 1)은 비어 있습니다.
	FString SkeletonPath;
	float Duration = 0.0f;
	float SampleRate = 30.0f;
	int32 NumFrames = 0;
	TArray<FBoneAnimationTrack> Tracks;

	// 잘못된 데이터를 로드 시점에 걸러낸다
	bool Validate(FString& OutError) const;
};
