#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"

#include "Engine/World.h"

struct FTransform;
class FImGuiRenderer;
using FShadowPreviewQuery = std::function<bool(const ULightComponent*, FTexture2DArray*&, TArray<FLightShadowRenderData>&)>;

using DetailsSelectionCallback = std::function<void(UActorComponent*)>;
using DetailsDeleteComponentCallback = std::function<void(UActorComponent*)>;

class FDetailsPanel : public IEditorPanel
{
public:
	FDetailsPanel() = default;
	~FDetailsPanel();

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;

	void SelectComponent(UActorComponent* Component);
	void DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent);

	const char* GetPanelName() const override { return "Details"; }

	void SetTarget(AActor* InTargetActor) 
	{ 
		TargetActor = InTargetActor;
		TargetComponent = InTargetActor ? InTargetActor->GetRootComponent() : nullptr;
	}
	void SetTargetComponent(UActorComponent* InTargetComponent) { TargetComponent = InTargetComponent; }

	void SetSelectionCallback(DetailsSelectionCallback InCallback) { Callback = InCallback; }
	void SetDeleteComponentCallback(DetailsDeleteComponentCallback InCallback) { DeleteCallback = InCallback; }

	void SetWorld(UWorld* InWorld) { World = InWorld; }
	const ULightComponent* GetSelectedLight() const;
	// 실행 그래프 디버그 대상은 Details에서 선택한 스켈레탈 메시를 우선한다.
	UActorComponent* GetSelectedComponent() const;
	// 에디터 매니저의 그래프 디버그 창 열기 동작을 Details 버튼과 연결한다.
	void SetOpenAnimGraphDebugCallback(std::function<void()> Callback);
	void SetShadowPreviewQuery(FShadowPreviewQuery InQuery) { QueryShadowPreview = std::move(InQuery); }
	void SetImGuiRenderer(FImGuiRenderer* InRenderer) { ImGuiRenderer = InRenderer; }

	ImFont* GetCustomFont() { return CustomFont; }

private:
	void DrawShadowPreview();
	// 선택 메시의 그래프 지정과 실행 디버그 창 열기 UI를 그린다.
	void DrawAnimGraphSlot(class USkeletalMeshComponent* Component);
	FShadowPreviewQuery QueryShadowPreview;
	// Details 패널이 에디터 매니저를 직접 소유하지 않고 디버그 창 열기를 요청하는 콜백이다.
	std::function<void()> mOpenAnimGraphDebug{};
	// 선택 메시의 그래프 지정에 실패했을 때 표시할 메시지이다.
	FString mAnimGraphError{};
	FImGuiRenderer* ImGuiRenderer = nullptr;
	float ShadowPreviewContrast = 32.0f;
	void DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent);
	void HandleComponentDrop(AActor* Actor, UActorComponent*& Selection);
	uint32 RootDropCandidateId = 0;

	UWorld* World = nullptr;
	ImFont* CustomFont = nullptr;

	AActor* TargetActor = nullptr;
	UActorComponent* TargetComponent = nullptr;
	DetailsSelectionCallback Callback;
	DetailsDeleteComponentCallback DeleteCallback;

	UActorComponent* PendingDeleteComponent = nullptr;
	UActorComponent* EditingNameComponent = nullptr;
	char ComponentNameEditBuffer[256] = {};
	bool bFocusNameEdit = false;
	bool bIsEditingName = false;

	uint32 PendingDroppedComponentId = 0;
	USceneComponent* PendingDragComponent = nullptr;
};
