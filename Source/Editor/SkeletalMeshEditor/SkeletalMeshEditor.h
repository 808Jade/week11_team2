#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Camera/ViewportCameraMovement.h"

struct FEditorBoneItem
{
	int32 BoneIndex;
	FString Name;
	
	// 자식 Bone Index 배열. 계층 구조 그릴 때 사용
	TArray<int32> ChildBoneIndices;

	// BindPose 행렬의 역행렬. Gizmo 그릴 때 사용
	FMatrix InverseBindPose;
};

struct FBoneGizmoInput
{
	// 이미지 크기에 대한 비율로 저장하여 분리 창과 렌더 타깃의 좌표를 일치시킵니다.
	FVector2 MouseUV = FVector2(0.0f, 0.0f);
	bool bHovered = false;
	bool bFocused = false;
	bool bMousePressed = false;
	bool bMouseDown = false;
	bool bCancel = false;
};

class FSkeletalMeshEditor : public FAssetEditor
{
public:
	FSkeletalMeshEditor(
		class UEditorEngine& InEditorEngine,
		class FEditorManager& InEditorManager,
		const FString& InAssetPath,
		class USkeletalMesh& InMesh
	);

	////////////////////////////////////////////////////////////
	// 에디터 공통 인터페이스
	////////////////////////////////////////////////////////////

	// 틱을 돌립니다.
	void Tick(float DeltaTime) override;

	// 현재 상태를 렌더링합니다.
	void OnRenderPreview() override;

	// 현재 상태에서 라인을 렌더링합니다. (Gizmo, BoneHierarchy 등)
	void OnRenderLine(class FLineBatcher& LineBatcher) override;

	////////////////////////////////////////////////////////////
	// 에디터 전용 함수
	////////////////////////////////////////////////////////////

	// 카메라를 메시에 포커싱합니다.
	void FocusMesh();

	// 프리뷰 카메라 이동 속도를 1.0~150.0 범위로 설정합니다.
	void SetCameraSpeed(float Speed) { CameraSpeed = FMath::Clamp(Speed, 1.0f, 150.0f); }

	// 본을 선택합니다. -1은 선택을 해제합니다.
	void SelectBone(int32 BoneIndex);

	// 선택한 본의 프리뷰 포즈를 부모 기준 로컬 변환으로 조회·수정합니다.
	bool GetSelectedBoneLocalTransform(FTransform& OutTransform) const;
	void SetSelectedBoneLocalTransform(const FTransform& Transform);

	// 스켈레톤의 상태를 초기화합니다.
	void ResetPose();

	// 본의 상태를 초기화합니다.
	void ResetBone(int32 BoneIndex);

	// 본 시각화의 표시 여부를 설정합니다.
	void SetBoneVisualizerVisible(bool bVisible) { bShowBoneVisualizer = bVisible; }

	// 본 시각화의 기본 색상을 설정합니다.
	void SetBoneVisualizerColor(const FVector4& Color) { BoneVisualizerColor = Color; }

	// 뷰포트 상태를 설정합니다. (뷰포트가 보이지 않으면 입력을 무시합니다)
	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input,
		const FBoneGizmoInput& InGizmoInput = {});

	void SetGizmoMode(EGizmoMode Mode);
	void SetGizmoSpace(EGizmoSpace Space);


	////////////////////////////////////////////////////////////
	// Getter
	////////////////////////////////////////////////////////////

	const char* GetEditorTypeName() const override { return "SkeletalMesh"; }
	USkeletalMesh* GetMesh() const { return Mesh; }
	const FGizmo* GetPreviewGizmo() const override { return BoneGizmo.GetTarget() ? &BoneGizmo : nullptr; }
	EGizmoMode GetGizmoMode() const { return BoneGizmo.GetMode(); }
	EGizmoSpace GetGizmoSpace() const { return BoneGizmo.GetSpace(); }
	bool IsGizmoDragging() const { return BoneGizmo.IsUsing(); }
	float GetCameraSpeed() const { return CameraSpeed; }

	const TArray<int32>& GetRootBoneIndices() const { return RootBoneIndices; }
	const TArray<FEditorBoneItem>& GetBoneItems() const { return BoneItems; }
	int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
	bool IsBoneVisualizerVisible() const { return bShowBoneVisualizer; }
	const FVector4& GetBoneVisualizerColor() const { return BoneVisualizerColor; }

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;

	// 리소스를 초기화합니다.
	bool InitializeResources() override;

	// 리소스를 정리합니다.
	void ShutdownResources() override;

	// UI, 에디터에서 사용할 수 있도록 BoneItems를 빌드합니다.
	void BuildBoneItems();

private:
	// 기즈모 조작과 본 피킹에 공통으로 필요한 뷰포트 입력 상태를 확인합니다.
	bool CanProcessViewportInput() const;

	// 현재 포즈의 본 월드 변환을 Gizmo 대상에 반영합니다.
	void SyncBoneGizmo();
	// 기즈모가 입력을 소비했으면 true를 반환합니다.
	bool UpdateBoneGizmo();
	void UpdateBonePicking();
	int32 PickBone(const FVector2& MousePosition) const;
	void ApplyBoneGizmoTransform();
	FQuat GetBoneWorldRotation(int32 BoneIndex) const;

	class USkeletalMesh* Mesh = nullptr;
	class USkeletalMeshComponent* MeshComponent = nullptr;
	USceneComponent* BoneGizmoTarget = nullptr;
	FGizmo BoneGizmo;
	FBoneGizmoInput GizmoInput;
	FTransform DragStartBoneTransform;
	FMatrix DragParentInverse;
	FQuat DragParentRotation;

	FViewportCameraInput CameraInput;
	float CameraSpeed = 5.0f;

	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;

	TArray<FEditorBoneItem> BoneItems;
	TArray<int32> RootBoneIndices;
	int32 SelectedBoneIndex = -1;

	FBox MeshBox;
	float BoneSphereSize = 0.0f;
	FVector4 BoneVisualizerColor = FVector4(0.0f, 0.0f, 0.0f, 1.0f);

	bool bShowBoneVisualizer = true;
	bool bViewportVisible = false;
	bool bInitialFocusPending = true;
};
