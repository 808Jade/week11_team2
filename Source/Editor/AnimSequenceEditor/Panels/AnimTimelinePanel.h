#pragma once

#include "Editor/SkeletalMeshEditor/Panels/SkeletalMeshEditorPanel.h"

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
	bool bScrubbing = false;
};
