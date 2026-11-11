#include "EnginePCH.h"
#include "AnimTimelinePanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Animation/AnimSequenceBase.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	// 기본 폰트에 ⏮ ▶ 같은 기호가 없을 수 있으므로 아이콘은 도형으로 직접 그립니다.
	enum class ETransportIcon { ToStart, PrevFrame, PlayReverse, Pause, Play, NextFrame, ToEnd, Loop };

	// UE 재생 컨트롤처럼 배경 없는 아이콘 버튼. 기본은 밝은 회색, 켜진 상태(재생 중·루프)는 강조색.
	const ImU32 IconColor = IM_COL32(200, 200, 200, 255);
	const ImU32 IconHoverColor = IM_COL32(255, 255, 255, 255);
	const ImU32 IconActiveColor = IM_COL32(56, 160, 255, 255);

	void Triangle(ImDrawList* DrawList, const ImVec2& C, float W, float H, bool bRight, bool bFilled, ImU32 Color)
	{
		const float D = bRight ? 1.0f : -1.0f;
		const ImVec2 A(C.x + D * W * 0.5f, C.y);
		const ImVec2 B(C.x - D * W * 0.5f, C.y - H * 0.5f);
		const ImVec2 E(C.x - D * W * 0.5f, C.y + H * 0.5f);
		if (bFilled) DrawList->AddTriangleFilled(A, B, E, Color);
		else DrawList->AddTriangle(A, B, E, Color, 1.5f);
	}

	void Bar(ImDrawList* DrawList, float X, float Y, float W, float H, ImU32 Color)
	{
		DrawList->AddRectFilled(ImVec2(X - W * 0.5f, Y - H * 0.5f), ImVec2(X + W * 0.5f, Y + H * 0.5f), Color);
	}

	void DrawIcon(ImDrawList* DrawList, ETransportIcon Icon, const ImVec2& C, float S, ImU32 Color)
	{
		const float BarW = ImMax(2.0f, std::round(S * 0.14f));
		switch (Icon)
		{
		case ETransportIcon::ToStart:      // |◀
			Bar(DrawList, C.x - S * 0.38f, C.y, BarW, S * 0.8f, Color);
			Triangle(DrawList, ImVec2(C.x + S * 0.08f, C.y), S * 0.62f, S * 0.8f, false, true, Color);
			break;
		case ETransportIcon::PrevFrame:    // ◁|  (한 프레임 뒤로: 속 빈 삼각형)
			Triangle(DrawList, ImVec2(C.x - S * 0.1f, C.y), S * 0.55f, S * 0.75f, false, false, Color);
			Bar(DrawList, C.x + S * 0.32f, C.y, BarW, S * 0.8f, Color);
			break;
		case ETransportIcon::PlayReverse:  // ◀
			Triangle(DrawList, C, S * 0.7f, S * 0.85f, false, true, Color);
			break;
		case ETransportIcon::Pause:        // ||
			Bar(DrawList, C.x - S * 0.17f, C.y, BarW * 1.4f, S * 0.8f, Color);
			Bar(DrawList, C.x + S * 0.17f, C.y, BarW * 1.4f, S * 0.8f, Color);
			break;
		case ETransportIcon::Play:         // ▶
			Triangle(DrawList, C, S * 0.7f, S * 0.85f, true, true, Color);
			break;
		case ETransportIcon::NextFrame:    // |▷
			Bar(DrawList, C.x - S * 0.32f, C.y, BarW, S * 0.8f, Color);
			Triangle(DrawList, ImVec2(C.x + S * 0.1f, C.y), S * 0.55f, S * 0.75f, true, false, Color);
			break;
		case ETransportIcon::ToEnd:        // ▶|
			Triangle(DrawList, ImVec2(C.x - S * 0.08f, C.y), S * 0.62f, S * 0.8f, true, true, Color);
			Bar(DrawList, C.x + S * 0.38f, C.y, BarW, S * 0.8f, Color);
			break;
		case ETransportIcon::Loop:         // 둥근 사각 고리 + 화살촉
		{
			const float Thick = ImMax(1.5f, S * 0.11f);
			const ImVec2 Min(C.x - S * 0.42f, C.y - S * 0.3f);
			const ImVec2 Max(C.x + S * 0.42f, C.y + S * 0.3f);
			const float R = S * 0.14f;
			// 아래쪽 가운데를 비워 두고 시계 방향으로 그린 뒤, 끝에 화살촉
			DrawList->PathLineTo(ImVec2(C.x - S * 0.05f, Max.y));
			DrawList->PathArcTo(ImVec2(Min.x + R, Max.y - R), R, IM_PI * 0.5f, IM_PI, 4);
			DrawList->PathArcTo(ImVec2(Min.x + R, Min.y + R), R, IM_PI, IM_PI * 1.5f, 4);
			DrawList->PathArcTo(ImVec2(Max.x - R, Min.y + R), R, IM_PI * 1.5f, IM_PI * 2.0f, 4);
			DrawList->PathArcTo(ImVec2(Max.x - R, Max.y - R), R, 0.0f, IM_PI * 0.5f, 4);
			DrawList->PathLineTo(ImVec2(C.x + S * 0.12f, Max.y));
			DrawList->PathStroke(Color, 0, Thick);
			Triangle(DrawList, ImVec2(C.x + S * 0.02f, Max.y), S * 0.26f, S * 0.3f, false, true, Color);
			break;
		}
		}
	}

	bool TransportButton(const char* Id, ETransportIcon Icon, bool bActive, const char* Tooltip)
	{
		const float Size = ImGui::GetFrameHeight();
		const bool bPressed = ImGui::InvisibleButton(Id, ImVec2(Size, Size));
		const bool bHovered = ImGui::IsItemHovered();
		if (bHovered)
		{
			ImGui::SetTooltip("%s", Tooltip);
		}

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		if (bHovered)
		{
			DrawList->AddRectFilled(Min, Max, IM_COL32(255, 255, 255, ImGui::IsItemActive() ? 40 : 20), 3.0f);
		}
		const ImU32 Color = bActive ? IconActiveColor : (bHovered ? IconHoverColor : IconColor);
		DrawIcon(DrawList, Icon, ImVec2((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f), Size * 0.55f, Color);
		return bPressed;
	}

	void FormatSpeed(char* Out, size_t Size, float Speed)
	{
		std::snprintf(Out, Size, Speed < 1.0f ? "x%.2g" : "x%.1f", Speed);
	}
}

