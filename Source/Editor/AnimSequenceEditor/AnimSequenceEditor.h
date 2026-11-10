#pragma once

#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Animation/AnimNotifyTypes.h"
#include <unordered_map>

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

	////////////////////////////////////////////////////////////
	// 재생 제어 (Timeline 버튼). 상태는 프리뷰 컴포넌트의 UAnimSingleNodeInstance에 있습니다.
	////////////////////////////////////////////////////////////

	// 재생합니다. bReverse면 역재생(음수 배속). 루프가 아니고 진행 방향의 끝에 있으면 반대쪽 끝에서 다시 시작합니다.
	void Play(bool bReverse);
	void Pause();
	// 정지 후 처음/끝으로 이동합니다.
	void JumpToStart();
	void JumpToEnd();
	// 정지 후 Delta 프레임만큼 이동합니다. 루프면 끝과 처음이 이어집니다.
	void StepFrame(int32 Delta);
	// 정지 후 Time(초)으로 이동합니다. (스크러빙)
	void SetPlaybackTime(float Time);
	void SetLooping(bool bInLooping);
	// 배속 크기(양수). 방향은 Play(bReverse)가 정합니다.
	void SetPlaySpeed(float InSpeed);

	bool IsPlaying() const;
	bool IsPlayingReverse() const;
	bool IsLooping() const { return bLooping; }
	float GetPlaySpeed() const { return PlaySpeed; }
	float GetPlaybackTime() const;
	float GetPlaybackLength() const;
	float GetFrameRate() const;
	int32 GetNumberOfFrames() const;

	// Asset Browser 목록을 다시 만듭니다.
	void RefreshCompatibleAnimations();

	// 같은 Skeleton의 다른 애니메이션으로 교체합니다. 탭은 그대로 두고 재생할 애니메이션만 바꿉니다. (UE Asset Browser)
	bool ChangeAnimation(const FString& AnimationPath);

	////////////////////////////////////////////////////////////
	// Notify 저장 (애니메이션 옆 .notifies.json)
	////////////////////////////////////////////////////////////

	// 지금 보여주는 애니메이션의 Notifies를 사이드카 파일에 씁니다.
	bool SaveNotifies();
	// 마지막으로 읽거나 저장한 뒤 Notifies가 바뀌었는지.
	bool HasUnsavedNotifies() const;
	const FString& GetLastNotifySaveError() const { return LastNotifySaveError; }

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

	// 애니메이션마다 디스크에 있는 것으로 아는 Notifies. 지금 Notifies와 다르면 저장 안 된 변경이 있습니다.
	// Asset Browser로 다른 애니메이션에 갔다가 돌아와도 저장 안 된 상태가 유지되도록 애니메이션별로 둡니다.
	void RememberSavedNotifies();
	std::unordered_map<const UAnimSequenceBase*, TArray<FAnimNotifyEvent>> SavedNotifies;
	FString LastNotifySaveError;

	// 프리뷰 메시나 애니메이션을 바꿔 인스턴스가 새로 생겨도 유지할 재생 설정
	class UAnimSingleNodeInstance* GetPlayer() const;
	float PlaySpeed = 1.0f;
	bool bLooping = true;
	bool bReverse = false;
};
