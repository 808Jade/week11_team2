#include "EnginePCH.h"
#include "Animation/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimSequenceBase.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/AnimInstance.h"

#include <algorithm>
#include <cmath>
#include <format>

void FAnimNode_BlendSpacePlayer::Initialize(UAnimInstance& AnimInstance) {
    mSampleWeights.clear();
    mBlendedLength = 0.0;
    FAnimNode_AssetPlayerBase::Initialize(AnimInstance);
    mNormalizedTime = mPlayRate < 0.0f ? 1.0 : 0.0;
}

void FAnimNode_BlendSpacePlayer::Update(const FAnimationUpdateContext& Context) {
    if (mXBinding != nullptr) {
        mX = *mXBinding;
    }

    if (mBlendSpace == nullptr) {
        mSampleWeights.clear();
        mBlendedLength = 0.0;
        mInternalTime = 0.0;

        return;
    }

    mBlendSpace->GetSamplesToBlend({mX, 0.0f, 0.0f}, mSampleWeights);
    mBlendedLength = mBlendSpace->GetBlendedPlayLength(mSampleWeights);

    mInternalTime = mNormalizedTime * mBlendedLength;
    AdvanceTime(Context.GetDeltaTime(), mBlendedLength);
    mNormalizedTime = mBlendedLength > 0.0 ? std::clamp(mInternalTime / mBlendedLength, 0.0, 1.0) : 0.0;
}

void FAnimNode_BlendSpacePlayer::Evaluate(FPoseContext& Output) {
    if (mBlendSpace == nullptr || mSampleWeights.empty()) {
        Output.ResetToReferencePose();
        return;
    }

    const auto& Samples{mBlendSpace->GetSamples()};
    UAnimInstance& AnimInstance{Output.GetAnimInstance()};
    FPoseContext SamplePose{AnimInstance};
    float Accumulated{};
    bool First{true};

    for (const FBlendSampleWeight& Weight : mSampleWeights) {

        if (Weight.mSampleIndex < 0 || static_cast<std::size_t>(Weight.mSampleIndex) >= Samples.size()) {
            continue;
        }

        const UAnimSequenceBase* Animation{Samples[static_cast<std::size_t>(Weight.mSampleIndex)].mAnimation};

        if (Animation == nullptr || Weight.mWeight <= 0.0f) {
            continue;
        }

        const double Time{mNormalizedTime * Animation->GetPlayLength()};

        if (First) {
            AnimInstance.EvaluateSequence(*Animation, Time, Output.mPose);
            Accumulated = Weight.mWeight;
            First = false;

            continue;
        }

        SamplePose.ResetToReferencePose();
        AnimInstance.EvaluateSequence(*Animation, Time, SamplePose.mPose);
        Accumulated += Weight.mWeight;
        FAnimationRuntime::BlendTwoPosesTogether(Output.mPose, SamplePose.mPose, 1.0f - Weight.mWeight / Accumulated, Output.mPose);
    }
}

const char* FAnimNode_BlendSpacePlayer::GetNodeType() const {
    return "Blend Space Player";
}

void FAnimNode_BlendSpacePlayer::GetDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    FAnimNodeValue BlendSpace{};

    BlendSpace.mName = "Blend Space";
    BlendSpace.mText = mBlendSpace != nullptr ? FbxPath::ToString(FbxPath::FromString(mBlendSpace->GetPath()).stem()) : "None";
    OutValues.push_back(std::move(BlendSpace));

    FAnimNodeValue X{};
    const FBlendParameter* Parameter{mBlendSpace != nullptr ? &mBlendSpace->GetBlendParameter(EBlendSpaceAxis::X) : nullptr};

    X.mName = Parameter != nullptr ? "X (" + Parameter->mDisplayName + ")" : "X";
    X.mType = EAnimNodeValueType::Float;
    X.mFloat = &mX;
    X.mMin = Parameter != nullptr ? Parameter->mMin : 0.0f;
    X.mMax = Parameter != nullptr ? Parameter->mMax : 0.0f;
    X.mBinding = mXBinding != nullptr ? mXBindingName : FString{};
    X.mBindingParameter = "X";
    OutValues.push_back(std::move(X));

    for (const FBlendSampleWeight& Weight : mSampleWeights) {
        if (mBlendSpace == nullptr || Weight.mSampleIndex < 0 || static_cast<std::size_t>(Weight.mSampleIndex) >= mBlendSpace->GetSamples().size()) {
            continue;
        }

        const FBlendSample& Sample{mBlendSpace->GetSamples()[static_cast<std::size_t>(Weight.mSampleIndex)]};
        FAnimNodeValue Entry{};

        Entry.mName = std::format("Sample {}", Weight.mSampleIndex);
        Entry.mText = std::format("{} @ {:.2f} : {:.0f}%", Sample.mAnimation != nullptr ? FbxPath::ToString(FbxPath::FromString(Sample.mAnimation->GetPath()).stem()) : "?", Sample.mSampleValue.X, Weight.mWeight * 100.0f);
        OutValues.push_back(std::move(Entry));
    }

    GetPlayerDebugValues(OutValues);
}

double FAnimNode_BlendSpacePlayer::GetCurrentAssetLength() const {
    return mBlendedLength;
}

const std::vector<FBlendSampleWeight>& FAnimNode_BlendSpacePlayer::GetSampleWeights() const {
    return mSampleWeights;
}

double FAnimNode_BlendSpacePlayer::GetNormalizedTime() const {
    return mNormalizedTime;
}
