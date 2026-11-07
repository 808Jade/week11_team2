#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Map.h"
#include "Containers/Set.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
	bool IsFiniteVector(const FVector& Value) {
		return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
	}

	bool IsValidRotation(const FQuat& Value) {
		const double LengthSquared{ static_cast<double>(Value.X) * Value.X + static_cast<double>(Value.Y) * Value.Y + static_cast<double>(Value.Z) * Value.Z + static_cast<double>(Value.W) * Value.W };
		return std::isfinite(LengthSquared) && std::abs(LengthSquared - 1.0) <= 1e-3;
	}

	bool HasValidKeyCount(int32 Count, int32 NumSamples) {
		return Count == 0 || Count == 1 || Count == NumSamples;
	}

	bool ValidateSkeleton(const TArray<FBoneInfo>& Bones, FString& OutError) {
		if (Bones.IsEmpty()) {
			OutError = "Animation: reference skeleton is empty";
			return false;
		}

		TSet<FString> Names{};
		for (int32 Index{}; Index < Bones.Num(); ++Index) {
			const FBoneInfo& Bone{ Bones[Index] };
			const FTransform& Bind{ Bone.LocalBindPose };
			if (Bone.Name.empty() || Names.Contains(Bone.Name) || Bone.ParentIndex < -1 || Bone.ParentIndex >= Index || (Index > 0 && Bone.ParentIndex < 0)) {
				OutError = "Animation: invalid or duplicate reference bone: " + Bone.Name;
				return false;
			}

			if (!IsFiniteVector(Bind.Location) || !IsFiniteVector(Bind.Scale) || !std::isfinite(Bind.Rotation.Pitch) || !std::isfinite(Bind.Rotation.Yaw) || !std::isfinite(Bind.Rotation.Roll) || Bind.Scale.X == 0.0f || Bind.Scale.Y == 0.0f || Bind.Scale.Z == 0.0f) {
				OutError = "Animation: invalid reference pose: " + Bone.Name;
				return false;
			}

			Names.Add(Bone.Name);
		}

		return true;
	}

	bool HasMatchingBindPose(const FTransform& Source, const FTransform& Target) {
		const FMatrix SourceMatrix{ Source.GetLocalMatrix() };
		const FMatrix TargetMatrix{ Target.GetLocalMatrix() };
		for (int32 Row{}; Row < 4; ++Row) {
			for (int32 Column{}; Column < 4; ++Column) {
				const float Left{ SourceMatrix.M[Row][Column] };
				const float Right{ TargetMatrix.M[Row][Column] };
				if (!std::isfinite(Left) || !std::isfinite(Right) || std::abs(Left - Right) > 1e-4f * std::max({ 1.0f, std::abs(Left), std::abs(Right) })) {
					return false;
				}
			}
		}

		return true;
	}

	FVector SampleVector(const TArray<FVector>& Keys, const FVector& Default, int32 First, int32 Second, float Alpha) {
		if (Keys.IsEmpty()) {
			return Default;
		}

		if (Keys.Num() == 1) {
			return Keys[0];
		}

		return Keys[First] * (1.0f - Alpha) + Keys[Second] * Alpha;
	}

	FQuat InterpolateRotation(const FQuat& First, const FQuat& Second, float Alpha) {
		const FQuat Left{ First.Normalized() };
		FQuat Right{ Second.Normalized() };
		double Dot{ static_cast<double>(Left.X) * Right.X + static_cast<double>(Left.Y) * Right.Y + static_cast<double>(Left.Z) * Right.Z + static_cast<double>(Left.W) * Right.W };
		if (Dot < 0.0) {
			Right = FQuat{ -Right.X, -Right.Y, -Right.Z, -Right.W };
			Dot = -Dot;
		}

		Dot = std::clamp(Dot, 0.0, 1.0);
		double LeftWeight{ 1.0 - Alpha };
		double RightWeight{ Alpha };
		if (Dot < 0.9995) {
			const double Angle{ std::acos(Dot) };
			const double InverseSine{ 1.0 / std::sin(Angle) };
			LeftWeight = std::sin((1.0 - Alpha) * Angle) * InverseSine;
			RightWeight = std::sin(Alpha * Angle) * InverseSine;
		}

		return FQuat{ static_cast<float>(Left.X * LeftWeight + Right.X * RightWeight), static_cast<float>(Left.Y * LeftWeight + Right.Y * RightWeight), static_cast<float>(Left.Z * LeftWeight + Right.Z * RightWeight), static_cast<float>(Left.W * LeftWeight + Right.W * RightWeight) }.Normalized();
	}

	FRotator MakeRotator(const FQuat& Rotation) {
		const double Length{ std::sqrt(static_cast<double>(Rotation.X) * Rotation.X + static_cast<double>(Rotation.Y) * Rotation.Y + static_cast<double>(Rotation.Z) * Rotation.Z + static_cast<double>(Rotation.W) * Rotation.W) };
		const double X{ Rotation.X / Length };
		const double Y{ Rotation.Y / Length };
		const double Z{ Rotation.Z / Length };
		const double W{ Rotation.W / Length };
		const double M00{ 1.0 - 2.0 * (Y * Y + Z * Z) };
		const double M01{ 2.0 * (X * Y + W * Z) };
		const double M02{ 2.0 * (X * Z - W * Y) };
		const double M10{ 2.0 * (X * Y - W * Z) };
		const double M11{ 1.0 - 2.0 * (X * X + Z * Z) };
		const double M12{ 2.0 * (Y * Z + W * X) };
		const double M22{ 1.0 - 2.0 * (X * X + Y * Y) };
		const double Horizontal{ std::sqrt(M00 * M00 + M01 * M01) };
		const double Pitch{ std::atan2(-M02, Horizontal) };
		const double Yaw{ Horizontal > 1e-6 ? std::atan2(M01, M00) : std::atan2(-M10, M11) };
		const double Roll{ Horizontal > 1e-6 ? std::atan2(M12, M22) : 0.0 };
		const double ToDegrees{ 180.0 / std::acos(-1.0) };
		return FRotator{ static_cast<float>(Pitch * ToDegrees), static_cast<float>(Yaw * ToDegrees), static_cast<float>(Roll * ToDegrees) };
	}
}

