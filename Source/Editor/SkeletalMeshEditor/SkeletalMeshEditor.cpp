#include "EnginePCH.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "SkeletalMeshEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Engine/World.h"
#include "Math/BoxSphereBounds.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"

namespace
{
	// 원근 나눗셈 전에 선분을 깊이 범위(0 <= Z <= W)로 잘라냅니다.
	bool ClipBoneSegmentToView(FVector4& Start, FVector4& End)
	{
		const auto ClipPlane = [&](float StartDistance, float EndDistance)
		{
			if (StartDistance < 0.0f && EndDistance < 0.0f)
			{
				return false;
			}

			if (StartDistance < 0.0f || EndDistance < 0.0f)
			{
				const float Alpha = StartDistance / (StartDistance - EndDistance);
				const FVector4 Intersection = Start + (End - Start) * Alpha;
				(StartDistance < 0.0f ? Start : End) = Intersection;
			}

			return true;
		};

		return ClipPlane(Start.Z, End.Z) &&
			ClipPlane(Start.W - Start.Z, End.W - End.Z) &&
			Start.W >= 0.0001f && End.W >= 0.0001f;
	}
}

// Bone의 네임스페이스를 제외한 마지막 콜론 뒤의 이름만 반환
FString GetBoneDisplayName(const FString& BoneName)
{
	const auto Separator = BoneName.find_last_of(':');

	if (Separator == FString::npos)
	{
		return BoneName;
	}

	return BoneName.substr(Separator + 1);
}

FSkeletalMeshEditor::FSkeletalMeshEditor(
	UEditorEngine& InEditorEngine,
	FEditorManager& InEditorManager,
	const FString& InAssetPath,
	USkeletalMesh& InMesh
)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, Mesh(&InMesh)
{
}

bool FSkeletalMeshEditor::InitializeResources()
{
	// 메시가 없음
	if (!Mesh)
	{
		return false;
	}

	// 기본 포즈를 표시할 LOD0 렌더 데이터가 필요합니다.
	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	if (LODs.IsEmpty())
	{
		return false;
	}

	if (LODs[0].BindVertices.IsEmpty() || LODs[0].Indices.IsEmpty())
	{
		return false;
	}

	// 부모 InitializeResources 호출 (FPreviewScene 초기화 및 세팅)
	if (!FAssetEditor::InitializeResources())
	{
		return false;
	}

	// PreviewScene에 SkeletalMeshComponent 추가
	auto Component = PreviewScene.AddComponent(USkeletalMeshComponent::StaticClass());
	MeshComponent = Cast<USkeletalMeshComponent>(Component);
	if (!MeshComponent)
	{
		return false;
	}

	// SkeletalMeshComponent에 메시를 설정
	MeshComponent->SetSkeletalMesh(Mesh);
	// 애니메이션 재생 없이 바인드 포즈의 본 변환과 스키닝 결과를 준비합니다.
	MeshComponent->RefreshBoneTransformsNow();

	MeshBox = Mesh->GetRenderData().LODRenderData[0].BindAABB;
	const FVector Extent = MeshBox.Max - MeshBox.Min;
	BoneSphereSize = FMath::Max(Extent.Size() * 0.0025f, 0.001f);

	// 본의 월드 변환을 담을 전용 대상을 만들고 부모 변환의 영향을 제거합니다.
	BoneGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BoneGizmoTarget)
	{
		return false;
	}

	BoneGizmoTarget->DetachFromParent();


	// BoneItems를 빌드합니다. (에디터 UI에서 본 계층 구조를 표시하기 위해)
	BuildBoneItems();

	// 카메라를 메시에 포커싱
	FocusMesh();

	return true;
}

TUniquePtr<IEditorTab> FSkeletalMeshEditor::CreateTab()
{
	return MakeUnique<FSkeletalMeshEditorTab>(*this);
}

void FSkeletalMeshEditor::FocusMesh()
{
	BoneGizmo.EndDrag();

	if (!ViewportClient)
	{
		return;
	}

	if (!Mesh)
	{
		return;
	}

	// 메시의 AABB를 가져와서 BoxSphereBounds로 변환
	const FBox& Box = Mesh->GetRenderData().LODRenderData[0].BindAABB;
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	FBoxSphereBounds Bound
	{
		.Origin = (Box.Min + Box.Max) * 0.5f,
		.BoxExtent = Extent,
		.SphereRadius = Extent.Size()
	};

	// AABB로 ViewportClient의 카메라를 메시에 포커싱
	ViewportClient->FocusOnBounds(Bound);
}

void FSkeletalMeshEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input,
	const FBoneGizmoInput& InGizmoInput)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
	GizmoInput = InGizmoInput;
}

void FSkeletalMeshEditor::SetGizmoMode(EGizmoMode Mode)
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetMode(Mode);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SetGizmoSpace(EGizmoSpace Space)
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetSpace(Space);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::Tick(float DeltaTime)
{
	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// 뷰포트가 보이면 카메라 이동을 적용
	if (bViewportVisible && !BoneGizmo.IsUsing() && !GizmoInput.bMouseDown)
	{
		FViewportCameraState Camera{
			.Location = ViewportClient->GetViewLocation(),
			.Rotation = ViewportClient->GetViewRotation(),
			.OrthoWidth = ViewportClient->GetOrthoWidth(),
			.ViewWidth = static_cast<float>(ViewportWidth),
			.bPerspective = true
		};
		ApplyViewportCameraMovement(Camera, CameraInput, DeltaTime, CameraSpeed, 0.2f);
		ViewportClient->SetViewLocation(Camera.Location);
		ViewportClient->SetViewRotation(Camera.Rotation);
	}

	// 부모 Tick 호출 (PreviewScene Tick)
	FAssetEditor::Tick(DeltaTime);

	const bool bGizmoConsumedInput = UpdateBoneGizmo();

	if (!bGizmoConsumedInput)
	{
		UpdateBonePicking();
	}

	CameraInput = {};
	GizmoInput = {};

	// 드래그 중에는 Gizmo가 포즈를 갱신하므로 역방향 동기화를 멈춥니다.
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::OnRenderPreview()
{
	// Viewport가 보이지 않으면 렌더링하지 않음
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender)
	{
		return;
	}

	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// ViewportClient 크기 변경
	ViewportClient->Resize(ViewportWidth, ViewportHeight);

	// ViewportClient의 RTV가 없다면 렌더링하지 않음
	if (!ViewportClient->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
	{
		return;
	}

	// FocusMesh가 아직 호출되지 않았다면 호출
	if (bInitialFocusPending)
	{
		FocusMesh();
		bInitialFocusPending = false;
	}

	// EditorEngine에 ViewportClient를 전달하여 렌더링
	EditorEngine.RenderAssetEditor(*this);
}

void FSkeletalMeshEditor::SelectBone(int32 BoneIndex)
{
	if (BoneIndex >= -1 && BoneIndex < BoneItems.Num())
	{
		BoneGizmo.EndDrag();
		SelectedBoneIndex = BoneIndex;
		SyncBoneGizmo();
	}
}

void FSkeletalMeshEditor::SyncBoneGizmo()
{
	if (!MeshComponent || !BoneGizmoTarget || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}

	if (BoneGizmo.IsUsing())
	{
		return;
	}

	const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex);
	const FVector Location = BoneWorld.TransformPosition(FVector::ZeroVector);
	const FRotator Rotation = GetBoneWorldRotation(SelectedBoneIndex).ToFRotator();

	// 스케일은 조작 배율로만 사용합니다. 부모의 비균일 스케일과 전단을 축 회전에 섞지 않습니다.
	BoneGizmoTarget->SetTransform(FTransform(Rotation, Location));
	BoneGizmo.SetTarget(BoneGizmoTarget);
}

FQuat FSkeletalMeshEditor::GetBoneWorldRotation(int32 BoneIndex) const
{
	FQuat Rotation = FQuat::Identity();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	while (BoneIndex >= 0)
	{
		Rotation = MeshComponent->GetBoneLocalTransform(BoneIndex).Rotation.Quaternion() * Rotation;
		BoneIndex = Bones[BoneIndex].ParentIndex;
	}

	return (MeshComponent->GetWorldRotation().Quaternion() * Rotation).Normalized();
}

bool FSkeletalMeshEditor::CanProcessViewportInput() const
{
	if
		(
			// 뷰포트 클라이언트가 없으면 입력을 무시
			!ViewportClient ||

			// 뷰포트가 보이지 않으면 입력을 무시
			!bViewportVisible ||

			// 뷰포트가 포커싱되지 않으면 입력을 무시
			!GizmoInput.bFocused ||

			// 뷰포트 너비가 변경되면 입력을 무시
			ViewportClient->GetWidth() != ViewportWidth ||

			// 뷰포트 높이가 변경되면 입력을 무시
			ViewportClient->GetHeight() != ViewportHeight
			)
	{
		return false;
	}

	return true;
}

