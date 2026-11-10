#pragma once

#include "Core/Types.h"

#include <unordered_set>

namespace ax::NodeEditor {
    struct EditorContext;
}

class UAnimInstance;
class USkeletalMeshComponent;
struct FAnimNode_Base;

class FAnimGraphPanel final {
public:
    FAnimGraphPanel();

    ~FAnimGraphPanel();
    FAnimGraphPanel(const FAnimGraphPanel&) = delete;
    FAnimGraphPanel& operator=(const FAnimGraphPanel&) = delete;

public:
    void Draw(USkeletalMeshComponent* Component, bool Playing, bool& Open);

private:
    void DrawContent(USkeletalMeshComponent* Component, bool Playing);

    void DrawVariables(UAnimInstance& Instance, bool Playing);

    void DrawGraph(UAnimInstance& Instance, bool Playing);

    bool PlaceNodes(const UAnimInstance& Instance, FAnimNode_Base* Root, bool All);

private:
    ax::NodeEditor::EditorContext* mEditor{};

    std::unordered_set<uint64> mPlacedNodes{};

    bool mLayoutRequested{};

    bool mFitRequested{};

    float mVariablesWidth{260.0f};
};
