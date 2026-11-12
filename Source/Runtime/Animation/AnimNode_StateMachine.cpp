#include "EnginePCH.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimationRuntime.h"
#include "Animation/AnimInstance.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <variant>
#include <unordered_map>

namespace {
    using FRuleValue = std::variant<std::monostate, float, bool>;

    FAnimNode_AssetPlayerBase* FindStatePlayer(FAnimNode_Base* Root, const FString& Name) {
        // 상태의 연결된 포즈 경로에서 플레이어를 찾되, 후보가 여러 개면 모호한 조회로 처리한다.
        std::vector<FAnimNode_Base*> Pending{Root};
        std::vector<FAnimNode_Base*> Visited{};
        FAnimNode_AssetPlayerBase* Found{};

        while (!Pending.empty()) {
            auto* Node{Pending.back()};
            Pending.pop_back();

            if (Node == nullptr || std::find(Visited.begin(), Visited.end(), Node) != Visited.end()) {
                continue;
            }

            Visited.push_back(Node);

            if (auto* Player{dynamic_cast<FAnimNode_AssetPlayerBase*>(Node)}; Player != nullptr && (Name.empty() || Node->mName == Name)) {
                if (Found != nullptr) {
                    return nullptr;
                }

                Found = Player;
            }

            std::vector<FAnimNodePoseInput> Inputs{};
            Node->GetPoseInputs(Inputs);

            for (const auto& Input : Inputs) {
                Pending.push_back(Input.mLink->mLinkedNode);
            }
        }

        return Found;
    }

    FRuleValue EvaluateRuleNode(const FAnimTransitionGraph& Graph, int32 Id, const FAnimationUpdateContext& Context, FAnimNode_Base* Root, float StateTime, std::unordered_map<int32, FRuleValue>& Cache, uint32 Depth) {
        if (Depth >= UAnimGraph::MaxNodes) {
            return {};
        }

        if (const auto Found{Cache.find(Id)}; Found != Cache.end()) {
            return Found->second;
        }

        const auto* Node{Graph.FindNode(Id)};

        if (Node == nullptr) {
            return {};
        }

        // monostate는 끊긴 입력이나 잘못된 타입을 뜻하며 최종 전환 판정에서는 false가 된다.
        FRuleValue Value{};
        UAnimInstance& Instance{Context.GetAnimInstance()};

        switch (Node->mType) {
            case EAnimRuleNodeType::Float: Value = Node->mValue; break;
            case EAnimRuleNodeType::Bool: Value = Node->mBoolValue; break;
            case EAnimRuleNodeType::GetFloat: {
                if (Instance.HasGraphVariable(Node->mName, false)) {
                    Value = Instance.GetFloatVariable(Node->mName);
                }

                break;
            }
            case EAnimRuleNodeType::GetBool: {
                if (Instance.HasGraphVariable(Node->mName, true)) {
                    Value = Instance.GetBoolVariable(Node->mName);
                }

                break;
            }
            case EAnimRuleNodeType::StateTime: Value = StateTime; break;
            case EAnimRuleNodeType::TimeRemaining:
            case EAnimRuleNodeType::Finished: {
                const auto* Player{FindStatePlayer(Root, Node->mName)};

                if (Player != nullptr && Player->GetCurrentAssetLength() > 0.0) {
                    if (Node->mType == EAnimRuleNodeType::Finished) {
                        Value = Player->IsFinished();
                    } else {
                        // 재생 방향과 속도를 반영해 클립 끝까지 남은 실제 시간을 구한다.
                        const double Remaining{Player->mPlayRate < 0.0f ? Player->GetAccumulatedTime() : Player->GetCurrentAssetLength() - Player->GetAccumulatedTime()};
                        Value = std::abs(Player->mPlayRate) > 0.000001f ? static_cast<float>(std::max(Remaining, 0.0) / std::abs(Player->mPlayRate)) : std::numeric_limits<float>::max();
                    }
                }

                break;
            }
            default: {
                const FRuleValue A{EvaluateRuleNode(Graph, Node->mInputA, Context, Root, StateTime, Cache, Depth + 1)};
                const FRuleValue B{Node->mType != EAnimRuleNodeType::Not ? EvaluateRuleNode(Graph, Node->mInputB, Context, Root, StateTime, Cache, Depth + 1) : FRuleValue{}};

                if (FAnimTransitionGraph::IsBoolInput(Node->mType)) {
                    const auto* Left{std::get_if<bool>(&A)};
                    const auto* Right{std::get_if<bool>(&B)};

                    if (Left != nullptr && (Right != nullptr || Node->mType == EAnimRuleNodeType::Not)) {
                        if (Node->mType == EAnimRuleNodeType::Not) {
                            Value = !*Left;
                        } else {
                            Value = Node->mType == EAnimRuleNodeType::And ? *Left && *Right : *Left || *Right;
                        }
                    }
                } else {
                    const auto* Left{std::get_if<float>(&A)};
                    const auto* Right{std::get_if<float>(&B)};

                    if (Left != nullptr && Right != nullptr && std::isfinite(*Left) && std::isfinite(*Right)) {
                        switch (Node->mType) {
                            case EAnimRuleNodeType::Greater: Value = *Left > *Right; break;
                            case EAnimRuleNodeType::GreaterEqual: Value = *Left >= *Right; break;
                            case EAnimRuleNodeType::Less: Value = *Left < *Right; break;
                            case EAnimRuleNodeType::LessEqual: Value = *Left <= *Right; break;
                            case EAnimRuleNodeType::Equal: Value = *Left == *Right; break;
                            case EAnimRuleNodeType::NotEqual: Value = *Left != *Right; break;
                            default: break;
                        }
                    }
                }

                break;
            }
        }

        Cache.emplace(Id, Value);

        return Value;
    }
}

