#pragma once

#include "Core/Types.h"

#include <array>
#include <filesystem>
#include <memory>

namespace ax::NodeEditor {
    struct EditorContext;
}

class UAnimGraph;

class FAnimGraphEditor {
public:
    FAnimGraphEditor();

    ~FAnimGraphEditor();

    FAnimGraphEditor(const FAnimGraphEditor&) = delete;

    FAnimGraphEditor& operator=(const FAnimGraphEditor&) = delete;

public:
    bool Open(const std::filesystem::path& Path, FString& Error);

    void Draw();

    bool IsFocused() const;

private:
    void Close();

    bool Save();

    void DrawVariables(bool SaveRequested);
    bool DrawVariable(std::size_t Index, bool SaveRequested);

    void DrawGraph();
    void DrawValueGetters();
    void DrawValueLinks();
    int32 FindVariableIndex(const FString& Name) const;

    void DrawSelection();

private:
    ax::NodeEditor::EditorContext* mEditor{};

    std::unique_ptr<UAnimGraph> mEditing{};

    std::uintptr_t mContextNode{};

    std::filesystem::path mPath{};

    int32 mSelectedNode{-2};

    bool mApplyPositions{};

    bool mFitRequested{};

    bool mDirty{};

    bool mOpen{};

    float mCreateX{};
    float mCreateY{};

    std::array<char, 64> mNewVariable{};

    FString mError{};
};