bool FSkeletalMeshEditor::UpdateBoneGizmo()
{
	if
		(
			// 공통 뷰포트 입력 조건을 만족하지 않으면 입력을 무시
			!CanProcessViewportInput() ||

			// BoneGizmo의 대상이 없으면 입력을 무시
			!BoneGizmo.GetTarget()
			)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	if (GizmoInput.bCancel)
	{
		if (BoneGizmo.IsUsing())
		{
			MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, DragStartBoneTransform);
			MeshComponent->RefreshBoneTransformsNow();
		}
		BoneGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = BoneGizmo.IsUsing();
	if ((!GizmoInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	SyncBoneGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(GizmoInput.MouseUV.X * Width, GizmoInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 시작하고, 버튼을 놓는 프레임의 마지막 마우스 위치도 반영합니다.
	const bool bUpdateDrag = bWasUsing || (GizmoInput.bMousePressed && GizmoInput.bMouseDown);
	BoneGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && BoneGizmo.IsUsing())
	{
		const int32 ParentIndex = Mesh->GetRefSkeleton()[SelectedBoneIndex].ParentIndex;
		const FMatrix ParentWorld = ParentIndex >= 0
			? MeshComponent->GetBoneWorldMatrix(ParentIndex) : MeshComponent->GetWorldMatrix();

		// 스케일이 0인 부모의 역행렬로 이동을 계산할 수 없습니다.
		if (BoneGizmo.GetMode() == EGizmoMode::Location && fabsf(ParentWorld.Determinant()) < 1e-12f)
		{
			BoneGizmo.EndDrag();
			return true;
		}

		DragStartBoneTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
		DragParentRotation = GetBoneWorldRotation(ParentIndex);
		if (BoneGizmo.GetMode() == EGizmoMode::Location)
		{
			DragParentInverse = ParentWorld.Inverse();
		}
	}

	if (BoneGizmo.IsUsing())
	{
		ApplyBoneGizmoTransform();
		if (!GizmoInput.bMouseDown)
		{
			BoneGizmo.EndDrag();
		}
	}

	return bWasUsing || BoneGizmo.IsUsing() || BoneGizmo.GetHoveredAxis() >= 0;
}

void FSkeletalMeshEditor::UpdateBonePicking()
{
	if
		(
			// 공통 뷰포트 입력 조건을 만족하지 않으면 입력을 무시
			!CanProcessViewportInput() ||

			// 스켈레탈 메시가 없으면 입력을 무시
			!Mesh ||

			// 프리뷰 메시 컴포넌트가 없으면 입력을 무시
			!MeshComponent ||

			// 본 시각화가 꺼져 있으면 입력을 무시
			!bShowBoneVisualizer ||

			// 마우스가 뷰포트 위에 없으면 입력을 무시
			!GizmoInput.bHovered ||

			// 좌클릭이 시작된 프레임이 아니면 입력을 무시
			!GizmoInput.bMousePressed ||

			// 취소 입력이 들어오면 입력을 무시
			GizmoInput.bCancel ||

			// 카메라를 조작 중이면 입력을 무시
			CameraInput.bCaptured ||

			// 기즈모를 드래그 중이면 입력을 무시
			BoneGizmo.IsUsing() ||

			// 뷰포트 너비가 0이면 입력을 무시
			ViewportWidth == 0 ||

			// 뷰포트 높이가 0이면 입력을 무시
			ViewportHeight == 0
			)
	{
		return;
	}

	const FVector2 MousePosition(GizmoInput.MouseUV.X * ViewportWidth,
		GizmoInput.MouseUV.Y * ViewportHeight);
	SelectBone(PickBone(MousePosition));
}

int32 FSkeletalMeshEditor::PickBone(const FVector2& MousePosition) const
{
	constexpr float HitPixels = 12.0f;
	const FMatrix ViewProjection = ViewportClient->GetViewProjectionMatrix();
	const FMatrix InverseViewProjection = ViewProjection.Inverse();
	const FVector CameraPosition = ViewportClient->GetViewLocation();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();

	TArray<FVector4> ClipPositions;
	ClipPositions.SetNum(Bones.Num());
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector Position = MeshComponent->GetBoneWorldMatrix(Index)
			.TransformPosition(FVector::ZeroVector);
		ClipPositions[Index] = FVector4(Position, 1.0f) * ViewProjection;
	}

	const auto ToScreen = [&](const FVector4& Clip)
	{
		return FVector2((Clip.X / Clip.W * 0.5f + 0.5f) * ViewportWidth,
			(0.5f - Clip.Y / Clip.W * 0.5f) * ViewportHeight);
	};

	int32 BestBoneIndex = -1;
	float BestCameraDistanceSquared = FLT_MAX;
	const auto ConsiderCandidate = [&](int32 Index, float ScreenDistance, const FVector4& ClipPosition)
	{
		if (ScreenDistance > HitPixels)
		{
			return;
		}

		const FVector4 World = ClipPosition * InverseViewProjection;
		const FVector Position(World.X / World.W, World.Y / World.W, World.Z / World.W);
		const float CameraDistanceSquared = (Position - CameraPosition).SizeSquared();
		if (CameraDistanceSquared < BestCameraDistanceSquared)
		{
			BestBoneIndex = Index;
			BestCameraDistanceSquared = CameraDistanceSquared;
		}
	};

	// Joint 후보 중 카메라에 가장 가까운 Joint를 선택합니다.
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector4& Clip = ClipPositions[Index];
		if (Clip.W < 0.0001f || Clip.Z < 0.0f || Clip.Z > Clip.W)
		{
			continue;
		}

		const FVector2 Delta = MousePosition - ToScreen(Clip);
		ConsiderCandidate(Index, sqrtf(Delta.X * Delta.X + Delta.Y * Delta.Y), Clip);
	}

	if (BestBoneIndex >= 0)
	{
		return BestBoneIndex;
	}

	// Joint 후보가 없을 때만 Bone을 검사합니다. 연결부는 부모 본을 선택합니다.
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const int32 ParentIndex = Bones[Index].ParentIndex;
		if (ParentIndex < 0 || ParentIndex >= Bones.Num())
		{
			continue;
		}

		FVector4 Start = ClipPositions[ParentIndex];
		FVector4 End = ClipPositions[Index];
		if (!ClipBoneSegmentToView(Start, End))
		{
			continue;
		}

		const FVector2 ScreenStart = ToScreen(Start);
		const FVector2 ScreenEnd = ToScreen(End);
		const FVector2 Segment = ScreenEnd - ScreenStart;
		const FVector2 ToMouse = MousePosition - ScreenStart;
		const float LengthSquared = Segment.X * Segment.X + Segment.Y * Segment.Y;
		const float Alpha = LengthSquared > 1e-6f
			? FMath::Clamp((ToMouse.X * Segment.X + ToMouse.Y * Segment.Y) / LengthSquared, 0.0f, 1.0f)
			: 0.0f;

		// 화면에서 마우스에 가장 가까운 점을 보간하고 월드 공간의 카메라 거리로 비교합니다.
		const FVector4 ClipPosition = (Start / Start.W) * (1.0f - Alpha) + (End / End.W) * Alpha;
		ConsiderCandidate(ParentIndex, DistanceToSegment(MousePosition, ScreenStart, ScreenEnd), ClipPosition);
	}

	return BestBoneIndex;
}