bool FAnimTransitionEvaluator::Evaluate(const FAnimTransitionGraph& Graph, const FAnimationUpdateContext& Context, FAnimNode_Base* StateRoot, float StateTime) {
    // 하나의 판정 안에서 공유되는 규칙 노드는 캐시해 같은 입력을 반복 계산하지 않는다.
    std::unordered_map<int32, FRuleValue> Cache{};
    const FRuleValue Value{EvaluateRuleNode(Graph, Graph.GetResult(), Context, StateRoot, StateTime, Cache, 0)};
    const auto* Result{std::get_if<bool>(&Value)};

    return Result != nullptr && *Result;
}

void FAnimTransitionEvaluator::GetDebugValues(const FAnimTransitionGraph& Graph, const FAnimationUpdateContext& Context, FAnimNode_Base* StateRoot, float StateTime, std::vector<FAnimRuleDebugValue>& OutValues) {
    std::unordered_map<int32, FRuleValue> Cache{};
    OutValues.clear();
    OutValues.reserve(Graph.GetNodes().size());

    for (const auto& Node : Graph.GetNodes()) {
        OutValues.push_back({Node.mId, EvaluateRuleNode(Graph, Node.mId, Context, StateRoot, StateTime, Cache, 0)});
    }
}

void FAnimNode_StateMachine::Configure(const FAnimStateMachineData& Data) {
    mEntry = Data.GetEntryState();
    mTransitions = Data.GetTransitions();
    // 낮은 우선순위 값부터 검사하며 동률인 전환은 작성 순서대로 처리한다.
    std::stable_sort(mTransitions.begin(), mTransitions.end(), [](const auto& A, const auto& B) {
        return A.mPriority < B.mPriority;
    });
}

void FAnimNode_StateMachine::AddState(const FAnimStateData& Data, FAnimNode_Base* Root) {
    mStates.push_back({Data.mId, Data.mName, {Root}, Data.mResetOnEntry, 0.0f});
}

int32 FAnimNode_StateMachine::FindStateIndex(int32 Id) const {
    const auto Found{std::find_if(mStates.begin(), mStates.end(), [Id](const auto& State) {
        return State.mId == Id;
    })};

    return Found != mStates.end() ? static_cast<int32>(Found - mStates.begin()) : -1;
}