UAnimInstance::~UAnimInstance() {
	if (mOwningComponent) {
		mOwningComponent->OnAnimInstanceDestroyed(this);
	}
}

bool UAnimInstance::Play(UAnimSequence* Sequence, const TArray<FBoneInfo>& RefSkeleton, bool Looping, FString& OutError) {
	OutError.clear();
	if (!Sequence || !std::isfinite(Sequence->GetSampleRate()) || Sequence->GetSampleRate() <= 0.0f || Sequence->GetNumSamples() <= 0 || !std::isfinite(Sequence->GetDuration())) {
		OutError = "Animation: sequence has invalid sampling data";
		return false;
	}

	TArray<int32> Mapping{};
	if (!BuildBoneMapping(*Sequence, RefSkeleton, Mapping, OutError)) {
		return false;
	}

	mSequence = Sequence;
	mSequenceRevision = Sequence->GetRevision();
	mRefSkeleton = RefSkeleton;
	mTrackToBoneIndices = std::move(Mapping);
	mPosition = mPlayRate < 0.0f ? Sequence->GetDuration() : 0.0;
	mPlaying = Sequence->GetDuration() > 0.0f;
	mLooping = Looping;
	mPoseDirty = true;
	return true;
}

void UAnimInstance::Pause() {
	mPlaying = false;
}

void UAnimInstance::Resume() {
	mPlaying = mSequence && mSequence->GetDuration() > 0.0f;
}

void UAnimInstance::Stop() {
	mSequence = nullptr;
	mSequenceRevision = 0;
	mRefSkeleton.Empty();
	mTrackToBoneIndices.Empty();
	mPosition = 0.0;
	mPlaying = false;
	mLooping = false;
	mPoseDirty = false;
}

bool UAnimInstance::SetPosition(float Position) {
	if (!mSequence || !std::isfinite(Position) || !std::isfinite(mSequence->GetDuration())) {
		return false;
	}

	mPosition = std::clamp(static_cast<double>(Position), 0.0, static_cast<double>(mSequence->GetDuration()));
	mPoseDirty = true;
	return true;
}

bool UAnimInstance::SetPlayRate(float PlayRate) {
	if (!std::isfinite(PlayRate)) {
		return false;
	}

	mPlayRate = PlayRate;
	return true;
}

void UAnimInstance::SetLooping(bool Looping) {
	mLooping = Looping;
}

float UAnimInstance::GetPosition() const {
	return static_cast<float>(mPosition);
}

float UAnimInstance::GetPlayRate() const {
	return mPlayRate;
}

bool UAnimInstance::IsPlaying() const {
	return mPlaying;
}

bool UAnimInstance::IsLooping() const {
	return mLooping;
}

