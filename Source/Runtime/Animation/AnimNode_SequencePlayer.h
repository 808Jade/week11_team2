#pragma once

#include "Animation/AnimNode_AssetPlayerBase.h"

class UAnimSequenceBase;

struct FAnimNode_SequencePlayer : public FAnimNode_AssetPlayerBase {
public:
    bool SetSequence(const UAnimSequenceBase* Sequence, bool Looping, float PlayRate, FString& OutError);
    void Update(const FAnimationUpdateContext& Context) override;

    void Evaluate(FPoseContext& Output) override;

    const char* GetNodeType() const override;

    void GetDebugValues(std::vector<FAnimNodeValue>& OutValues) override;

    double GetCurrentAssetLength() const override;

    void SetPaused(bool Paused);

public:

    const UAnimSequenceBase* mSequence{};

private:
    bool mPaused{};
};
