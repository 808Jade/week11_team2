#include "EnginePCH.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequenceBase.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/AnimInstance.h"
#include <cmath>

bool FAnimNode_SequencePlayer::SetSequence(const UAnimSequenceBase* Sequence, bool Looping, float PlayRate, FString& OutError) {
    if (Sequence == nullptr || Sequence->GetSkeleton() == nullptr) {
        OutError = "Set an initialized animation sequence first";
        return false;
    }

    const double Length{Sequence->GetPlayLength()};
    if (!std::isfinite(PlayRate) || PlayRate <= 0.0f || !std::isfinite(Length) || Length < 0.0) {
        OutError = "Invalid play rate or sequence length";
        return false;
    }

    mSequence = Sequence;
    mLoop = Looping;
    mPlayRate = PlayRate;
    Restart();
    return true;
}

void FAnimNode_SequencePlayer::Update(const FAnimationUpdateContext& Context) {
    if (!mPaused) {
        AdvanceTime(Context.GetDeltaTime(), GetCurrentAssetLength());
    }
}

void FAnimNode_SequencePlayer::Evaluate(FPoseContext& Output) {
    if (mSequence != nullptr) {
        Output.GetAnimInstance().EvaluateSequence(*mSequence, mInternalTime, Output.mPose);
    } else {
        Output.ResetToReferencePose();
    }
}

const char* FAnimNode_SequencePlayer::GetNodeType() const {
    return "Sequence Player";
}

void FAnimNode_SequencePlayer::GetDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    FAnimNodeValue Sequence{};

    Sequence.mName = "Sequence";
    Sequence.mText = mSequence != nullptr ? FbxPath::ToString(FbxPath::FromString(mSequence->GetPath()).stem()) : "None";
    OutValues.push_back(std::move(Sequence));

    GetPlayerDebugValues(OutValues);
}

double FAnimNode_SequencePlayer::GetCurrentAssetLength() const {
    return mSequence != nullptr ? mSequence->GetPlayLength() : 0.0;
}

void FAnimNode_SequencePlayer::SetPaused(bool Paused) {
    mPaused = Paused;
}
