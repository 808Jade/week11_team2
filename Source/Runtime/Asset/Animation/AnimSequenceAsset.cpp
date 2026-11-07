#include "EnginePCH.h"
#include "Asset/Animation/AnimSequenceAsset.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Asset/Animation/SkeletonAsset.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <utility>

namespace {
	constexpr uint32 Magic{ 0x4D494E41 };
	constexpr uint32 Version{ 2 };
	constexpr uint32 SkeletonMagic{ 0x4C454B53 };
	constexpr uint32 SkeletonVersion{ 1 };
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

	std::filesystem::path FilePath(const FString& Path) {
		return std::filesystem::path{ std::u8string{ Path.begin(), Path.end() } };
	}

	class FAnimationReader {
	public:
		explicit FAnimationReader(const std::filesystem::path& Path);

	public:
		bool Bytes(void* Value, uint64 Size);
		bool Count(uint32& Value, uint64 MinimumBytes);
		bool String(FString& Value);
		bool Vector(FVector& Value);
		bool Bones(TArray<FBoneInfo>& Value);
		bool VectorKeys(TArray<FVector>& Keys, int32 NumSamples);
		bool RotationKeys(TArray<FQuat>& Keys, int32 NumSamples);
		uint64 GetRemaining() const;

	private:
		std::ifstream mStream{};
		uint64 mRemaining{};
	};

	FAnimationReader::FAnimationReader(const std::filesystem::path& Path)
		: mStream{ Path, std::ios::binary | std::ios::ate } {
		if (mStream && mStream.tellg() >= 0) {
			mRemaining = static_cast<uint64>(mStream.tellg());
			mStream.seekg(0);
		}
	}

	bool FAnimationReader::Bytes(void* Value, uint64 Size) {
		if (Size > mRemaining) {
			return false;
		}

		mRemaining -= Size;
		if (Size > 0) {
			mStream.read(static_cast<char*>(Value), static_cast<std::streamsize>(Size));
		}

		return static_cast<bool>(mStream);
	}

	bool FAnimationReader::Count(uint32& Value, uint64 MinimumBytes) {
		return Bytes(&Value, sizeof(Value)) && Value <= INT32_MAX && Value <= mRemaining / MinimumBytes;
	}

	bool FAnimationReader::String(FString& Value) {
		uint32 Size{};
		if (!Count(Size, 1)) {
			return false;
		}

		Value.resize(Size);
		return Bytes(Value.data(), Size) && Value.find('\0') == FString::npos;
	}

	bool FAnimationReader::Vector(FVector& Value) {
		return Bytes(&Value.X, sizeof(float)) && Bytes(&Value.Y, sizeof(float)) && Bytes(&Value.Z, sizeof(float));
	}

	bool FAnimationReader::Bones(TArray<FBoneInfo>& Value) {
		uint32 NumBones{};
		if (!Count(NumBones, 2 * sizeof(uint32) + 25 * sizeof(float)) || NumBones == 0) {
			return false;
		}

		Value.SetNum(static_cast<int32>(NumBones));
		for (FBoneInfo& Bone : Value) {
			FTransform& Bind{ Bone.LocalBindPose };
			if (!String(Bone.Name) || !Bytes(&Bone.ParentIndex, sizeof(Bone.ParentIndex)) || !Vector(Bind.Location) || !Bytes(&Bind.Rotation.Pitch, sizeof(float)) || !Bytes(&Bind.Rotation.Yaw, sizeof(float)) || !Bytes(&Bind.Rotation.Roll, sizeof(float)) || !Vector(Bind.Scale) || !Bytes(Bone.InverseBindPose.M, sizeof(Bone.InverseBindPose.M))) {
				return false;
			}
		}

		return true;
	}

	bool FAnimationReader::VectorKeys(TArray<FVector>& Keys, int32 NumSamples) {
		uint32 CountValue{};
		if (!Count(CountValue, 3 * sizeof(float)) || (CountValue != 0 && CountValue != 1 && CountValue != static_cast<uint32>(NumSamples))) {
			return false;
		}

		Keys.SetNum(static_cast<int32>(CountValue));
		for (FVector& Key : Keys) {
			if (!Vector(Key)) {
				return false;
			}
		}

		return true;
	}

