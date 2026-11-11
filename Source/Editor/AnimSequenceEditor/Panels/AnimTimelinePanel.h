#pragma once

#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"
#include "Core/Types.h"

class FAnimSequenceEditor;
class UAnimSequenceBase;

// UE Animation 에디터 아래쪽의 Timeline.
//   위: 프레임 눈금(Ruler) + 재생 헤드, 가운데: 트랙(Notify 트랙 자리), 아래: 재생 컨트롤 줄
// 눈금·트랙 영역을 드래그하면 재생 헤드를 옮기고(스크러빙), 휠로 확대/축소, 가운데·오른쪽 버튼 드래그로 이동합니다.
class FAnimTimelinePanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FAnimTimelinePanel(FAnimSequenceEditor& InEditor);

protected:
	void DrawContent() override;

private:
	// 눈금·트랙·재생 헤드와 마우스 조작
	void DrawTimeArea(float Height);

	// 아래쪽 재생 컨트롤 줄: 처음/이전 프레임/역재생/재생/다음 프레임/끝/루프/배속/현재 위치
	void DrawTransportBar();

	// 애니메이션이 바뀌면 보이는 범위를 전체 길이로 되돌립니다.
	void ResetViewIfSequenceChanged();

	////////////////////////////////////////////////////////////
	// 프레임 ↔ 픽셀 변환 (보이는 범위 [ViewStartFrame, ViewEndFrame]를 TimeAreaMinX..MaxX에 대응)
	////////////////////////////////////////////////////////////

	float FrameToX(float Frame) const;
	float XToFrame(float X) const;

	FAnimSequenceEditor& AnimEditor;

	// 보이는 프레임 범위 (프레임 단위, 소수 허용)
	float ViewStartFrame = 0.0f;
	float ViewEndFrame = 1.0f;
	// 이번 프레임의 시간 영역 픽셀 범위
	float TimeAreaMinX = 0.0f;
	float TimeAreaMaxX = 1.0f;

	const UAnimSequenceBase* ViewSequence = nullptr;

	////////////////////////////////////////////////////////////
	// Notify 편집 (UE Notify 트랙)
	//   빈 곳 우클릭: Add Notify / Add Notify State / Add Track    Notify 우클릭: Rename / Delete
	//   좌클릭: 선택, 드래그: 이동(프레임 스냅, Shift면 해제), NotifyState 오른쪽 끝 드래그: 길이
	//   더블클릭: 이름 변경, Delete: 삭제
	////////////////////////////////////////////////////////////

	enum class EDragMode : uint8 { None, Scrub, MoveNotify, ResizeNotify };
	enum class ENamePopupMode : uint8 { AddNotify, AddNotifyState, Rename };

	// 마우스 아래 Notify 인덱스(없으면 INDEX_NONE). NotifyState의 오른쪽 끝이면 bOutResizeHandle.
	int32 HitTestNotify(const ImVec2& Mouse, bool& bOutResizeHandle) const;
	float GetRowTop(int32 Track) const { return TracksTop + Track * RowHeight; }
	// Y 위치의 트랙 줄 번호 (보이는 줄 범위로 제한)
	int32 TrackAtY(float Y) const;
	int32 GetNumTrackRows() const;
	// 시간을 프레임에 맞춥니다. bSnap이 false면 그대로 둡니다.
	float SnapTime(float Time, bool bSnap) const;
	float MouseTime(float X) const;
	bool IsValidNotify(int32 Index) const;

	void AddNotify(const FString& Name, float Time, int32 Track, bool bState);
	void DeleteNotify(int32 Index);
	void OpenNamePopup(ENamePopupMode Mode, int32 RenameTarget = INDEX_NONE);
	// 이 애니메이션에서 쓰인 Notify 이름 (메뉴에서 다시 고를 수 있게)
	TArray<FString> CollectNotifyNames() const;

	void DrawTrackRows(ImDrawList* DrawList, float OriginX, float Width);
	void DrawNotifies(ImDrawList* DrawList);
	void DrawContextMenu();
	void DrawNamePopup();

	EDragMode DragMode = EDragMode::None;
	int32 SelectedNotify = INDEX_NONE;
	// Add Track으로 늘린 줄 수. 실제 줄 수는 Notify의 TrackIndex와 이 값 중 큰 쪽입니다.
	int32 NumTrackRows = 1;

	// 드래그를 시작할 때의 값
	float DragStartMouseTime = 0.0f;
	float DragStartTrigger = 0.0f;
	float DragStartDuration = 0.0f;

	// 우클릭 메뉴를 연 위치
	float ContextTime = 0.0f;
	int32 ContextTrack = 0;
	int32 ContextNotify = INDEX_NONE;

	// 이름 입력 팝업
	bool bOpenNamePopup = false;
	ENamePopupMode NamePopupMode = ENamePopupMode::AddNotify;
	int32 RenameTarget = INDEX_NONE;
	char NameBuffer[128] = {};

	// 이번 프레임의 트랙 영역 배치
	float TracksTop = 0.0f;
	float RowHeight = 1.0f;
};