void FSkeletalMeshEditor::ApplyBoneGizmoTransform()
{
	FTransform Local = DragStartBoneTransform;
	switch (BoneGizmo.GetMode())
	{
	case EGizmoMode::Location:
		Local.Location = DragParentInverse.TransformPosition(BoneGizmoTarget->GetWorldLocation());
		break;
	case EGizmoMode::Rotation:
		Local.Rotation = (DragParentRotation.Inverse() *
			BoneGizmoTarget->GetRelativeRotation().Quaternion()).Normalized().ToFRotator();
		break;
	case EGizmoMode::Scale:
		Local.Scale = DragStartBoneTransform.Scale * BoneGizmoTarget->GetRelativeScale3D();
		break;
	}

	// 조작한 성분만 변경하고 나머지는 드래그 시작 시점의 로컬 값을 유지합니다.
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Local);
	MeshComponent->RefreshBoneTransformsNow();
}

bool FSkeletalMeshEditor::GetSelectedBoneLocalTransform(FTransform& OutTransform) const
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return false;
	}

	OutTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
	return true;
}

void FSkeletalMeshEditor::SetSelectedBoneLocalTransform(const FTransform& Transform)
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return;
	}

	// 원본 애셋 대신 프리뷰 컴포넌트의 포즈만 수정합니다. 본 갱신은 다음 World Tick에서 수행합니다.
	BoneGizmo.EndDrag();
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Transform);
}