	bool FAnimationReader::RotationKeys(TArray<FQuat>& Keys, int32 NumSamples) {
		uint32 CountValue{};
		if (!Count(CountValue, 4 * sizeof(float)) || (CountValue != 0 && CountValue != 1 && CountValue != static_cast<uint32>(NumSamples))) {
			return false;
		}

		Keys.SetNum(static_cast<int32>(CountValue));
		for (FQuat& Key : Keys) {
			if (!Bytes(&Key.X, sizeof(float)) || !Bytes(&Key.Y, sizeof(float)) || !Bytes(&Key.Z, sizeof(float)) || !Bytes(&Key.W, sizeof(float))) {
				return false;
			}
		}

		return true;
	}

	uint64 FAnimationReader::GetRemaining() const {
		return mRemaining;
	}

	class FAnimationWriter {
	public:
		explicit FAnimationWriter(const std::filesystem::path& Path);

	public:
		void Bytes(const void* Value, uint64 Size);
		void Count(uint32 Value);
		void String(const FString& Value);
		void Vector(const FVector& Value);
		void Bones(const TArray<FBoneInfo>& Value);
		void VectorKeys(const TArray<FVector>& Keys);
		void RotationKeys(const TArray<FQuat>& Keys);
		bool Close();

	private:
		std::ofstream mStream{};
	};

	FAnimationWriter::FAnimationWriter(const std::filesystem::path& Path)
		: mStream{ Path, std::ios::binary | std::ios::trunc } {
	}

	void FAnimationWriter::Bytes(const void* Value, uint64 Size) {
		if (Size > 0) {
			mStream.write(static_cast<const char*>(Value), static_cast<std::streamsize>(Size));
		}
	}

	void FAnimationWriter::Count(uint32 Value) {
		Bytes(&Value, sizeof(Value));
	}

	void FAnimationWriter::String(const FString& Value) {
		if (Value.size() > INT32_MAX) {
			mStream.setstate(std::ios::failbit);
			return;
		}

		Count(static_cast<uint32>(Value.size()));
		Bytes(Value.data(), Value.size());
	}

	void FAnimationWriter::Vector(const FVector& Value) {
		Bytes(&Value.X, sizeof(float));
		Bytes(&Value.Y, sizeof(float));
		Bytes(&Value.Z, sizeof(float));
	}

	void FAnimationWriter::Bones(const TArray<FBoneInfo>& Value) {
		Count(static_cast<uint32>(Value.Num()));
		for (const FBoneInfo& Bone : Value) {
			const FTransform& Bind{ Bone.LocalBindPose };
			String(Bone.Name);
			Bytes(&Bone.ParentIndex, sizeof(Bone.ParentIndex));
			Vector(Bind.Location);
			Bytes(&Bind.Rotation.Pitch, sizeof(float));
			Bytes(&Bind.Rotation.Yaw, sizeof(float));
			Bytes(&Bind.Rotation.Roll, sizeof(float));
			Vector(Bind.Scale);
			Bytes(Bone.InverseBindPose.M, sizeof(Bone.InverseBindPose.M));
		}
	}

	void FAnimationWriter::VectorKeys(const TArray<FVector>& Keys) {
		Count(static_cast<uint32>(Keys.Num()));
		for (const FVector& Key : Keys) {
			Vector(Key);
		}
	}

	void FAnimationWriter::RotationKeys(const TArray<FQuat>& Keys) {
		Count(static_cast<uint32>(Keys.Num()));
		for (const FQuat& Key : Keys) {
			Bytes(&Key.X, sizeof(float));
			Bytes(&Key.Y, sizeof(float));
			Bytes(&Key.Z, sizeof(float));
			Bytes(&Key.W, sizeof(float));
		}
	}

	bool FAnimationWriter::Close() {
		mStream.flush();
		mStream.close();
		return static_cast<bool>(mStream);
	}

	FString GetBoneIdentity(const TArray<FBoneInfo>& Bones) {
		FString Identity{};
		for (const FBoneInfo& Bone : Bones) {
			const FTransform& Bind{ Bone.LocalBindPose };
			float TransformValues[]{ Bind.Location.X, Bind.Location.Y, Bind.Location.Z, Bind.Rotation.Pitch, Bind.Rotation.Yaw, Bind.Rotation.Roll, Bind.Scale.X, Bind.Scale.Y, Bind.Scale.Z };
			for (float& Value : TransformValues) {
				if (Value == 0.0f) {
					Value = 0.0f;
				}
			}

			Identity.append(Bone.Name);
			Identity.push_back('\0');
			Identity.append(reinterpret_cast<const char*>(&Bone.ParentIndex), sizeof(Bone.ParentIndex));
			Identity.append(reinterpret_cast<const char*>(TransformValues), sizeof(TransformValues));
		}

		return Identity;
	}