UAnimSequence* UAnimInstance::GetSequence() const {
	return mSequence;
}

USkeletalMeshComponent* UAnimInstance::GetOwningComponent() const {
	return mOwningComponent;
}

bool UAnimInstance::Update(float DeltaTime) {
	if (!mSequence) {
		return false;
	}

	if (mSequenceRevision != mSequence->GetRevision()) {
		FString Error{};
		TArray<int32> Mapping{};
		if (!mSequence->Validate(Error) || !BuildBoneMapping(*mSequence, mRefSkeleton, Mapping, Error)) {
			Stop();
			return true;
		}

		mTrackToBoneIndices = std::move(Mapping);
		mSequenceRevision = mSequence->GetRevision();
		mPosition = std::clamp(mPosition, 0.0, static_cast<double>(mSequence->GetDuration()));
		mPoseDirty = true;
	}

	if (mPlaying && std::isfinite(DeltaTime) && DeltaTime > 0.0f && mPlayRate != 0.0f) {
		const double Duration{ mSequence->GetDuration() };
		const double NextPosition{ mPosition + static_cast<double>(DeltaTime) * mPlayRate };
		double Position{};
		if (Duration <= 0.0 || !std::isfinite(Duration)) {
			mPlaying = false;
		} else if (mLooping) {
			Position = std::fmod(NextPosition, Duration);
			if (Position < 0.0) {
				Position += Duration;
			}
		} else {
			Position = std::clamp(NextPosition, 0.0, Duration);
			if ((mPlayRate > 0.0f && NextPosition >= Duration) || (mPlayRate < 0.0f && NextPosition <= 0.0)) {
				mPlaying = false;
			}
		}

		mPoseDirty = mPoseDirty || Position != mPosition;
		mPosition = Position;
	}

	const bool PoseDirty{ mPoseDirty };
	mPoseDirty = false;
	return PoseDirty;
}

bool UAnimInstance::EvaluatePose(TArray<FTransform>& OutPose) const {
	if (mSequence && mSequenceRevision != mSequence->GetRevision()) {
		return false;
	}

	if (!mSequence || !mSequence->GetSkeleton() || mRefSkeleton.IsEmpty() || mSequence->GetBoneTracks().Num() != mTrackToBoneIndices.Num() || mSequence->GetNumSamples() <= 0 || !std::isfinite(mSequence->GetSampleRate()) || mSequence->GetSampleRate() <= 0.0f || !std::isfinite(mSequence->GetDuration())) {
		return false;
	}

	const TArray<FBoneInfo>& SourceBones{ mSequence->GetSkeleton()->GetRefSkeleton() };
	const int32 NumSamples{ mSequence->GetNumSamples() };
	const double SamplePosition{ mPosition >= mSequence->GetDuration() ? static_cast<double>(NumSamples - 1) : std::clamp(mPosition * mSequence->GetSampleRate(), 0.0, static_cast<double>(NumSamples - 1)) };
	const int32 First{ static_cast<int32>(SamplePosition) };
	const int32 Second{ std::min(First + 1, NumSamples - 1) };
	const float Alpha{ static_cast<float>(SamplePosition - First) };
	TArray<FTransform> Pose{};
	Pose.Reserve(mRefSkeleton.Num());
	for (const FBoneInfo& Bone : mRefSkeleton) {
		Pose.Add(Bone.LocalBindPose);
	}

	for (int32 TrackIndex{}; TrackIndex < mTrackToBoneIndices.Num(); ++TrackIndex) {
		const FAnimBoneTrack& Track{ mSequence->GetBoneTracks()[TrackIndex] };
		const int32 BoneIndex{ mTrackToBoneIndices[TrackIndex] };
		if (BoneIndex < 0 || BoneIndex >= Pose.Num() || Track.GetBoneIndex() < 0 || Track.GetBoneIndex() >= SourceBones.Num() || SourceBones[Track.GetBoneIndex()].Name != mRefSkeleton[BoneIndex].Name || !HasValidKeyCount(Track.GetPositionKeys().Num(), NumSamples) || !HasValidKeyCount(Track.GetRotationKeys().Num(), NumSamples) || !HasValidKeyCount(Track.GetScaleKeys().Num(), NumSamples)) {
			return false;
		}

		FTransform& Transform{ Pose[BoneIndex] };
		Transform.Location = SampleVector(Track.GetPositionKeys(), Transform.Location, First, Second, Alpha);
		Transform.Scale = SampleVector(Track.GetScaleKeys(), Transform.Scale, First, Second, Alpha);
		if (!Track.GetRotationKeys().IsEmpty()) {
			const FQuat& Left{ Track.GetRotationKeys()[Track.GetRotationKeys().Num() == 1 ? 0 : First] };
			const FQuat& Right{ Track.GetRotationKeys()[Track.GetRotationKeys().Num() == 1 ? 0 : Second] };
			if (!IsValidRotation(Left) || !IsValidRotation(Right)) {
				return false;
			}

			Transform.Rotation = MakeRotator(InterpolateRotation(Left, Right, Alpha));
		}

		if (!IsFiniteVector(Transform.Location) || !IsFiniteVector(Transform.Scale)) {
			return false;
		}
	}

	OutPose = std::move(Pose);
	return true;
}

