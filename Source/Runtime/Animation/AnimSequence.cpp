#include "EnginePCH.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Containers/Set.h"

#include <cmath>
#include <limits>
#include <utility>

namespace {
	bool IsFiniteVector(const FVector& Value) {
		return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
	}

	bool HasValidKeyCount(int32 Count, int32 NumSamples) {
		return Count == 0 || Count == 1 || Count == NumSamples;
	}
}

void UAnimSequence::Build(float InSampleRate, int32 InNumSamples, USkeleton* InSkeleton, TArray<FAnimBoneTrack> InBoneTracks) {
	mSampleRate = InSampleRate;
	mNumSamples = InNumSamples;
	mSkeleton = InSkeleton;
	mBoneTracks = std::move(InBoneTracks);
	++mRevision;
}

bool UAnimSequence::Validate(FString& OutError) const {
	if (!mSkeleton) {
		OutError = "Animation asset: no skeleton assigned";
		return false;
	}

	return ValidateData(mSampleRate, mNumSamples, mSkeleton->GetRefSkeleton(), mBoneTracks, OutError);
}

bool UAnimSequence::ValidateData(float SampleRate, int32 NumSamples, const TArray<FBoneInfo>& RefSkeleton, const TArray<FAnimBoneTrack>& BoneTracks, FString& OutError) {
	OutError.clear();
	if (!std::isfinite(SampleRate) || SampleRate <= 0.0f || NumSamples <= 0 || static_cast<double>(NumSamples - 1) / SampleRate > std::numeric_limits<float>::max() || RefSkeleton.IsEmpty() || BoneTracks.IsEmpty()) {
		OutError = "Animation asset: invalid sampling data or empty skeleton/tracks";
		return false;
	}

	TSet<FString> Names{};
	for (int32 Index{}; Index < RefSkeleton.Num(); ++Index) {
		const FBoneInfo& Bone{ RefSkeleton[Index] };
		const FTransform& Bind{ Bone.LocalBindPose };
		if (Bone.Name.empty() || Bone.Name.find('\0') != FString::npos || Names.Contains(Bone.Name) || Bone.ParentIndex < -1 || Bone.ParentIndex >= Index || (Index > 0 && Bone.ParentIndex < 0) || !IsFiniteVector(Bind.Location) || !IsFiniteVector(Bind.Scale) || !std::isfinite(Bind.Rotation.Pitch) || !std::isfinite(Bind.Rotation.Yaw) || !std::isfinite(Bind.Rotation.Roll) || Bind.Scale.X == 0.0f || Bind.Scale.Y == 0.0f || Bind.Scale.Z == 0.0f) {
			OutError = "Animation asset: invalid reference bone: " + Bone.Name;
			return false;
		}

		for (int Row{}; Row < 4; ++Row) {
			for (int Column{}; Column < 4; ++Column) {
				if (!std::isfinite(Bone.InverseBindPose.M[Row][Column])) {
					OutError = "Animation asset: invalid inverse bind pose: " + Bone.Name;
					return false;
				}
			}
		}

		Names.Add(Bone.Name);
	}

	TSet<int32> TrackedBones{};
	for (const FAnimBoneTrack& Track : BoneTracks) {
		const int32 Index{ Track.GetBoneIndex() };
		if (Index < 0 || Index >= RefSkeleton.Num() || TrackedBones.Contains(Index) || (Track.GetPositionKeys().IsEmpty() && Track.GetRotationKeys().IsEmpty() && Track.GetScaleKeys().IsEmpty()) || !HasValidKeyCount(Track.GetPositionKeys().Num(), NumSamples) || !HasValidKeyCount(Track.GetRotationKeys().Num(), NumSamples) || !HasValidKeyCount(Track.GetScaleKeys().Num(), NumSamples)) {
			OutError = "Animation asset: invalid or duplicate bone track";
			return false;
		}

		for (const FVector& Key : Track.GetPositionKeys()) {
			if (!IsFiniteVector(Key)) {
				OutError = "Animation asset: invalid position key";
				return false;
			}
		}

		for (const FVector& Key : Track.GetScaleKeys()) {
			if (!IsFiniteVector(Key)) {
				OutError = "Animation asset: invalid scale key";
				return false;
			}
		}

		for (const FQuat& Key : Track.GetRotationKeys()) {
			const double LengthSquared{ static_cast<double>(Key.X) * Key.X + static_cast<double>(Key.Y) * Key.Y + static_cast<double>(Key.Z) * Key.Z + static_cast<double>(Key.W) * Key.W };
			if (!std::isfinite(LengthSquared) || std::abs(LengthSquared - 1.0) > 1e-3) {
				OutError = "Animation asset: invalid rotation key";
				return false;
			}
		}

		TrackedBones.Add(Index);
	}

	return true;
}

void UAnimSequence::SetPath(const FString& Path) {
	mPath = Path;
}

const FString& UAnimSequence::GetPath() const {
	return mPath;
}

uint64 UAnimSequence::GetRevision() const {
	return mRevision;
}

float UAnimSequence::GetSampleRate() const {
	return mSampleRate;
}

int32 UAnimSequence::GetNumSamples() const {
	return mNumSamples;
}

float UAnimSequence::GetDuration() const {
	return mNumSamples > 1 ? static_cast<float>((static_cast<double>(mNumSamples) - 1.0) / static_cast<double>(mSampleRate)) : 0.0f;
}

USkeleton* UAnimSequence::GetSkeleton() const {
	return mSkeleton;
}

const TArray<FAnimBoneTrack>& UAnimSequence::GetBoneTracks() const {
	return mBoneTracks;
}
