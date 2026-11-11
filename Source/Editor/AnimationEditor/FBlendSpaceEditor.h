#pragma once

#include "Core/Types.h"

#include <array>
#include <filesystem>
#include <memory>

class UBlendSpace;

// Blend Space 복사본의 축과 샘플 배치를 편집하고 입력 위치별 혼합 가중치를 미리 보여준다.
class FBlendSpaceEditor {
public:
    // 샘플 선택과 미리보기 값 등 Blend Space 편집 상태를 생성한다.
    FBlendSpaceEditor();

    // 편집 복사본과 편집기에 속한 상태를 해제한다.
    ~FBlendSpaceEditor();

public:
    // 공유 에셋을 편집용 1차원 복사본으로 열고 선택 및 드래그 상태를 초기화한다.
    bool Open(const std::filesystem::path& Path, FString& Error);

    // 축, 샘플 목록과 캔버스를 표시하고 저장 입력을 처리한다.
    void Draw();

    // 이 편집 창 또는 하위 창이 입력 포커스를 갖는지 확인한다.
    bool IsFocused() const;

private:
    // 편집 창을 닫고 저장 전 복사본을 해제한다.
    void Close();

    // 편집 복사본을 파일과 공유 에셋 캐시에 반영하고 변경 상태를 해제한다.
    bool Save();

    // 축과 샘플 및 미리보기 가중치를 그리고 샘플 이동과 에셋 드롭을 처리한다.
    void DrawCanvas();
    // 선택 샘플의 교체 및 삭제와 새 샘플 추가 메뉴를 그린다.
    void DrawSampleMenus();

    // 축 이름, 유효 범위와 격자 수 편집을 처리한다.
    void DrawAxis();

    // 샘플별 시퀀스와 좌표를 목록에서 편집하고 선택 및 삭제를 처리한다.
    void DrawSamples();

    // 시퀀스를 로드해 지정 입력 좌표에 샘플을 추가하고 새 샘플을 선택한다.
    void AddSampleAt(const FString& AnimationPath, float Value);

    // 축 범위로 제한하고 자유 배치가 아니면 격자 간격에 좌표를 맞춘다.
    float Snap(float Value, bool Free) const;

private:
    // 저장 전 샘플 및 축 변경을 보관하는 독립된 편집 에셋을 소유한다.
    std::unique_ptr<UBlendSpace> mEditing{};

    // 편집 결과를 저장할 실제 Blend Space 파일 경로이다.
    std::filesystem::path mPath{};

    // 마지막 저장 이후 축이나 샘플에 변경이 생겼는지 나타낸다.
    bool mDirty{};

    // Blend Space 편집 창이 열려 있는지 나타낸다.
    bool mOpen{};

    // 혼합할 샘플과 가중치를 미리 계산할 현재 X 입력이다.
    float mPreviewInput{};

    // 목록과 캔버스에서 선택한 원본 샘플 인덱스이며 미선택은 -1이다.
    int32 mSelectedSample{-1};

    // 마우스로 이동 중인 샘플 인덱스이며 드래그 밖에서는 -1이다.
    int32 mDraggingSample{-1};

    // 샘플 위치 대신 미리보기 입력 위치를 마우스로 조절 중인지 나타낸다.
    bool mDraggingPreview{};

    // 샘플 추가와 이동 좌표를 기본적으로 축 격자에 맞출지 지정한다.
    bool mSnapToGrid{true};
    // 캔버스의 샘플 위치에 시퀀스 이름을 표시할지 지정한다.
    bool mShowSampleNames{true};
    // 다음 목록 표시에서 선택한 샘플 행이 보이도록 스크롤할지 나타낸다.
    bool mRevealSample{};

    // 샘플 추가 메뉴를 연 위치의 축 좌표이다.
    float mAddValue{};

    // 샘플 추가 메뉴에서 시퀀스 목록을 거를 검색어 입력 버퍼이다.
    std::array<char, 64> mAddSearch{};

    // 에셋 로드, 샘플 편집 또는 저장에 실패했을 때 표시할 메시지이다.
    FString mError{};
};
