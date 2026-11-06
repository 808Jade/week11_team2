#include "EnginePCH.h"
#include "AnimationClipData.h"

bool FAnimationClipData::Validate(FString& OutError) const
{
	if (NumFrames < 1 || !std::isfinite(SampleRate) || SampleRate <= 0.0f || !std::isfinite(Duration) || Duration < 0.0f)
	{
		OutError = "AnimationClipData: invalid timing: " + Name;
		return false;
	}
	if (Tracks.IsEmpty())
	{
		OutError = "AnimationClipData: no tracks: " + Name;
		return false;
	}

	const auto IsFiniteVector = [](const FVector& V)
	{
		return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
	};

	for (const FBoneAnimationTrack& Track : Tracks)
	{
		if (Track.Positions.Num() != NumFrames || Track.Rotations.Num() != NumFrames || Track.Scales.Num() != NumFrames)
		{
			OutError = "AnimationClipData: frame count mismatch: " + Track.BoneName;
			return false;
		}
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			const FQuat& Q = Track.Rotations[Frame];
			if (!IsFiniteVector(Track.Positions[Frame]) || !IsFiniteVector(Track.Scales[Frame])
				|| !std::isfinite(Q.X) || !std::isfinite(Q.Y) || !std::isfinite(Q.Z) || !std::isfinite(Q.W))
			{
				OutError = "AnimationClipData: non-finite value: " + Track.BoneName;
				return false;
			}
		}
	}
	return true;
}