void FAnimNode_StateMachine::Initialize(UAnimInstance& AnimInstance) {
    FAnimNode_Base::Initialize(AnimInstance);
    mCurrent = FindStateIndex(mEntry);
    mTarget = -1;
    mActiveTransition = -1;
    mBlendElapsed = 0.0f;
    mBlendDuration = 0.0f;
    mRuleDebugValues.clear();
    mLastOutputPose.Empty();
    mSnapshotPose.Empty();
    mBlendFromSnapshot = false;

    for (auto& State : mStates) {
        State.mElapsed = 0.0f;
    }
}

bool FAnimNode_StateMachine::TryStartTransition(const FAnimationUpdateContext& Context, int32 FromIndex, bool Interrupt) {
    auto& From{mStates[static_cast<std::size_t>(FromIndex)]};
    mRuleDebugValues.clear();

    for (const auto& Transition : mTransitions) {
        const int32 TargetIndex{FindStateIndex(Transition.mToState)};
        const bool AnyState{Transition.mFromState == FAnimStateMachineData::AnyStateId};

        if ((!AnyState && Transition.mFromState != From.mId) || TargetIndex < 0 || (AnyState && TargetIndex == FromIndex)) {
            continue;
        }

        if (Interrupt && TargetIndex == mTarget) {
            continue;
        }

        const bool Allowed{FAnimTransitionEvaluator::Evaluate(Transition.mRule, Context, From.mPose.mLinkedNode, From.mElapsed)};
        mRuleDebugValues.push_back({std::format("Rule -> {} ({})", mStates[static_cast<std::size_t>(TargetIndex)].mName, Transition.mPriority), FString{Allowed ? "true" : "false"}});

        if (!Allowed) {
            continue;
        }

        if (Interrupt) {
            mBlendFromSnapshot = mLastOutputPose.Num() > 0;
            if (mBlendFromSnapshot) {
                mSnapshotPose = mLastOutputPose;
            }
            mCurrent = mTarget;
            mTarget = -1;
        } else {
            mBlendFromSnapshot = false;
        }

        auto& Target{mStates[static_cast<std::size_t>(TargetIndex)]};

        // 재진입 정책에 따라 포즈 경로의 재생 상태를 초기화한다.
        if (Target.mResetOnEntry) {
            Target.mPose.Initialize(Context.GetAnimInstance());
        }

        Target.mElapsed = 0.0f;
        // 진입 프레임에는 시간을 진행하지 않고 새 상태의 입력 값과 샘플 가중치만 준비한다.
        const FAnimationUpdateContext InitialContext{Context.GetAnimInstance(), 0.0f};
        Target.mPose.Update(InitialContext);
        mBlendElapsed = 0.0f;
        mBlendDuration = std::max(Transition.mDuration, 0.0f);

        if (mBlendDuration <= 0.0f || TargetIndex == mCurrent) {
            mCurrent = TargetIndex;
            mTarget = -1;
            mActiveTransition = -1;
            mBlendFromSnapshot = false;
        } else {
            mTarget = TargetIndex;
            mActiveTransition = Transition.mId;
        }

        // 같은 프레임에 조건이 여러 개 참이어도 먼저 선택한 전환 하나만 수행한다.
        return true;
    }

    return false;
}

void FAnimNode_StateMachine::Update(const FAnimationUpdateContext& Context) {
    if (mCurrent < 0) {
        return;
    }

    const float Delta{std::isfinite(Context.GetDeltaTime()) ? std::max(Context.GetDeltaTime(), 0.0f) : 0.0f};
    auto& Current{mStates[static_cast<std::size_t>(mCurrent)]};
    Current.mPose.Update(Context);
    Current.mElapsed += Delta;

    if (mTarget >= 0) {
        auto& Target{mStates[static_cast<std::size_t>(mTarget)]};
        Target.mPose.Update(Context);
        Target.mElapsed += Delta;
        mBlendElapsed += Delta;

        if (mBlendElapsed >= mBlendDuration) {
            mCurrent = mTarget;
            mTarget = -1;
            mActiveTransition = -1;
            mBlendFromSnapshot = false;
            return;
        }

        TryStartTransition(Context, mTarget, true);
        return;
    }

    TryStartTransition(Context, mCurrent, false);
}