	template <typename WritePayload>
	bool WriteAssetFile(const FString& Path, const FString& AssetType, WritePayload Write, FString& OutError) {
		std::filesystem::path Temporary{};
		try {
			const std::filesystem::path Target{ FilePath(Path) };
			if (!Target.parent_path().empty()) {
				std::filesystem::create_directories(Target.parent_path());
			}

			Temporary = Target.native() + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
			FAnimationWriter Writer{ Temporary };
			Write(Writer);
			if (!Writer.Close()) {
				OutError = AssetType + ": write failed: " + Path;
			} else if (!MoveFileExW(Temporary.c_str(), Target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				OutError = AssetType + ": file replacement failed (" + std::to_string(GetLastError()) + "): " + Path;
			} else {
				return true;
			}
		} catch (const std::exception& Error) {
			OutError = AssetType + ": write failed: " + Error.what();
		}

		if (!Temporary.empty()) {
			std::error_code CleanupError{};
			std::filesystem::remove(Temporary, CleanupError);
		}

		return false;
	}
}

bool FAnimSequenceAsset::IsAssetPath(const FString& Path) {
	FString Suffix{ Path.size() >= 9 ? Path.substr(Path.size() - 9) : FString{} };
	for (char& Character : Suffix) {
		if (Character >= 'A' && Character <= 'Z') {
			Character += 'a' - 'A';
		}
	}

	return Suffix == ".anim.bin";
}

bool FAnimSequenceAsset::Read(const FString& Path, UAnimSequence& OutSequence, FString& OutError) {
	OutError.clear();
	try {
		FAnimationReader Reader{ FilePath(Path) };
		uint32 FileMagic{};
		uint32 FileVersion{};
		float SampleRate{};
		uint32 NumSamples{};
		if (!Reader.Bytes(&FileMagic, sizeof(FileMagic)) || FileMagic != Magic || !Reader.Bytes(&FileVersion, sizeof(FileVersion)) || FileVersion != Version || !Reader.Bytes(&SampleRate, sizeof(SampleRate)) || !Reader.Bytes(&NumSamples, sizeof(NumSamples)) || NumSamples == 0 || NumSamples > INT32_MAX) {
			OutError = "Animation asset: invalid header or version: " + Path;
			return false;
		}

		FString SkeletonPath{};
		if (!Reader.String(SkeletonPath)) {
			OutError = "Animation asset: missing skeleton path: " + Path;
			return false;
		}

		USkeleton* Skeleton{ UAssetManager::LoadSkeleton(SkeletonPath, OutError) };
		if (!Skeleton) {
			return false;
		}

		const TArray<FBoneInfo>& RefSkeleton{ Skeleton->GetRefSkeleton() };
		uint32 NumTracks{};
		if (!Reader.Count(NumTracks, 4 * sizeof(uint32)) || NumTracks == 0 || NumTracks > static_cast<uint32>(RefSkeleton.Num())) {
			OutError = "Animation asset: invalid track count: " + Path;
			return false;
		}

		TArray<FAnimBoneTrack> Tracks{};
		Tracks.Reserve(NumTracks);
		for (uint32 Index{}; Index < NumTracks; ++Index) {
			int32 BoneIndex{};
			TArray<FVector> Positions{};
			TArray<FQuat> Rotations{};
			TArray<FVector> Scales{};
			if (!Reader.Bytes(&BoneIndex, sizeof(BoneIndex)) || !Reader.VectorKeys(Positions, static_cast<int32>(NumSamples)) || !Reader.RotationKeys(Rotations, static_cast<int32>(NumSamples)) || !Reader.VectorKeys(Scales, static_cast<int32>(NumSamples))) {
				OutError = "Animation asset: invalid or truncated track data: " + Path;
				return false;
			}

			Tracks.Add(FAnimBoneTrack{ BoneIndex, std::move(Positions), std::move(Rotations), std::move(Scales) });
		}

		if (Reader.GetRemaining() != 0 || !UAnimSequence::ValidateData(SampleRate, static_cast<int32>(NumSamples), RefSkeleton, Tracks, OutError)) {
			if (OutError.empty()) {
				OutError = "Animation asset: unexpected trailing data: " + Path;
			}

			return false;
		}

		OutSequence.Build(SampleRate, static_cast<int32>(NumSamples), Skeleton, std::move(Tracks));
		return true;
	} catch (const std::exception& Error) {
		OutError = "Animation asset: read failed: " + FString{ Error.what() };
		return false;
	}
}

bool FAnimSequenceAsset::Write(const FString& Path, const UAnimSequence& Sequence, FString& OutError) {
	OutError.clear();
	if (!IsAssetPath(Path) || !Sequence.Validate(OutError)) {
		if (OutError.empty()) {
			OutError = "Animation asset: expected .anim.bin output path";
		}

		return false;
	}

	const USkeleton& Skeleton{ *Sequence.GetSkeleton() };
	if (!FSkeletonAsset::Write(FAssetUtils::ResolveAssetFilePath(Skeleton.GetPath()), Skeleton.GetRefSkeleton(), OutError)) {
		return false;
	}

	return WriteAssetFile(Path, "Animation asset", [&Sequence, &Skeleton](FAnimationWriter& Writer) {
		Writer.Count(Magic);
		Writer.Count(Version);
		const float SampleRate{ Sequence.GetSampleRate() };
		Writer.Bytes(&SampleRate, sizeof(SampleRate));
		Writer.Count(static_cast<uint32>(Sequence.GetNumSamples()));
		Writer.String(Skeleton.GetPath());

		Writer.Count(static_cast<uint32>(Sequence.GetBoneTracks().Num()));
		for (const FAnimBoneTrack& Track : Sequence.GetBoneTracks()) {
			const int32 BoneIndex{ Track.GetBoneIndex() };
			Writer.Bytes(&BoneIndex, sizeof(BoneIndex));
			Writer.VectorKeys(Track.GetPositionKeys());
			Writer.RotationKeys(Track.GetRotationKeys());
			Writer.VectorKeys(Track.GetScaleKeys());
		}

	}, OutError);
}

bool FSkeletonAsset::IsAssetPath(const FString& Path) {
	FString Suffix{ Path.size() >= 13 ? Path.substr(Path.size() - 13) : FString{} };
	for (char& Character : Suffix) {
		if (Character >= 'A' && Character <= 'Z') {
			Character += 'a' - 'A';
		}
	}

	return Suffix == ".skeleton.bin";
}

FString FSkeletonAsset::GetGeneratedPath(const TArray<FBoneInfo>& Bones) {
	uint64 Hash{ 14695981039346656037ull };
	for (unsigned char Byte : GetBoneIdentity(Bones)) {
		Hash = (Hash ^ Byte) * 1099511628211ull;
	}

	return "Skeletons/" + std::format("{:016x}", Hash) + ".skeleton.bin";
}

bool FSkeletonAsset::HaveSameBones(const TArray<FBoneInfo>& A, const TArray<FBoneInfo>& B) {
	return A.Num() == B.Num() && GetBoneIdentity(A) == GetBoneIdentity(B);
}

bool FSkeletonAsset::Read(const FString& Path, TArray<FBoneInfo>& OutBones, FString& OutError) {
	OutError.clear();
	try {
		FAnimationReader Reader{ FilePath(Path) };
		uint32 FileMagic{};
		uint32 FileVersion{};
		TArray<FBoneInfo> Bones{};
		if (!Reader.Bytes(&FileMagic, sizeof(FileMagic)) || FileMagic != SkeletonMagic || !Reader.Bytes(&FileVersion, sizeof(FileVersion)) || FileVersion != SkeletonVersion || !Reader.Bones(Bones) || Reader.GetRemaining() != 0) {
			OutError = "Skeleton asset: invalid or truncated file: " + Path;
			return false;
		}

		OutBones = std::move(Bones);
		return true;
	} catch (const std::exception& Error) {
		OutError = "Skeleton asset: read failed: " + FString{ Error.what() };
		return false;
	}
}

bool FSkeletonAsset::Write(const FString& Path, const TArray<FBoneInfo>& Bones, FString& OutError) {
	OutError.clear();
	try {
		if (std::filesystem::exists(FilePath(Path))) {
			TArray<FBoneInfo> ExistingBones{};
			if (!Read(Path, ExistingBones, OutError)) {
				return false;
			}

			if (!HaveSameBones(Bones, ExistingBones)) {
				OutError = "Skeleton asset: existing file has different bones: " + Path;
				return false;
			}

			return true;
		}
	} catch (const std::exception& Error) {
		OutError = "Skeleton asset: write failed: " + FString{ Error.what() };
		return false;
	}

	return WriteAssetFile(Path, "Skeleton asset", [&Bones](FAnimationWriter& Writer) {
		Writer.Count(SkeletonMagic);
		Writer.Count(SkeletonVersion);
		Writer.Bones(Bones);
	}, OutError);
}