FAnimTimelinePanel::FAnimTimelinePanel(FAnimSequenceEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Timeline")
	, AnimEditor(InEditor)
{
}

void FAnimTimelinePanel::DrawContent()
{
	ResetViewIfSequenceChanged();

	const float BarHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y + ImGui::GetStyle().ItemSpacing.y;
	const float TimeHeight = ImMax(0.0f, ImGui::GetContentRegionAvail().y - BarHeight);
	DrawTimeArea(TimeHeight);
	DrawTransportBar();
}

void FAnimTimelinePanel::ResetViewIfSequenceChanged()
{
	const UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	if (Sequence == ViewSequence)
	{
		return;
	}
	ViewSequence = Sequence;
	// 전체 길이 + 양쪽 약간의 여백 (UE처럼 처음과 끝 프레임이 가장자리에 붙지 않게)
	const float Frames = static_cast<float>(ImMax(1, AnimEditor.GetNumberOfFrames()));
	const float Margin = Frames * 0.02f;
	ViewStartFrame = -Margin;
	ViewEndFrame = Frames + Margin;
	DragMode = EDragMode::None;
	SelectedNotify = INDEX_NONE;
	ContextNotify = INDEX_NONE;
	RenameTarget = INDEX_NONE;
	NumTrackRows = 1;
}

float FAnimTimelinePanel::FrameToX(float Frame) const
{
	const float Span = ImMax(1.e-4f, ViewEndFrame - ViewStartFrame);
	return TimeAreaMinX + (Frame - ViewStartFrame) / Span * (TimeAreaMaxX - TimeAreaMinX);
}

float FAnimTimelinePanel::XToFrame(float X) const
{
	const float Width = ImMax(1.0f, TimeAreaMaxX - TimeAreaMinX);
	return ViewStartFrame + (X - TimeAreaMinX) / Width * (ViewEndFrame - ViewStartFrame);
}

void FAnimTimelinePanel::DrawTimeArea(float Height)
{
	if (Height <= 1.0f)
	{
		return;
	}

	ImGui::BeginChild("##TimeArea", ImVec2(0.0f, Height), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const float RulerHeight = ImGui::GetFrameHeight();
	const float LabelWidth = ImMin(180.0f, Avail.x * 0.3f);
	RowHeight = ImGui::GetFrameHeight() + 4.0f;

	TimeAreaMinX = Origin.x + LabelWidth;
	TimeAreaMaxX = ImMax(TimeAreaMinX + 1.0f, Origin.x + Avail.x - 8.0f);
	const float Top = Origin.y;
	const float Bottom = Origin.y + Avail.y;
	const float TrackTop = Top + RulerHeight;
	TracksTop = TrackTop;

	const int32 NumFrames = AnimEditor.GetNumberOfFrames();
	const float Rate = AnimEditor.GetFrameRate();
	const float CurrentFrame = Rate > 0.0f ? AnimEditor.GetPlaybackTime() * Rate : 0.0f;
	UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	const bool bCanEdit = Sequence && Rate > 0.0f && NumFrames > 0;
	if (!IsValidNotify(SelectedNotify)) SelectedNotify = INDEX_NONE;

	const ImU32 BgColor = IM_COL32(32, 32, 32, 255);
	const ImU32 LabelBgColor = IM_COL32(40, 40, 40, 255);
	const ImU32 RulerBgColor = IM_COL32(26, 26, 26, 255);
	const ImU32 OutOfRangeColor = IM_COL32(0, 0, 0, 70);
	const ImU32 MajorTickColor = IM_COL32(150, 150, 150, 255);
	const ImU32 MinorTickColor = IM_COL32(90, 90, 90, 255);
	const ImU32 GridColor = IM_COL32(255, 255, 255, 14);
	const ImU32 PlayheadColor = IM_COL32(230, 60, 50, 255);

	// 배경: 왼쪽 트랙 이름 열 / 오른쪽 시간 영역
	DrawList->AddRectFilled(ImVec2(Origin.x, Top), ImVec2(TimeAreaMinX, Bottom), LabelBgColor);
	DrawList->AddRectFilled(ImVec2(TimeAreaMinX, Top), ImVec2(Origin.x + Avail.x, Bottom), BgColor);
	DrawList->AddRectFilled(ImVec2(TimeAreaMinX, Top), ImVec2(Origin.x + Avail.x, TrackTop), RulerBgColor);

	DrawTrackRows(DrawList, Origin.x, Avail.x);
	DrawList->AddText(ImVec2(Origin.x + 8.0f, Top + (RulerHeight - ImGui::GetTextLineHeight()) * 0.5f),
		ImGui::GetColorU32(ImGuiCol_TextDisabled), "Tracks");
	DrawList->AddLine(ImVec2(TimeAreaMinX, Top), ImVec2(TimeAreaMinX, Bottom), IM_COL32(0, 0, 0, 160));

	// 트랙 추가 버튼 (트랙 이름 열 머리)
	if (bCanEdit)
	{
		const float ButtonSize = RulerHeight - 4.0f;
		ImGui::SetCursorScreenPos(ImVec2(TimeAreaMinX - ButtonSize - 4.0f, Top + 2.0f));
		if (ImGui::Button("+##AddNotifyTrack", ImVec2(ButtonSize, ButtonSize)))
		{
			NumTrackRows = GetNumTrackRows() + 1;
		}
		ImGui::SetItemTooltip("Add Notify Track");
	}

	// 시간 영역 마우스 처리: 눈금·트랙 전체를 하나의 버튼으로 잡습니다.
	ImGui::SetCursorScreenPos(ImVec2(TimeAreaMinX, Top));
	ImGui::InvisibleButton("##TimeInput", ImVec2(ImMax(1.0f, TimeAreaMaxX - TimeAreaMinX), ImMax(1.0f, Avail.y)),
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bHovered = ImGui::IsItemHovered();
	const ImGuiIO& IO = ImGui::GetIO();
	const bool bMouseInTracks = IO.MousePos.y >= TrackTop;

	bool bHoverResizeHandle = false;
	const int32 HoveredNotify = bCanEdit && bHovered && bMouseInTracks ? HitTestNotify(IO.MousePos, bHoverResizeHandle) : INDEX_NONE;
	if (HoveredNotify != INDEX_NONE && bHoverResizeHandle && DragMode == EDragMode::None)
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}

	// 휠: 마우스 위치를 기준으로 확대/축소 (그 지점의 프레임이 제자리에 남도록)
	if (bHovered && IO.MouseWheel != 0.0f)
	{
		const float Pivot = XToFrame(IO.MousePos.x);
		const float Scale = IO.MouseWheel > 0.0f ? 0.8f : 1.25f;
		const float NewSpan = ImClamp((ViewEndFrame - ViewStartFrame) * Scale, 2.0f, ImMax(10.0f, NumFrames * 4.0f));
		const float T = (Pivot - ViewStartFrame) / ImMax(1.e-4f, ViewEndFrame - ViewStartFrame);
		ViewStartFrame = Pivot - NewSpan * T;
		ViewEndFrame = ViewStartFrame + NewSpan;
	}

	// 가운데·오른쪽 버튼 드래그: 좌우 이동
	if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
	{
		const float FramesPerPixel = (ViewEndFrame - ViewStartFrame) / ImMax(1.0f, TimeAreaMaxX - TimeAreaMinX);
		ViewStartFrame -= IO.MouseDelta.x * FramesPerPixel;
		ViewEndFrame -= IO.MouseDelta.x * FramesPerPixel;
	}

	// 오른쪽 버튼을 거의 움직이지 않고 떼면 Notify 메뉴 (끌었다면 위의 화면 이동입니다)
	const float ClickThreshold = IO.MouseDragThreshold;
	if (bCanEdit && bHovered && bMouseInTracks && ImGui::IsMouseReleased(ImGuiMouseButton_Right)
		&& IO.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < ClickThreshold * ClickThreshold)
	{
		bool bIgnored = false;
		ContextNotify = HitTestNotify(IO.MousePos, bIgnored);
		if (ContextNotify != INDEX_NONE) SelectedNotify = ContextNotify;
		ContextTime = SnapTime(MouseTime(IO.MousePos.x), true);
		ContextTrack = TrackAtY(IO.MousePos.y);
		ImGui::OpenPopup("##NotifyContext");
	}

	// 왼쪽 버튼: Notify 위면 선택·이동·길이 조절, 빈 곳이면 스크러빙. 눈금 줄은 항상 스크러빙입니다.
	if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		bool bResizeHandle = false;
		const int32 Hit = bCanEdit && bMouseInTracks ? HitTestNotify(IO.MousePos, bResizeHandle) : INDEX_NONE;
		if (Hit != INDEX_NONE)
		{
			SelectedNotify = Hit;
			const FAnimNotifyEvent& Notify = Sequence->Notifies[Hit];
			DragStartMouseTime = MouseTime(IO.MousePos.x);
			DragStartTrigger = Notify.TriggerTime;
			DragStartDuration = Notify.Duration;
			DragMode = bResizeHandle ? EDragMode::ResizeNotify : EDragMode::MoveNotify;
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				DragMode = EDragMode::None;
				OpenNamePopup(ENamePopupMode::Rename, Hit);
			}
		}
		else
		{
			if (bMouseInTracks) SelectedNotify = INDEX_NONE;
			DragMode = EDragMode::Scrub;
		}
	}
	if (DragMode != EDragMode::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		DragMode = EDragMode::None;
	}

	const float Length = AnimEditor.GetPlaybackLength();
	const bool bSnap = !IO.KeyShift;
	switch (DragMode)
	{
	case EDragMode::Scrub:
		if (Rate > 0.0f && NumFrames > 0)
		{
			const float Frame = ImClamp(std::round(XToFrame(IO.MousePos.x)), 0.0f, static_cast<float>(NumFrames));
			if (std::abs(Frame - CurrentFrame) > 1.e-3f || AnimEditor.IsPlaying())
			{
				AnimEditor.SetPlaybackTime(Frame / Rate);
			}
		}
		break;
	case EDragMode::MoveNotify:
		// 클릭만 하면(선택) 위치를 바꾸지 않습니다. 끌기 시작한 뒤부터 따라갑니다.
		if (IsValidNotify(SelectedNotify) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			FAnimNotifyEvent& Notify = Sequence->Notifies[SelectedNotify];
			const float Time = SnapTime(DragStartTrigger + MouseTime(IO.MousePos.x) - DragStartMouseTime, bSnap);
			Notify.TriggerTime = ImClamp(Time, 0.0f, ImMax(0.0f, Length - Notify.Duration));
			Notify.TrackIndex = TrackAtY(IO.MousePos.y);
		}
		break;
	case EDragMode::ResizeNotify:
		if (IsValidNotify(SelectedNotify) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			FAnimNotifyEvent& Notify = Sequence->Notifies[SelectedNotify];
			// 최소 1프레임. 0이 되면 NotifyState가 Notify로 바뀌므로 막습니다.
			const float MinDuration = 1.0f / Rate;
			const float End = ImClamp(SnapTime(MouseTime(IO.MousePos.x), bSnap), Notify.TriggerTime + MinDuration,
				ImMax(Notify.TriggerTime + MinDuration, Length));
			Notify.Duration = End - Notify.TriggerTime;
		}
		break;
	case EDragMode::None:
		break;
	}

	// Delete: 선택한 Notify 삭제 (이 패널에 포커스가 있고 글자 입력 중이 아닐 때)
	if (bCanEdit && IsValidNotify(SelectedNotify) && !IO.WantTextInput
		&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		DeleteNotify(SelectedNotify);
	}

	DrawList->PushClipRect(ImVec2(TimeAreaMinX, Top), ImVec2(Origin.x + Avail.x, Bottom), true);

	// 애니메이션 범위 밖(0 이전, 마지막 프레임 이후)은 어둡게
	const float StartX = FrameToX(0.0f);
	const float EndX = FrameToX(static_cast<float>(NumFrames));
	if (StartX > TimeAreaMinX) DrawList->AddRectFilled(ImVec2(TimeAreaMinX, Top), ImVec2(StartX, Bottom), OutOfRangeColor);
	if (EndX < Origin.x + Avail.x) DrawList->AddRectFilled(ImVec2(EndX, Top), ImVec2(Origin.x + Avail.x, Bottom), OutOfRangeColor);

	// 눈금 간격: 숫자 라벨이 최소 60px 떨어지도록 1, 2, 5, 10, 20, 50 ... 중에서 고릅니다.
	const float PixelsPerFrame = (TimeAreaMaxX - TimeAreaMinX) / ImMax(1.e-4f, ViewEndFrame - ViewStartFrame);
	int32 MajorStep = 1;
	for (int32 Base = 1; MajorStep * PixelsPerFrame < 60.0f && Base < 1000000; Base *= 10)
	{
		for (const int32 Mul : { 1, 2, 5 })
		{
			MajorStep = Base * Mul;
			if (MajorStep * PixelsPerFrame >= 60.0f) break;
		}
	}
	const int32 MinorStep = MajorStep >= 5 && MajorStep % 5 == 0 ? MajorStep / 5 : 1;
	const bool bDrawMinor = MinorStep * PixelsPerFrame >= 5.0f && MinorStep < MajorStep;

	const int32 FirstTick = static_cast<int32>(std::floor(ViewStartFrame / MinorStep)) * MinorStep;
	for (int32 Frame = FirstTick; static_cast<float>(Frame) <= ViewEndFrame; Frame += MinorStep)
	{
		const float X = FrameToX(static_cast<float>(Frame));
		const bool bMajor = Frame % MajorStep == 0;
		if (!bMajor && !bDrawMinor) continue;

		const float TickHeight = bMajor ? RulerHeight * 0.45f : RulerHeight * 0.22f;
		DrawList->AddLine(ImVec2(X, TrackTop - TickHeight), ImVec2(X, TrackTop), bMajor ? MajorTickColor : MinorTickColor);
		if (bMajor)
		{
			DrawList->AddLine(ImVec2(X, TrackTop), ImVec2(X, Bottom), GridColor);
			char Label[16];
			std::snprintf(Label, sizeof(Label), "%d", Frame);
			DrawList->AddText(ImVec2(X + 3.0f, Top + 1.0f), MajorTickColor, Label);
		}
	}
	DrawList->AddLine(ImVec2(TimeAreaMinX, TrackTop), ImVec2(Origin.x + Avail.x, TrackTop), IM_COL32(0, 0, 0, 160));

	DrawNotifies(DrawList);

	// 재생 헤드: 눈금 위 라벨 상자 + 아래로 이어지는 세로선 (UE의 빨간 재생 헤드)
	const float HeadX = FrameToX(CurrentFrame);
	DrawList->AddLine(ImVec2(HeadX, TrackTop), ImVec2(HeadX, Bottom), PlayheadColor, 1.5f);
	{
		char Label[16];
		std::snprintf(Label, sizeof(Label), "%d", static_cast<int32>(std::floor(CurrentFrame + 1.e-3f)));
		const ImVec2 TextSize = ImGui::CalcTextSize(Label);
		const float BoxHalf = ImMax(TextSize.x * 0.5f + 4.0f, 8.0f);
		const ImVec2 BoxMin(HeadX - BoxHalf, Top + 1.0f);
		const ImVec2 BoxMax(HeadX + BoxHalf, TrackTop - 3.0f);
		DrawList->AddRectFilled(BoxMin, BoxMax, PlayheadColor, 2.0f);
		DrawList->AddTriangleFilled(ImVec2(HeadX - 4.0f, BoxMax.y), ImVec2(HeadX + 4.0f, BoxMax.y),
			ImVec2(HeadX, TrackTop), PlayheadColor);
		DrawList->AddText(ImVec2(HeadX - TextSize.x * 0.5f, BoxMin.y + (BoxMax.y - BoxMin.y - TextSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 255), Label);
	}

	DrawList->PopClipRect();

	// 마지막 아이템은 여전히 ##TimeInput 버튼입니다. Notify 위면 그 정보를, 아니면 조작법을 잠시 머무를 때 보여 줍니다.
	if (DragMode == EDragMode::None)
	{
		if (HoveredNotify != INDEX_NONE)
		{
			const FAnimNotifyEvent& Notify = Sequence->Notifies[HoveredNotify];
			if (Notify.IsNotifyState())
				ImGui::SetItemTooltip("%s\nFrame %.1f - %.1f  (%.3fs - %.3fs)", Notify.NotifyName.c_str(),
					Notify.TriggerTime * Rate, Notify.GetEndTriggerTime() * Rate, Notify.TriggerTime, Notify.GetEndTriggerTime());
			else
				ImGui::SetItemTooltip("%s\nFrame %.1f  (%.3fs)", Notify.NotifyName.c_str(), Notify.TriggerTime * Rate, Notify.TriggerTime);
		}
		else
		{
			ImGui::SetItemTooltip("Drag: scrub   Wheel: zoom   Right/Middle drag: pan\nRight click track: add notify");
		}
	}

	DrawContextMenu();
	if (bOpenNamePopup)
	{
		bOpenNamePopup = false;
		ImGui::OpenPopup("##NotifyName");
	}
	DrawNamePopup();

	ImGui::EndChild();
}

////////////////////////////////////////////////////////////
// Notify 편집
////////////////////////////////////////////////////////////

bool FAnimTimelinePanel::IsValidNotify(int32 Index) const
{
	const UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	return Sequence && Index >= 0 && Index < Sequence->Notifies.Num();
}

int32 FAnimTimelinePanel::GetNumTrackRows() const
{
	int32 Rows = ImMax(1, NumTrackRows);
	if (const UAnimSequenceBase* Sequence = AnimEditor.GetSequence())
	{
		for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
			Rows = ImMax(Rows, Notify.TrackIndex + 1);
	}
	return Rows;
}

int32 FAnimTimelinePanel::TrackAtY(float Y) const
{
	const int32 Row = static_cast<int32>(std::floor((Y - TracksTop) / ImMax(1.0f, RowHeight)));
	return ImClamp(Row, 0, GetNumTrackRows() - 1);
}

float FAnimTimelinePanel::SnapTime(float Time, bool bSnap) const
{
	const float Rate = AnimEditor.GetFrameRate();
	return bSnap && Rate > 0.0f ? std::round(Time * Rate) / Rate : Time;
}

float FAnimTimelinePanel::MouseTime(float X) const
{
	const float Rate = AnimEditor.GetFrameRate();
	return Rate > 0.0f ? XToFrame(X) / Rate : 0.0f;
}

int32 FAnimTimelinePanel::HitTestNotify(const ImVec2& Mouse, bool& bOutResizeHandle) const
{
	bOutResizeHandle = false;
	const UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	const float Rate = AnimEditor.GetFrameRate();
	if (!Sequence || Rate <= 0.0f) return INDEX_NONE;

	// 나중에 그린 것이 위에 있으므로 뒤에서부터 찾습니다. 선택한 Notify는 맨 위에 그리므로 먼저 봅니다.
	auto Test = [&](int32 Index, bool& bHandle)
	{
		const FAnimNotifyEvent& Notify = Sequence->Notifies[Index];
		const float RowTop = GetRowTop(Notify.TrackIndex);
		if (Mouse.y < RowTop || Mouse.y >= RowTop + RowHeight) return false;
		const float X0 = FrameToX(Notify.TriggerTime * Rate);
		if (!Notify.IsNotifyState()) return std::abs(Mouse.x - X0) <= 6.0f;
		const float X1 = ImMax(FrameToX(Notify.GetEndTriggerTime() * Rate), X0 + 4.0f);
		if (Mouse.x >= X1 - 5.0f && Mouse.x <= X1 + 3.0f) { bHandle = true; return true; }
		return Mouse.x >= X0 - 3.0f && Mouse.x < X1;
	};
	if (IsValidNotify(SelectedNotify) && Test(SelectedNotify, bOutResizeHandle)) return SelectedNotify;
	for (int32 Index = Sequence->Notifies.Num() - 1; Index >= 0; --Index)
	{
		bOutResizeHandle = false;
		if (Index != SelectedNotify && Test(Index, bOutResizeHandle)) return Index;
	}
	bOutResizeHandle = false;
	return INDEX_NONE;
}

void FAnimTimelinePanel::AddNotify(const FString& Name, float Time, int32 Track, bool bState)
{
	UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	const float Rate = AnimEditor.GetFrameRate();
	if (!Sequence || Rate <= 0.0f || Name.empty()) return;

	const float Length = AnimEditor.GetPlaybackLength();
	FAnimNotifyEvent Notify;
	Notify.NotifyName = Name;
	Notify.TrackIndex = ImMax(0, Track);
	Notify.TriggerTime = ImClamp(Time, 0.0f, Length);
	if (bState)
	{
		// 처음 길이는 10프레임. 끝을 넘으면 시작을 앞으로 당기고, 그래도 넘으면 길이를 줄입니다.
		const float DefaultDuration = 10.0f / Rate;
		Notify.TriggerTime = ImMax(0.0f, ImMin(Notify.TriggerTime, Length - DefaultDuration));
		Notify.Duration = ImMax(1.0f / Rate, ImMin(DefaultDuration, Length - Notify.TriggerTime));
	}
	Sequence->Notifies.Add(Notify);
	SelectedNotify = Sequence->Notifies.Num() - 1;
	NumTrackRows = ImMax(NumTrackRows, Notify.TrackIndex + 1);
}

void FAnimTimelinePanel::DeleteNotify(int32 Index)
{
	if (!IsValidNotify(Index)) return;
	AnimEditor.GetSequence()->Notifies.RemoveAt(static_cast<uint32>(Index));
	SelectedNotify = INDEX_NONE;
	ContextNotify = INDEX_NONE;
	RenameTarget = INDEX_NONE;
	DragMode = EDragMode::None;
}

void FAnimTimelinePanel::OpenNamePopup(ENamePopupMode Mode, int32 InRenameTarget)
{
	NamePopupMode = Mode;
	RenameTarget = Mode == ENamePopupMode::Rename ? InRenameTarget : INDEX_NONE;
	NameBuffer[0] = '\0';
	if (Mode == ENamePopupMode::Rename && IsValidNotify(RenameTarget))
	{
		std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", AnimEditor.GetSequence()->Notifies[RenameTarget].NotifyName.c_str());
	}
	bOpenNamePopup = true;
}

TArray<FString> FAnimTimelinePanel::CollectNotifyNames() const
{
	TArray<FString> Names;
	if (const UAnimSequenceBase* Sequence = AnimEditor.GetSequence())
	{
		for (const FAnimNotifyEvent& Notify : Sequence->Notifies)
		{
			if (std::find(Names.begin(), Names.end(), Notify.NotifyName) == Names.end())
				Names.Add(Notify.NotifyName);
		}
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}

void FAnimTimelinePanel::DrawTrackRows(ImDrawList* DrawList, float OriginX, float Width)
{
	const int32 Rows = GetNumTrackRows();
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const float RowTop = GetRowTop(Row);
		DrawList->AddRectFilled(ImVec2(OriginX, RowTop), ImVec2(OriginX + Width, RowTop + RowHeight),
			IM_COL32(255, 255, 255, Row % 2 == 0 ? 8 : 0));
		char Label[32];
		if (Row == 0) std::snprintf(Label, sizeof(Label), "Notifies");
		else std::snprintf(Label, sizeof(Label), "Notifies %d", Row + 1);
		DrawList->AddText(ImVec2(OriginX + 8.0f, RowTop + (RowHeight - ImGui::GetTextLineHeight()) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), Label);
		DrawList->AddLine(ImVec2(OriginX, RowTop + RowHeight), ImVec2(OriginX + Width, RowTop + RowHeight),
			IM_COL32(0, 0, 0, 120));
	}
}

void FAnimTimelinePanel::DrawNotifies(ImDrawList* DrawList)
{
	const UAnimSequenceBase* Sequence = AnimEditor.GetSequence();
	const float Rate = AnimEditor.GetFrameRate();
	if (!Sequence || Rate <= 0.0f) return;

	const ImU32 NotifyColor = IM_COL32(240, 160, 60, 255);
	const ImU32 StateColor = IM_COL32(70, 130, 210, 220);
	const ImU32 SelectedOutline = IM_COL32(255, 255, 255, 255);
	const ImU32 TextColor = IM_COL32(235, 235, 235, 255);
	const float TextHeight = ImGui::GetTextLineHeight();

	auto Draw = [&](int32 Index)
	{
		const FAnimNotifyEvent& Notify = Sequence->Notifies[Index];
		const bool bSelected = Index == SelectedNotify;
		const float RowTop = GetRowTop(Notify.TrackIndex);
		const float CenterY = RowTop + RowHeight * 0.5f;
		const float X0 = FrameToX(Notify.TriggerTime * Rate);
		const char* Name = Notify.NotifyName.c_str();

		if (Notify.IsNotifyState())
		{
			// NotifyState: 구간 막대 + 오른쪽 끝 손잡이, 이름은 막대 안에
			const float X1 = ImMax(FrameToX(Notify.GetEndTriggerTime() * Rate), X0 + 4.0f);
			const ImVec2 Min(X0, RowTop + 3.0f);
			const ImVec2 Max(X1, RowTop + RowHeight - 3.0f);
			DrawList->AddRectFilled(Min, Max, StateColor, 3.0f);
			DrawList->AddRectFilled(ImVec2(X1 - 3.0f, Min.y), Max, IM_COL32(255, 255, 255, 60), 3.0f);
			if (bSelected) DrawList->AddRect(Min, Max, SelectedOutline, 3.0f, 0, 1.5f);
			DrawList->PushClipRect(Min, ImVec2(X1 - 4.0f, Max.y), true);
			DrawList->AddText(ImVec2(X0 + 5.0f, CenterY - TextHeight * 0.5f), TextColor, Name);
			DrawList->PopClipRect();
		}
		else
		{
			// Notify: 마름모 + 오른쪽에 이름
			const float R = ImMin(6.0f, RowHeight * 0.35f);
			const ImVec2 Top(X0, CenterY - R), Right(X0 + R, CenterY), Bottom(X0, CenterY + R), Left(X0 - R, CenterY);
			DrawList->AddQuadFilled(Top, Right, Bottom, Left, NotifyColor);
			if (bSelected) DrawList->AddQuad(Top, Right, Bottom, Left, SelectedOutline, 1.5f);
			DrawList->AddText(ImVec2(X0 + R + 4.0f, CenterY - TextHeight * 0.5f), TextColor, Name);
		}
	};

	for (int32 Index = 0; Index < Sequence->Notifies.Num(); ++Index)
	{
		if (Index != SelectedNotify) Draw(Index);
	}
	if (IsValidNotify(SelectedNotify)) Draw(SelectedNotify);
}

void FAnimTimelinePanel::DrawContextMenu()
{
	if (!ImGui::BeginPopup("##NotifyContext"))
	{
		return;
	}

	if (IsValidNotify(ContextNotify))
	{
		ImGui::TextDisabled("%s", AnimEditor.GetSequence()->Notifies[ContextNotify].NotifyName.c_str());
		if (ImGui::MenuItem("Rename..."))
		{
			OpenNamePopup(ENamePopupMode::Rename, ContextNotify);
		}
		if (ImGui::MenuItem("Delete", "Del"))
		{
			DeleteNotify(ContextNotify);
		}
		ImGui::Separator();
	}

	// 이미 쓰인 이름을 다시 고르거나 New...로 새 이름을 입력합니다.
	const TArray<FString> Names = CollectNotifyNames();
	auto AddMenu = [&](const char* Label, bool bState)
	{
		if (!ImGui::BeginMenu(Label)) return;
		for (const FString& Name : Names)
		{
			if (ImGui::MenuItem(Name.c_str()))
			{
				AddNotify(Name, ContextTime, ContextTrack, bState);
			}
		}
		if (!Names.IsEmpty()) ImGui::Separator();
		if (ImGui::MenuItem("New..."))
		{
			OpenNamePopup(bState ? ENamePopupMode::AddNotifyState : ENamePopupMode::AddNotify);
		}
		ImGui::EndMenu();
	};
	AddMenu("Add Notify", false);
	AddMenu("Add Notify State", true);
	ImGui::Separator();
	if (ImGui::MenuItem("Add Track"))
	{
		NumTrackRows = GetNumTrackRows() + 1;
	}
	ImGui::EndPopup();
}

void FAnimTimelinePanel::DrawNamePopup()
{
	if (!ImGui::BeginPopup("##NotifyName"))
	{
		return;
	}

	const char* Title = NamePopupMode == ENamePopupMode::Rename ? "Rename Notify"
		: NamePopupMode == ENamePopupMode::AddNotifyState ? "New Notify State" : "New Notify";
	ImGui::TextUnformatted(Title);
	if (ImGui::IsWindowAppearing())
	{
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(200.0f);
	bool bConfirm = ImGui::InputText("##NotifyNameInput", NameBuffer, sizeof(NameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
	bConfirm |= ImGui::Button("OK");
	ImGui::SameLine();
	const bool bCancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

	// 앞뒤 공백은 지웁니다. 빈 이름은 받지 않습니다.
	FString Name = NameBuffer;
	const size_t First = Name.find_first_not_of(" \t");
	Name = First == FString::npos ? FString() : Name.substr(First, Name.find_last_not_of(" \t") - First + 1);

	if (bConfirm && !Name.empty())
	{
		if (NamePopupMode == ENamePopupMode::Rename)
		{
			if (IsValidNotify(RenameTarget)) AnimEditor.GetSequence()->Notifies[RenameTarget].NotifyName = Name;
		}
		else
		{
			AddNotify(Name, ContextTime, ContextTrack, NamePopupMode == ENamePopupMode::AddNotifyState);
		}
		ImGui::CloseCurrentPopup();
	}
	else if (bCancel)
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void FAnimTimelinePanel::DrawTransportBar()
{
	// UE처럼 아래쪽에 약간 어두운 띠를 깔고, 버튼은 가운데·배속은 오른쪽·현재 위치는 왼쪽에 둡니다.
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float Height = ImGui::GetFrameHeight() + Style.WindowPadding.y;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 24, 24, 255));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Style.WindowPadding.x, Style.WindowPadding.y * 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, Style.ItemSpacing.y));
	ImGui::BeginChild("##TransportBar", ImVec2(0.0f, Height), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	const float Width = ImGui::GetContentRegionAvail().x;
	const float StartX = ImGui::GetCursorPosX();
	const float RowY = ImGui::GetCursorPosY();

	// 왼쪽: 현재 프레임 / 시간
	{
		const float Rate = AnimEditor.GetFrameRate();
		const float Time = AnimEditor.GetPlaybackTime();
		const int32 Frame = Rate > 0.0f ? static_cast<int32>(std::floor(Time * Rate + 1.e-4f)) : 0;
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%d / %d  (%.2fs)", Frame, AnimEditor.GetNumberOfFrames(), Time);
	}

	// 왼쪽: Notify 저장. 바뀐 것이 있을 때만 누를 수 있고 이름 끝에 *가 붙습니다.
	{
		const bool bDirty = AnimEditor.HasUnsavedNotifies();
		ImGui::SameLine();
		ImGui::BeginDisabled(!bDirty);
		if (ImGui::SmallButton(bDirty ? "Save*###SaveNotifies" : "Save###SaveNotifies"))
		{
			AnimEditor.SaveNotifies();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			const FString& Error = AnimEditor.GetLastNotifySaveError();
			if (!Error.empty()) ImGui::SetTooltip("Save Notifies (Ctrl+S)\nLast error: %s", Error.c_str());
			else ImGui::SetTooltip("Save Notifies (Ctrl+S)");
		}
	}

	// 가운데: 재생 버튼 7개
	const bool bPlaying = AnimEditor.IsPlaying();
	const bool bReverse = AnimEditor.IsPlayingReverse();
	const bool bForward = bPlaying && !bReverse;
	constexpr int32 ButtonCount = 7;
	const float ButtonsWidth = ImGui::GetFrameHeight() * ButtonCount + ImGui::GetStyle().ItemSpacing.x * (ButtonCount - 1);
	ImGui::SameLine();
	ImGui::SetCursorPos(ImVec2(StartX + ImMax(0.0f, (Width - ButtonsWidth) * 0.5f), RowY));

	if (TransportButton("##ToStart", ETransportIcon::ToStart, false, "To Start"))
	{
		AnimEditor.JumpToStart();
	}
	ImGui::SameLine();
	if (TransportButton("##PrevFrame", ETransportIcon::PrevFrame, false, "Previous Frame"))
	{
		AnimEditor.StepFrame(-1);
	}
	ImGui::SameLine();
	// 역재생 중이면 일시정지 아이콘으로 바뀌고, 누르면 정지합니다.
	if (TransportButton("##Reverse", bReverse ? ETransportIcon::Pause : ETransportIcon::PlayReverse, false,
		bReverse ? "Pause" : "Play Reverse"))
	{
		bReverse ? AnimEditor.Pause() : AnimEditor.Play(true);
	}
	ImGui::SameLine();
	// 정방향 재생 중이면 일시정지 아이콘으로 바뀝니다.
	if (TransportButton("##Play", bForward ? ETransportIcon::Pause : ETransportIcon::Play, false,
		bForward ? "Pause" : "Play Forward"))
	{
		bForward ? AnimEditor.Pause() : AnimEditor.Play(false);
	}
	ImGui::SameLine();
	if (TransportButton("##NextFrame", ETransportIcon::NextFrame, false, "Next Frame"))
	{
		AnimEditor.StepFrame(1);
	}
	ImGui::SameLine();
	if (TransportButton("##ToEnd", ETransportIcon::ToEnd, false, "To End"))
	{
		AnimEditor.JumpToEnd();
	}
	ImGui::SameLine();
	if (TransportButton("##Loop", ETransportIcon::Loop, AnimEditor.IsLooping(),
		AnimEditor.IsLooping() ? "Loop: On" : "Loop: Off"))
	{
		AnimEditor.SetLooping(!AnimEditor.IsLooping());
	}

	// 오른쪽: 배속 (테두리 없는 콤보, UE "x1.0 ⌄")
	static constexpr float Speeds[] = { 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f };
	char SpeedLabel[16];
	FormatSpeed(SpeedLabel, sizeof(SpeedLabel), AnimEditor.GetPlaySpeed());
	const float ComboWidth = ImGui::CalcTextSize("x0.25").x + ImGui::GetFrameHeight() + Style.FramePadding.x * 2.0f;
	ImGui::SameLine();
	ImGui::SetCursorPos(ImVec2(StartX + ImMax(0.0f, Width - ComboWidth), RowY));
	ImGui::SetNextItemWidth(ComboWidth);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
	if (ImGui::BeginCombo("##Speed", SpeedLabel))
	{
		for (const float Speed : Speeds)
		{
			char Label[16];
			FormatSpeed(Label, sizeof(Label), Speed);
			const bool bSelected = Speed == AnimEditor.GetPlaySpeed();
			if (ImGui::Selectable(Label, bSelected))
			{
				AnimEditor.SetPlaySpeed(Speed);
			}
			if (bSelected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::PopStyleColor(2);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play Speed");

	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}
