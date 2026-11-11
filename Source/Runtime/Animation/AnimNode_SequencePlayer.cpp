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
    if (!std::isfinite(PlayRate) || !std::isfinite(Length) || Length < 0.0) {
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
    mRootMotionDelta = {};
    AdvanceTime(Context.GetDeltaTime(), GetCurrentAssetLength());

    if (mSequence != nullptr && mAdvancedThisUpdate) {
        mRootMotionDelta = Context.GetAnimInstance().ExtractSequenceEvents(*mSequence, mPreviousTime, mInternalTime, mPlayRate, mWrappedThisUpdate, mIncludeStartThisUpdate, Context);
    }
}

void FAnimNode_SequencePlayer::Evaluate(FPoseContext& Output) {
    // 시퀀스가 없는 노드도 유효한 기준 포즈를 출력해 상위 블렌딩이 가능하게 한다.
    if (mSequence != nullptr) {
        Output.GetAnimInstance().EvaluateSequence(*mSequence, mInternalTime, Output.mPose);
    } else {
        Output.ResetToReferencePose();
    }
}

const FString& FAnimNode_SequencePlayer::GetNodeType() const {
    static const FString Type{"Sequence Player"};

    return Type;
}

void FAnimNode_SequencePlayer::GetDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    FAnimNodeValue Sequence{};

    Sequence.mName = "Sequence";
    Sequence.mValue = mSequence != nullptr ? FbxPath::ToString(FbxPath::FromString(mSequence->GetPath()).stem()) : "None";
    OutValues.push_back(std::move(Sequence));

    GetPlayerDebugValues(OutValues);
}

double FAnimNode_SequencePlayer::GetCurrentAssetLength() const {
    return mSequence != nullptr ? mSequence->GetPlayLength() : 0.0;
}

void FAnimNode_SequencePlayer::SetPaused(bool Paused) {
    SetPlaying(!Paused);
}

FRootMotionDelta FAnimNode_SequencePlayer::GetRootMotionDelta() const {
    return mRootMotionDelta;
}