void FAnimNode_StateMachine::Evaluate(FPoseContext& Output) {
    if (mCurrent < 0) {
        return;
    }

    auto& Current{mStates[static_cast<std::size_t>(mCurrent)]};

    if (mTarget < 0) {
        Current.mPose.Evaluate(Output);
        mLastOutputPose = Output.mPose;

        return;
    }

    // 전환 진행률을 도착 상태의 가중치로 사용하고 두 포즈를 교차 블렌딩한다.
    FPoseContext Source{Output.GetAnimInstance()};
    FPoseContext Target{Output.GetAnimInstance()};
    const float Alpha{GetBlendAlpha()};

    if (mBlendFromSnapshot) {
        Source.mPose = mSnapshotPose;
    } else {
        Current.mPose.Evaluate(Source, 1.0f - Alpha);
    }

    mStates[static_cast<std::size_t>(mTarget)].mPose.Evaluate(Target, Alpha);
    FAnimationRuntime::BlendTwoPosesTogether(Source.mPose, Target.mPose, 1.0f - Alpha, Output.mPose);
    mLastOutputPose = Output.mPose;
}

const FString& FAnimNode_StateMachine::GetNodeType() const {
    static const FString Type{"State Machine"};

    return Type;
}

void FAnimNode_StateMachine::GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs) {
    for (auto& State : mStates) {
        OutInputs.push_back({State.mName, &State.mPose});
    }
}

void FAnimNode_StateMachine::GetDebugValues(std::vector<FAnimNodeValue>& OutValues) {
    OutValues.push_back({"Current State", mCurrent >= 0 ? mStates[static_cast<std::size_t>(mCurrent)].mName : FString{"None"}});
    OutValues.push_back({"Target State", mTarget >= 0 ? mStates[static_cast<std::size_t>(mTarget)].mName : FString{"None"}});
    OutValues.push_back({"Transition", std::format("{:.0f}%", GetBlendAlpha() * 100.0f)});

    if (mCurrent >= 0) {
        OutValues.push_back({"State Time", std::format("{:.2f} s", mStates[static_cast<std::size_t>(mCurrent)].mElapsed)});
    }

    OutValues.insert(OutValues.end(), mRuleDebugValues.begin(), mRuleDebugValues.end());
}

int32 FAnimNode_StateMachine::GetCurrentState() const {
    return mCurrent >= 0 ? mStates[static_cast<std::size_t>(mCurrent)].mId : -1;
}

int32 FAnimNode_StateMachine::GetTargetState() const {
    return mTarget >= 0 ? mStates[static_cast<std::size_t>(mTarget)].mId : -1;
}

float FAnimNode_StateMachine::GetBlendAlpha() const {
    return mTarget >= 0 && mBlendDuration > 0.0f ? std::clamp(mBlendElapsed / mBlendDuration, 0.0f, 1.0f) : 1.0f;
}

int32 FAnimNode_StateMachine::GetActiveTransition() const {
    return mActiveTransition;
}

const FAnimStateRuntime* FAnimNode_StateMachine::FindState(int32 Id) const {
    const int32 Index{FindStateIndex(Id)};

    return Index >= 0 ? &mStates[static_cast<std::size_t>(Index)] : nullptr;
}

FRootMotionDelta FAnimNode_StateMachine::GetRootMotionDelta() const {
    if (mCurrent < 0) {
        return {};
    }

    const FAnimNode_Base* Current{mStates[static_cast<std::size_t>(mCurrent)].mPose.mLinkedNode};
    const FRootMotionDelta Source{Current != nullptr ? Current->GetRootMotionDelta() : FRootMotionDelta{}};

    if (mTarget < 0) {
        return Source;
    }

    const FAnimNode_Base* Target{mStates[static_cast<std::size_t>(mTarget)].mPose.mLinkedNode};
    return FAnimationRuntime::BlendRootMotion(Source, Target != nullptr ? Target->GetRootMotionDelta() : FRootMotionDelta{}, 1.0f - GetBlendAlpha());
}
