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

	////////////////////////////////////////////////////////////
	// 에디터 전용 함수
	////////////////////////////////////////////////////////////

	// 프리뷰 메시를 바꾸고 애니메이션을 다시 재생합니다.
	bool ChangePreviewMesh(const FString& MeshPath);

	// 처음부터 다시 재생합니다.
	void RestartAnimation();

	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const char* GetEditorTypeName() const override { return "AnimSequence"; }
	UAnimSequenceBase* GetSequence() const { return Sequence; }
	const TArray<FString>& GetPreviewMeshPaths() const { return PreviewMeshPaths; }
	const FString& GetPreviewMeshPath() const { return PreviewMeshPath; }
	const FString& GetLastPlayError() const { return LastPlayError; }

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;

private:
	UAnimSequenceBase* Sequence = nullptr;
	TArray<FString> PreviewMeshPaths;
	FString PreviewMeshPath;
	FString LastPlayError;
};