void UAnimInstance::Initialize(USkeletalMeshComponent* Component) {
	mOwningComponent = Component;
}

bool UAnimInstance::BuildBoneMapping(const UAnimSequence& Sequence, const TArray<FBoneInfo>& RefSkeleton, TArray<int32>& OutMapping, FString& OutError) const {
	const USkeleton* Skeleton{ Sequence.GetSkeleton() };
	if (!Skeleton) {
		OutError = "Animation: sequence has no skeleton";
		return false;
	}

	const TArray<FBoneInfo>& SourceBones{ Skeleton->GetRefSkeleton() };
	if (!ValidateSkeleton(SourceBones, OutError) || !ValidateSkeleton(RefSkeleton, OutError)) {
		return false;
	}

	TMap<FString, int32> TargetIndices{};
	for (int32 Index{}; Index < RefSkeleton.Num(); ++Index) {
		TargetIndices.Add(RefSkeleton[Index].Name, Index);
	}

	TArray<int32> BoneMapping{};
	for (const FBoneInfo& Bone : SourceBones) {
		const int32* Target{ TargetIndices.Find(Bone.Name) };
		if (!Target) {
			OutError = "Animation: mesh is missing bone: " + Bone.Name;
			return false;
		}

		BoneMapping.Add(*Target);
	}

	for (int32 Index{}; Index < SourceBones.Num(); ++Index) {
		const FBoneInfo& Source{ SourceBones[Index] };
		const FBoneInfo& Target{ RefSkeleton[BoneMapping[Index]] };
		const int32 Parent{ Source.ParentIndex < 0 ? -1 : BoneMapping[Source.ParentIndex] };
		if (Target.ParentIndex != Parent || !HasMatchingBindPose(Source.LocalBindPose, Target.LocalBindPose)) {
			OutError = "Animation: bone hierarchy or reference pose does not match: " + Source.Name;
			return false;
		}
	}

	if (Sequence.GetBoneTracks().IsEmpty()) {
		OutError = "Animation: sequence has no bone tracks";
		return false;
	}

	TArray<int32> Mapping{};
	TSet<int32> TrackedBones{};
	for (const FAnimBoneTrack& Track : Sequence.GetBoneTracks()) {
		const int32 Index{ Track.GetBoneIndex() };
		if (Index < 0 || Index >= SourceBones.Num() || TrackedBones.Contains(Index) || (Track.GetPositionKeys().IsEmpty() && Track.GetRotationKeys().IsEmpty() && Track.GetScaleKeys().IsEmpty()) || !HasValidKeyCount(Track.GetPositionKeys().Num(), Sequence.GetNumSamples()) || !HasValidKeyCount(Track.GetRotationKeys().Num(), Sequence.GetNumSamples()) || !HasValidKeyCount(Track.GetScaleKeys().Num(), Sequence.GetNumSamples())) {
			OutError = "Animation: invalid or duplicate bone track";
			return false;
		}

		for (const FVector& Key : Track.GetPositionKeys()) {
			if (!IsFiniteVector(Key)) {
				OutError = "Animation: invalid position key";
				return false;
			}
		}

		for (const FVector& Key : Track.GetScaleKeys()) {
			if (!IsFiniteVector(Key)) {
				OutError = "Animation: invalid scale key";
				return false;
			}
		}

		for (const FQuat& Key : Track.GetRotationKeys()) {
			if (!IsValidRotation(Key)) {
				OutError = "Animation: invalid rotation key";
				return false;
			}
		}

		TrackedBones.Add(Index);
		Mapping.Add(BoneMapping[Index]);
	}

	OutMapping = std::move(Mapping);
	return true;
}
