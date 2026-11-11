#include "EnginePCH.h"
#include "Animation/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimSequenceBase.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/AnimInstance.h"

#include <algorithm>
#include <cmath>

void FAnimNode_BlendSpacePlayer::Initialize(UAnimInstance& AnimInstance) {
    mSampleWeights.clear();
    mBlendedLength = 0.0;
    FAnimNode_AssetPlayerBase::Initialize(AnimInstance);
    mNormalizedTime = mPlayRate < 0.0f ? 1.0 : 0.0;
}

void FAnimNode_BlendSpacePlayer::Update(const FAnimationUpdateContext& Context) {
    mRootMotionDelta = {};
    mX = mXInput.Evaluate(Context, mX);

    if (mBlendSpace == nullptr) {
        mSampleWeights.clear();
        mBlendedLength = 0.0;
        mInternalTime = 0.0;

        return;
    }

    mBlendSpace->GetSamplesToBlend({mX, 0.0f, 0.0f}, mSampleWeights);
    mBlendedLength = mBlendSpace->GetBlendedPlayLength(mSampleWeights);

    // 입력 변화로 혼합 길이가 바뀌어도 이전 위상을 새 길이에 옮겨 동작의 주기를 유지한다.
    mInternalTime = mNormalizedTime * mBlendedLength;
    AdvanceTime(Context.GetDeltaTime(), mBlendedLength);
    mNormalizedTime = mBlendedLength > 0.0 ? std::clamp(mInternalTime / mBlendedLength, 0.0, 1.0) : 0.0;

    if (mAdvancedThisUpdate && mBlendedLength > 0.0) {
        const double PreviousPhase{mPreviousTime / mBlendedLength};
        const auto& Samples{mBlendSpace->GetSamples()};
        float Accumulated{};

        for (const FBlendSampleWeight& Weight : mSampleWeights) {
            if (Weight.mSampleIndex < 0 || static_cast<std::size_t>(Weight.mSampleIndex) >= Samples.size() || Weight.mWeight <= 0.0f) {
                continue;
            }

            const UAnimSequenceBase* Sequence{Samples[static_cast<std::size_t>(Weight.mSampleIndex)].mAnimation};

            if (Sequence == nullptr) {
                continue;
            }

            const double Length{Sequence->GetPlayLength()};
            const FRootMotionDelta Delta{Context.GetAnimInstance().ExtractSequenceEvents(*Sequence, PreviousPhase * Length, mNormalizedTime * Length, mPlayRate, mWrappedThisUpdate, mIncludeStartThisUpdate, Context)};
            Accumulated += Weight.mWeight;
            mRootMotionDelta = FAnimationRuntime::BlendRootMotion(mRootMotionDelta, Delta, 1.0f - Weight.mWeight / Accumulated);
        }
    }
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

        // 서로 다른 길이의 시퀀스를 같은 정규화 시점에서 샘플링한다.
        const double Time{mNormalizedTime * Animation->GetPlayLength()};
        AnimInstance.RecordDebugSample(*this, Weight.mSampleIndex, Weight.mWeight);

        if (First) {
            AnimInstance.EvaluateSequence(*Animation, Time, Output.mPose);
            Accumulated = Weight.mWeight;
            First = false;

            continue;
        }

        SamplePose.ResetToReferencePose();
        AnimInstance.EvaluateSequence(*Animation, Time, SamplePose.mPose);
        // 누적 포즈에 새 샘플을 합칠 때 현재까지의 가중치 합을 기준으로 다시 정규화한다.
        Accumulated += Weight.mWeight;
        FAnimationRuntime::BlendTwoPosesTogether(Output.mPose, SamplePose.mPose, 1.0f - Weight.mWeight / Accumulated, Output.mPose);
    }
}

void FAnimNode_BlendSpacePlayer::GetValueInputs(std::vector<FAnimNodeValueInput>& OutInputs) {
    OutInputs.push_back({"X", &mXInput});
}

const FString& FAnimNode_BlendSpacePlayer::GetNodeType() const {
    static const FString Type{"Blend Space Player"};

    return Type;
}

void FAnimNode_BlendSpacePlayer::GetDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    FAnimNodeValue BlendSpace{};

    BlendSpace.mName = "Blend Space";
    BlendSpace.mValue = mBlendSpace != nullptr ? FbxPath::ToString(FbxPath::FromString(mBlendSpace->GetPath()).stem()) : "None";
    OutValues.push_back(std::move(BlendSpace));

    FAnimNodeValue X{};
    const FBlendParameter* Parameter{mBlendSpace != nullptr ? &mBlendSpace->GetBlendParameter(EBlendSpaceAxis::X) : nullptr};

    X.mName = "X";
    X.mValue = FAnimNodeFloatValue{&mX, Parameter != nullptr ? Parameter->mMin : 0.0f, Parameter != nullptr ? Parameter->mMax : 0.0f};
    OutValues.push_back(std::move(X));

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

FRootMotionDelta FAnimNode_BlendSpacePlayer::GetRootMotionDelta() const {
    return mRootMotionDelta;
}
