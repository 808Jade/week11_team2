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
	const float BarHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y + ImGui::GetStyle().ItemSpacing.y;
	const float TrackHeight = ImMax(0.0f, ImGui::GetContentRegionAvail().y - BarHeight);

	// TODO: 프레임 눈금·재생 헤드·스크러빙, Notify 트랙
	ImGui::BeginChild("##TimelineTracks", ImVec2(0.0f, TrackHeight));
	ImGui::TextDisabled("Tracks (scrubber and notifies come next)");
	ImGui::EndChild();

	DrawTransportBar();
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
