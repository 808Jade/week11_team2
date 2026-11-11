#include "EnginePCH.h"
#include "Animation/AnimNode_AssetPlayerBase.h"

#include <algorithm>
#include <cmath>
#include <format>

void FAnimNode_AssetPlayerBase::Initialize(UAnimInstance& AnimInstance) {
    FAnimNode_Base::Initialize(AnimInstance);
    Restart();
}

double FAnimNode_AssetPlayerBase::GetAccumulatedTime() const {
    return mInternalTime;
}

void FAnimNode_AssetPlayerBase::SetAccumulatedTime(double Time) {
    mInternalTime = std::clamp(std::isfinite(Time) ? Time : 0.0, 0.0, std::max(GetCurrentAssetLength(), 0.0));
    mFinished = false;
    mPreviousTime = mInternalTime;
    mWrappedThisUpdate = false;
    mAdvancedThisUpdate = false;
    mIncludeStartOnNextAdvance = true;
}

bool FAnimNode_AssetPlayerBase::IsFinished() const {
    return mFinished;
}

void FAnimNode_AssetPlayerBase::Restart() {
    // 역재생은 끝 지점에서 출발해야 시작 직후 종료로 판정되지 않는다.
    SetAccumulatedTime(mPlayRate < 0.0f ? std::max(GetCurrentAssetLength(), 0.0) : 0.0);
    mPlaying = true;
}

void FAnimNode_AssetPlayerBase::AdvanceTime(float DeltaTime, double Length) {
    mPreviousTime = mInternalTime;
    mWrappedThisUpdate = false;
    mAdvancedThisUpdate = false;
    mIncludeStartThisUpdate = mIncludeStartOnNextAdvance;

    if (Length <= 0.0 || !std::isfinite(mPlayRate)) {
        mInternalTime = 0.0;
        return;
    }

    if (!mPlaying || mFinished || mPlayRate == 0.0f || !std::isfinite(DeltaTime) || DeltaTime <= 0.0f) {
        return;
    }

    const double Next{mInternalTime + static_cast<double>(DeltaTime) * static_cast<double>(mPlayRate)};

    if (mLoop) {
        mWrappedThisUpdate = Next >= Length || Next < 0.0;
        mInternalTime = std::fmod(Next, Length);

        if (mInternalTime < 0.0) {
            mInternalTime += Length;
        }

        mFinished = false;
    } else {
        mInternalTime = std::clamp(Next, 0.0, Length);
        mFinished = (mPlayRate > 0.0f && Next >= Length) || (mPlayRate < 0.0f && Next <= 0.0);
    }

    mAdvancedThisUpdate = mInternalTime != mPreviousTime || mWrappedThisUpdate;

    if (mAdvancedThisUpdate) {
        mIncludeStartOnNextAdvance = false;
    }
}

void FAnimNode_AssetPlayerBase::GetPlayerDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    const double Length{GetCurrentAssetLength()};
    FAnimNodeValue Time{};

    Time.mName = "Time";
    Time.mValue = std::format("{:.2f} / {:.2f} s{}", mInternalTime, Length, mFinished ? " (finished)" : "");
    OutValues.push_back(std::move(Time));

    FAnimNodeValue PlayRate{};

    PlayRate.mName = "Play Rate";
    PlayRate.mValue = FAnimNodeFloatValue{&mPlayRate, -3.0f, 3.0f};
    OutValues.push_back(std::move(PlayRate));

    FAnimNodeValue Loop{};

    Loop.mName = "Loop";
    Loop.mValue = &mLoop;
    OutValues.push_back(std::move(Loop));
}

void FAnimNode_AssetPlayerBase::SetPlaying(bool Playing) {
    mPlaying = Playing;

    if (Playing) {
        mFinished = false;
    }
}

bool FAnimNode_AssetPlayerBase::IsPlaying() const {
    return mPlaying && !mFinished;
}

bool FAnimNode_AssetPlayerBase::SetPlayRate(float PlayRate) {
    if (!std::isfinite(PlayRate)) {
        return false;
    }

    mPlayRate = PlayRate;
    return true;
}

float FAnimNode_AssetPlayerBase::GetPlayRate() const {
    return mPlayRate;
}

void FAnimNode_AssetPlayerBase::SetLooping(bool Looping) {
    mLoop = Looping;
}

bool FAnimNode_AssetPlayerBase::IsLooping() const {
    return mLoop;
}

double FAnimNode_AssetPlayerBase::GetPreviousAssetTime() const {
    return mPreviousTime;
}

bool FAnimNode_AssetPlayerBase::DidWrapThisUpdate() const {
    return mWrappedThisUpdate;
}
