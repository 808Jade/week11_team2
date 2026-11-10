#pragma once

#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"

class UAnimSequenceBase;

// UE의 Animation Editor(Animation 모드)에 해당합니다.
// 애니메이션과 같은 Skeleton을 쓰는 Skeletal Mesh를 프리뷰 메시로 띄우고 애니메이션을 재생합니다.
// 뷰포트·본 계층·본 디테일은 SkeletalMesh 에디터의 것을 그대로 재사용합니다.
class FAnimSequenceEditor : public FSkeletalMeshEditor
{
public:
	FAnimSequenceEditor(
		class UEditorEngine& InEditorEngine,
		class FEditorManager& InEditorManager,
		const FString& InAssetPath,
		UAnimSequenceBase& InSequence,
		USkeletalMesh& InPreviewMesh
	);

	// 애니메이션과 같은 Skeleton을 쓰는 Skeletal Mesh 에셋 경로 목록 (UE의 Skeleton Preview Mesh 후보).
	// 이미 로드된 메시는 Skeleton 포인터로, 아직 로드되지 않은 .fbxbin은 파일의 SkeletonKey로 비교합니다.
	static TArray<FString> FindPreviewMeshPaths(const UAnimSequenceBase& Sequence);

	// 애니메이션과 같은 Skeleton을 쓰는 애니메이션 에셋 경로 목록 (UE Asset Browser 목록).
	// Skeleton은 애셋을 로드해야 알 수 있으므로, 아직 로드되지 않은 애니메이션을 로드합니다.
	static TArray<FString> FindCompatibleAnimationPaths(const UAnimSequenceBase& Sequence);

	////////////////////////////////////////////////////////////
	// 에디터 전용 함수
	////////////////////////////////////////////////////////////

	// 프리뷰 메시를 바꾸고 애니메이션을 다시 재생합니다.
	bool ChangePreviewMesh(const FString& MeshPath);

	// 처음부터 다시 재생합니다.
	void RestartAnimation();

	// Asset Browser 목록을 다시 만듭니다.
	void RefreshCompatibleAnimations();

	// 같은 Skeleton의 다른 애니메이션으로 교체합니다. 탭은 그대로 두고 재생할 애니메이션만 바꿉니다. (UE Asset Browser)
	bool ChangeAnimation(const FString& AnimationPath);

	// 처음 연 애셋 경로 또는 지금 보여주는 애니메이션 경로면 이 에디터가 담당합니다.
	bool IsEditingAsset(const FString& Path) const override;
	void OnReopenAsset(const FString& Path) override;

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const char* GetEditorTypeName() const override { return "AnimSequence"; }
	UAnimSequenceBase* GetSequence() const { return Sequence; }
	// 지금 재생 중인 애니메이션 경로. (Asset Browser로 교체하면 GetAssetPath()와 달라집니다)
	const FString& GetSequencePath() const { return SequencePath; }
	const TArray<FString>& GetPreviewMeshPaths() const { return PreviewMeshPaths; }
	const FString& GetPreviewMeshPath() const { return PreviewMeshPath; }
	const FString& GetLastPlayError() const { return LastPlayError; }
	const TArray<FString>& GetCompatibleAnimationPaths() const { return CompatibleAnimationPaths; }

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;

private:
	UAnimSequenceBase* Sequence = nullptr;
	FString SequencePath;
	TArray<FString> PreviewMeshPaths;
	TArray<FString> CompatibleAnimationPaths;
	FString PreviewMeshPath;
	FString LastPlayError;
};
