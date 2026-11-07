#include "EnginePCH.h"
#include "SkeletalMeshEditor.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/Asset/FbxImporter/FbxImporter.h"
#include "Asset/Animation/AnimSequenceAsset.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"

#include <commdlg.h>
#include <cmath>
#include <filesystem>
#include <utility>

namespace {
	FString SafeAnimationName(const FString& Name) {
		FString Result{};
		for (char Character : Name) {
			Result += (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-' ? Character : '_';
			if (Result.size() == 48) {
				break;
			}
		}

		return Result.empty() ? FString{ "Animation" } : Result;
	}

	FString AnimationOutputPath(const FString& Source, const FString& ClipName, int32 ClipIndex) {
		const std::filesystem::path Input{ std::u8string{ Source.begin(), Source.end() } };
		std::error_code Error{};
		const auto Absolute{ std::filesystem::absolute(Input, Error).lexically_normal().generic_u8string() };
		if (Error) {
			return {};
		}

		uint64 Hash{ 14695981039346656037ull };
		for (char8_t Character : Absolute) {
			Hash = (Hash ^ static_cast<uint8>(Character)) * 1099511628211ull;
		}

		FString HashText{};
		for (int Shift{ 60 }; Shift >= 0; Shift -= 4) {
			HashText += "0123456789abcdef"[(Hash >> Shift) & 0xf];
		}

		const auto Stem{ Input.stem().generic_u8string() };
		return "Assets/Animations/AN_" + SafeAnimationName(FString{ Stem.begin(), Stem.end() }) + "_" + HashText + "/" + std::to_string(ClipIndex) + "_" + SafeAnimationName(ClipName) + ".anim.bin";
	}
}

FSkeletalMeshEditor::~FSkeletalMeshEditor() {
	Shutdown();
}

bool FSkeletalMeshEditor::BrowseAnimationFbx() {
	const FString Path{ ChooseAnimationFile(L"FBX Files (*.fbx)\0*.fbx\0", L"Open FBX Animation") };
	return !Path.empty() && OpenAnimationFbx(Path);
}

bool FSkeletalMeshEditor::BrowseAnimationAsset() {
	const FString Path{ ChooseAnimationFile(L"Animation Assets (*.anim.bin)\0*.anim.bin\0", L"Load Animation Asset") };
	return !Path.empty() && LoadAnimationAsset(Path);
}

FString FSkeletalMeshEditor::ChooseAnimationFile(const wchar_t* Filter, const wchar_t* Title) {
	const bool WasPlaying{ IsAnimationPlaying() };
	PauseAnimation();
	wchar_t FilePath[32768]{};
	OPENFILENAMEW Dialog{};
	Dialog.lStructSize = sizeof(Dialog);
	Dialog.hwndOwner = GetActiveWindow();
	Dialog.lpstrFile = FilePath;
	Dialog.nMaxFile = static_cast<DWORD>(std::size(FilePath));
	Dialog.lpstrFilter = Filter;
	Dialog.lpstrTitle = Title;
	Dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	const bool Selected{ GetOpenFileNameW(&Dialog) != FALSE };
	const DWORD DialogError{ Selected ? 0 : CommDlgExtendedError() };
	mSkipAnimationTick = true;
	if (WasPlaying && MeshComponent && MeshComponent->GetAnimInstance()) {
		MeshComponent->GetAnimInstance()->Resume();
	}

	if (!Selected) {
		if (DialogError != 0) {
			ReportAnimationError("Animation file dialog failed: " + std::to_string(DialogError));
		}

		return {};
	}

	const std::u8string Path{ std::filesystem::path{ FilePath }.generic_u8string() };
	return FString{ Path.begin(), Path.end() };
}

bool FSkeletalMeshEditor::OpenAnimationFbx(const FString& Path) {
	auto Importer{ MakeUnique<FFbxImporter>() };
	FString Error{};
	TArray<FString> Warnings{};
	if (!Importer->Open(Path, Error) || !Importer->ReadSkeletonData(Warnings, Error)) {
		return ReportAnimationError(Error);
	}

	TArray<FString> Names{ Importer->GetAnimationNames() };
	if (Names.IsEmpty()) {
		return ReportAnimationError("FBX contains no animation clips.");
	}

	mAnimationImporter = std::move(Importer);
	mAnimationNames = std::move(Names);
	mAnimationSourceWarnings = std::move(Warnings);
	mAnimationWarnings = mAnimationSourceWarnings;
	mAnimationSourcePath = Path;
	mAnimationError.clear();
	mSkipAnimationTick = true;
	return true;
}

bool FSkeletalMeshEditor::ImportAnimation(int32 AnimationIndex, float SampleRate) {
	if (!MeshComponent || !mAnimationImporter) {
		return ReportAnimationError("Open an animation FBX before importing a clip.");
	}

	TUniquePtr<UAnimSequence> Sequence{ NewObject<UAnimSequence>() };
	FString Error{};
	TArray<FString> Warnings{};
	mAnimationWarnings = mAnimationSourceWarnings;
	const bool Built{ mAnimationImporter->BuildAnimSequence(AnimationIndex, SampleRate, *Sequence, Warnings, Error) };
	mAnimationWarnings.Append(Warnings);
	TUniquePtr<UAnimInstance> Validation{ NewObject<UAnimInstance>() };
	if (!Built || !Validation->Play(Sequence.get(), MeshComponent->GetBones(), mAnimationLooping, Error)) {
		return ReportAnimationError(Error);
	}

	const FString AssetPath{ AnimationOutputPath(mAnimationSourcePath, mAnimationNames[AnimationIndex], AnimationIndex) };
	if (AssetPath.empty() || !FAnimSequenceAsset::Write(AssetPath, *Sequence, Error)) {
		return ReportAnimationError(Error.empty() ? FString{ "Cannot create animation asset path." } : Error);
	}

	if (!UAssetManager::LoadAnimation(AssetPath, Error, true)) {
		return ReportAnimationError(Error);
	}

	if (!LoadAnimationAsset(AssetPath)) {
		return false;
	}

	mAnimationName = mAnimationNames[AnimationIndex];
	mAnimationWarnings = mAnimationSourceWarnings;
	mAnimationWarnings.Append(Warnings);
	HTR_LOG(Info, "Animation asset saved: {}", AssetPath);
	return true;
}

bool FSkeletalMeshEditor::LoadAnimationAsset(const FString& Path) {
	if (!MeshComponent) {
		return ReportAnimationError("Preview mesh is not initialized.");
	}

	FString Error{};
	UAnimSequence* Sequence{ UAssetManager::LoadAnimation(Path, Error) };
	if (!Sequence || !MeshComponent->PlayAnimation(Sequence, mAnimationLooping, Error)) {
		return ReportAnimationError(Error);
	}

	mPreviewAnimation = Sequence;
	const auto Filename{ std::filesystem::path{ std::u8string{ Path.begin(), Path.end() } }.filename().generic_u8string() };
	const FString Name{ Filename.begin(), Filename.end() };
	mAnimationName = Name.substr(0, Name.size() - 9);
	MeshComponent->GetAnimInstance()->SetPlayRate(mAnimationPlayRate);
	if (mAnimationPlayRate < 0.0f) {
		MeshComponent->GetAnimInstance()->SetPosition(Sequence->GetDuration());
		MeshComponent->TickComponent(0.0f);
	}

	mAnimationWarnings.Empty();
	mAnimationError.clear();
	mSkipAnimationTick = true;
	BoneGizmo.EndDrag();
	SyncBoneGizmo();
	return true;
}

bool FSkeletalMeshEditor::PlayAnimation() {
	if (!MeshComponent || !mPreviewAnimation) {
		return ReportAnimationError("Load or import an animation asset before playing.");
	}

	UAnimInstance* Instance{ MeshComponent->GetAnimInstance() };
	if (!Instance || Instance->GetSequence() != mPreviewAnimation) {
		FString Error{};
		if (!MeshComponent->PlayAnimation(mPreviewAnimation, mAnimationLooping, Error)) {
			return ReportAnimationError(Error);
		}

		Instance = MeshComponent->GetAnimInstance();
		Instance->SetPosition(mAnimationPlayRate < 0.0f ? mPreviewAnimation->GetDuration() : 0.0f);
	} else if ((mAnimationPlayRate >= 0.0f && Instance->GetPosition() >= mPreviewAnimation->GetDuration()) || (mAnimationPlayRate < 0.0f && Instance->GetPosition() <= 0.0f)) {
		Instance->SetPosition(mAnimationPlayRate < 0.0f ? mPreviewAnimation->GetDuration() : 0.0f);
	}

	Instance->SetLooping(mAnimationLooping);
	Instance->SetPlayRate(mAnimationPlayRate);
	Instance->Resume();
	MeshComponent->TickComponent(0.0f);
	BoneGizmo.EndDrag();
	SyncBoneGizmo();
	mAnimationError.clear();
	return true;
}

void FSkeletalMeshEditor::PauseAnimation() {
	if (MeshComponent && MeshComponent->GetAnimInstance()) {
		MeshComponent->GetAnimInstance()->Pause();
	}
}

void FSkeletalMeshEditor::StopAnimation() {
	BoneGizmo.EndDrag();
	if (MeshComponent) {
		MeshComponent->StopAnimation();
	}

	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SeekAnimation(float Position) {
	if (!MeshComponent || !mPreviewAnimation || !std::isfinite(Position)) {
		return;
	}

	const bool WasPlaying{ IsAnimationPlaying() };
	UAnimInstance* Instance{ MeshComponent->GetAnimInstance() };
	if (!Instance || Instance->GetSequence() != mPreviewAnimation) {
		if (!PlayAnimation()) {
			return;
		}

		Instance = MeshComponent->GetAnimInstance();
	}

	Instance->SetPosition(Position);
	if (!WasPlaying) {
		Instance->Pause();
	}

	MeshComponent->TickComponent(0.0f);
	BoneGizmo.EndDrag();
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SetAnimationLooping(bool Looping) {
	mAnimationLooping = Looping;
	if (MeshComponent && MeshComponent->GetAnimInstance()) {
		MeshComponent->GetAnimInstance()->SetLooping(Looping);
	}
}

void FSkeletalMeshEditor::SetAnimationPlayRate(float PlayRate) {
	if (!std::isfinite(PlayRate)) {
		return;
	}

	mAnimationPlayRate = PlayRate;
	if (MeshComponent && MeshComponent->GetAnimInstance()) {
		MeshComponent->GetAnimInstance()->SetPlayRate(PlayRate);
	}
}

bool FSkeletalMeshEditor::IsAnimationPlaying() const {
	return MeshComponent && MeshComponent->GetAnimInstance() && MeshComponent->GetAnimInstance()->IsPlaying();
}

bool FSkeletalMeshEditor::IsAnimationLooping() const {
	return mAnimationLooping;
}

float FSkeletalMeshEditor::GetAnimationPlayRate() const {
	return mAnimationPlayRate;
}

float FSkeletalMeshEditor::GetAnimationPosition() const {
	return MeshComponent && MeshComponent->GetAnimInstance() ? MeshComponent->GetAnimInstance()->GetPosition() : 0.0f;
}

float FSkeletalMeshEditor::GetAnimationDuration() const {
	return mPreviewAnimation ? mPreviewAnimation->GetDuration() : 0.0f;
}

const UAnimSequence* FSkeletalMeshEditor::GetPreviewAnimation() const {
	return mPreviewAnimation;
}

const TArray<FString>& FSkeletalMeshEditor::GetAnimationNames() const {
	return mAnimationNames;
}

const FString& FSkeletalMeshEditor::GetAnimationSourcePath() const {
	return mAnimationSourcePath;
}

const FString& FSkeletalMeshEditor::GetAnimationAssetPath() const {
	static const FString Empty{};
	return mPreviewAnimation ? mPreviewAnimation->GetPath() : Empty;
}

const FString& FSkeletalMeshEditor::GetAnimationName() const {
	return mAnimationName;
}

const FString& FSkeletalMeshEditor::GetAnimationError() const {
	return mAnimationError;
}

const TArray<FString>& FSkeletalMeshEditor::GetAnimationWarnings() const {
	return mAnimationWarnings;
}

bool FSkeletalMeshEditor::ReportAnimationError(const FString& Error) {
	mAnimationError = Error;
	HTR_LOG(Error, "Animation preview: {}", Error);
	return false;
}
