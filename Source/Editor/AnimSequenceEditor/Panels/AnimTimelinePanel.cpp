#include "EnginePCH.h"
#include "AnimTimelinePanel.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"

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
	bScrubbing = false;
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
	const float RowHeight = ImGui::GetFrameHeight();
	const float LabelWidth = ImMin(180.0f, Avail.x * 0.3f);

	TimeAreaMinX = Origin.x + LabelWidth;
	TimeAreaMaxX = ImMax(TimeAreaMinX + 1.0f, Origin.x + Avail.x - 8.0f);
	const float Top = Origin.y;
	const float Bottom = Origin.y + Avail.y;
	const float TrackTop = Top + RulerHeight;

	const int32 NumFrames = AnimEditor.GetNumberOfFrames();
	const float Rate = AnimEditor.GetFrameRate();
	const float CurrentFrame = Rate > 0.0f ? AnimEditor.GetPlaybackTime() * Rate : 0.0f;

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

	// 트랙 행 (Notify 트랙 자리. 실제 Notify는 다음 단계)
	const char* TrackNames[] = { "Notifies" };
	for (int32 Row = 0; Row < IM_ARRAYSIZE(TrackNames); ++Row)
	{
		const float RowTop = TrackTop + Row * RowHeight;
		DrawList->AddRectFilled(ImVec2(Origin.x, RowTop), ImVec2(Origin.x + Avail.x, RowTop + RowHeight),
			IM_COL32(255, 255, 255, Row % 2 == 0 ? 8 : 0));
		DrawList->AddText(ImVec2(Origin.x + 8.0f, RowTop + (RowHeight - ImGui::GetTextLineHeight()) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text), TrackNames[Row]);
		DrawList->AddLine(ImVec2(Origin.x, RowTop + RowHeight), ImVec2(Origin.x + Avail.x, RowTop + RowHeight),
			IM_COL32(0, 0, 0, 120));
	}
	DrawList->AddText(ImVec2(Origin.x + 8.0f, Top + (RulerHeight - ImGui::GetTextLineHeight()) * 0.5f),
		ImGui::GetColorU32(ImGuiCol_TextDisabled), "Tracks");
	DrawList->AddLine(ImVec2(TimeAreaMinX, Top), ImVec2(TimeAreaMinX, Bottom), IM_COL32(0, 0, 0, 160));

	// 시간 영역 마우스 처리: 눈금·트랙 전체를 하나의 버튼으로 잡습니다.
	ImGui::SetCursorScreenPos(ImVec2(TimeAreaMinX, Top));
	ImGui::InvisibleButton("##TimeInput", ImVec2(ImMax(1.0f, TimeAreaMaxX - TimeAreaMinX), ImMax(1.0f, Avail.y)),
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bHovered = ImGui::IsItemHovered();
	const ImGuiIO& IO = ImGui::GetIO();

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

	// 왼쪽 버튼: 스크러빙. 누르는 순간부터 놓을 때까지 재생 헤드가 마우스를 따라가고, 프레임 단위로 맞춥니다.
	if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		bScrubbing = true;
	}
	if (bScrubbing)
	{
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			bScrubbing = false;
		}
		else if (Rate > 0.0f && NumFrames > 0)
		{
			const float Frame = ImClamp(std::round(XToFrame(IO.MousePos.x)), 0.0f, static_cast<float>(NumFrames));
			if (std::abs(Frame - CurrentFrame) > 1.e-3f || AnimEditor.IsPlaying())
			{
				AnimEditor.SetPlaybackTime(Frame / Rate);
			}
		}
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

	// 마지막 아이템은 여전히 ##TimeInput 버튼입니다. 잠시 머무를 때만 조작법을 보여 줍니다.
	if (!bScrubbing)
	{
		ImGui::SetItemTooltip("Drag: scrub   Wheel: zoom   Right/Middle drag: pan");
	}

	ImGui::EndChild();
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