void FSkeletalMeshEditor::ResetPose()
{
	if (!MeshComponent)
	{
		return;
	}

	BoneGizmo.EndDrag();
	MeshComponent->ResetPose();
}

void FSkeletalMeshEditor::ResetBone(int32 BoneIndex)
{
	if (!Mesh)
	{
		return;
	}

	if (!MeshComponent)
	{
		return;
	}

	if (BoneIndex < 0 || BoneIndex >= Mesh->GetRefSkeleton().Num())
	{
		return;
	}

	BoneGizmo.EndDrag();
	FTransform Transform = Mesh->GetRefSkeleton()[BoneIndex].LocalBindPose;
	MeshComponent->SetBoneLocalTransform(BoneIndex, Transform);
}

void FSkeletalMeshEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	if (!bShowBoneVisualizer)
	{
		return;
	}

	if (!Mesh)
	{
		return;
	}
	
	if (!MeshComponent)
	{
		return;
	}

	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();

	TArray<FVector> Positions;
	Positions.SetNum(Bones.Num());

	// 각 Bone의 원점을 월드 공간으로 변환하여 Positions에 저장
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector4 Position = FVector4(0.0f, 0.0f, 0.0f, 1.0f) * MeshComponent->GetBoneWorldMatrix(Index);
		Positions[Index] = FVector(Position.X, Position.Y, Position.Z);
	}

	// Bone 시각화 그리기
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FVector4 Color = Index == SelectedBoneIndex
			? FVector4(1.0f, 1.0f, 0.0f, 1.0f)
			: BoneVisualizerColor;
		
		// Joint 먼저 그리기
		LineBatcher.AddSphere(Positions[Index], BoneSphereSize, Color);
		
		const int32 ParentIndex = Bones[Index].ParentIndex;

		// 부모가 있다면, 부모에서 자식으로 이어지는 Bone을 그리기
		if (ParentIndex >= 0 && ParentIndex < Bones.Num())
		{
			FVector4 BoneColor = BoneVisualizerColor;
			if (Index == SelectedBoneIndex)
			{
				// 현재 본이 선택한 본이면 부모 본을 노란색으로 표시
				BoneColor = FVector4(1.0f, 0.5f, 0.0f, 1.0f);
			}
			else if (ParentIndex == SelectedBoneIndex)
			{
				// 부모 Index가 선택된 본이면 부모 본을 주황색으로 표시
				BoneColor = FVector4(1.0f, 1.0f, 0.0f, 1.0f);
			}
			
			// ParentIndex -> Index Direction
			const FVector Direction = (Positions[Index] - Positions[ParentIndex]).Normalized();

			const FVector Start = Positions[ParentIndex] + Direction * BoneSphereSize;
			const FVector End = Positions[Index] - Direction * BoneSphereSize;

			const float Length = (Positions[Index] - Positions[ParentIndex]).Size();
			const float HalfWidth = FMath::Min(BoneSphereSize * 1.5f, Length * 0.1f);
			LineBatcher.AddPyramid(Start, End, HalfWidth, BoneColor);
		}
	}
}

void FSkeletalMeshEditor::ShutdownResources()
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	BoneGizmoTarget = nullptr;
	SelectedBoneIndex = -1;
	MeshComponent = nullptr;
	Mesh = nullptr;
	FAssetEditor::ShutdownResources();
}

void FSkeletalMeshEditor::BuildBoneItems()
{
	if (!Mesh)
	{
		return;
	}

	const TArray<FBoneInfo>& BoneData = Mesh->GetRefSkeleton();
	BoneItems.Empty();
	RootBoneIndices.Empty();
	SelectBone(-1);

	for (int32 BoneIndex = 0; BoneIndex < BoneData.Num(); ++BoneIndex)
	{
		const FBoneInfo& Bone = BoneData[BoneIndex];

		BoneItems.Add(FEditorBoneItem{
			.BoneIndex = BoneIndex,
			.Name = GetBoneDisplayName(Bone.Name),
			.ChildBoneIndices = {},
			.InverseBindPose = Bone.InverseBindPose
		});

		FEditorBoneItem& BoneItem = BoneItems[BoneIndex];

		if (Bone.ParentIndex == -1)
		{
			RootBoneIndices.Add(BoneIndex);
		}
		else if (Bone.ParentIndex >= 0)
		{
			BoneItems[Bone.ParentIndex].ChildBoneIndices.Add(BoneIndex);
		}
	}
}
