#pragma once

#include "Core/Types.h"

#include <array>
#include <filesystem>
#include <memory>

class UBlendSpace;

class FBlendSpaceEditor {
public:
    FBlendSpaceEditor();

    ~FBlendSpaceEditor();

public:
    bool Open(const std::filesystem::path& Path, FString& Error);

    void Draw();

    bool IsFocused() const;

private:
    void Close();

    bool Save();

    void DrawCanvas();
    void DrawSampleMenus();

    void DrawAxis();

    void DrawSamples();

    void AddSampleAt(const FString& AnimationPath, float Value);

    float Snap(float Value, bool Free) const;

private:
    std::unique_ptr<UBlendSpace> mEditing{};

    std::filesystem::path mPath{};

    bool mDirty{};

    bool mOpen{};

    float mPreviewInput{};

    int32 mSelectedSample{-1};

    int32 mDraggingSample{-1};

    bool mDraggingPreview{};

    bool mSnapToGrid{true};
    bool mShowSampleNames{true};
    bool mRevealSample{};

    float mAddValue{};

    std::array<char, 64> mAddSearch{};

    FString mError{};
};
