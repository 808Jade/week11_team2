#include "EnginePCH.h"
#include "Animation/AnimStateMachineData.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimAssetArchive.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

const std::vector<FAnimRuleNode>& FAnimTransitionGraph::GetNodes() const {
    return mNodes;
}

FAnimRuleNode* FAnimTransitionGraph::FindNode(int32 Id) {
    return const_cast<FAnimRuleNode*>(std::as_const(*this).FindNode(Id));
}

const FAnimRuleNode* FAnimTransitionGraph::FindNode(int32 Id) const {
    const auto Found{std::find_if(mNodes.begin(), mNodes.end(), [Id](const auto& Node) {
        return Node.mId == Id;
    })};

    return Found != mNodes.end() ? &*Found : nullptr;
}

int32 FAnimTransitionGraph::AddNode(EAnimRuleNodeType Type, float X, float Y) {
    if (Type >= EAnimRuleNodeType::Count || mNodes.size() >= UAnimGraph::MaxNodes || mNextId == std::numeric_limits<int32>::max() || !std::isfinite(X) || !std::isfinite(Y)) {
        return -1;
    }

    FAnimRuleNode Node{};
    Node.mId = mNextId++;
    Node.mType = Type;
    Node.mX = X;
    Node.mY = Y;
    mNodes.push_back(Node);

    return Node.mId;
}

bool FAnimTransitionGraph::RemoveNode(int32 Id) {
    if (std::erase_if(mNodes, [Id](const auto& Node) {
        return Node.mId == Id;
    }) == 0) {
        return false;
    }

    for (auto& Node : mNodes) {
        if (Node.mInputA == Id) {
            Node.mInputA = -1;
        }

        if (Node.mInputB == Id) {
            Node.mInputB = -1;
        }
    }

    if (mResult == Id) {
        mResult = -1;
    }

    return true;
}

bool FAnimTransitionGraph::Reaches(int32 From, int32 To) const {
    std::vector<int32> Pending{From};
    std::vector<int32> Visited{};

    while (!Pending.empty()) {
        const int32 Id{Pending.back()};
        Pending.pop_back();

        if (Id == To) {
            return true;
        }

        const auto* Node{FindNode(Id)};

        if (Node == nullptr || std::find(Visited.begin(), Visited.end(), Id) != Visited.end()) {
            continue;
        }

        Visited.push_back(Id);
        Pending.push_back(Node->mInputA);
        Pending.push_back(Node->mInputB);
    }

    return false;
}

bool FAnimTransitionGraph::Connect(int32 From, int32 To, int32 Pin) {
    // 값 타입, 입력 개수와 순환 여부를 검사한 뒤 대상 입력에 노드 ID를 저장한다.
    const auto* Source{FindNode(From)};
    auto* Target{FindNode(To)};

    if (Source == nullptr || Target == nullptr || Pin < 0 || Pin >= GetInputCount(Target->mType) || IsBool(Source->mType) != IsBoolInput(Target->mType) || Reaches(From, To)) {
        return false;
    }

    (Pin == 0 ? Target->mInputA : Target->mInputB) = From;

    return true;
}

bool FAnimTransitionGraph::Disconnect(int32 To, int32 Pin) {
    auto* Node{FindNode(To)};

    if (Node == nullptr || Pin < 0 || Pin >= GetInputCount(Node->mType)) {
        return false;
    }

    int32& Input{Pin == 0 ? Node->mInputA : Node->mInputB};
    const bool Changed{Input >= 0};
    Input = -1;

    return Changed;
}

bool FAnimTransitionGraph::SetResult(int32 Id) {
    const auto* Node{FindNode(Id)};

    if (Id != -1 && (Node == nullptr || !IsBool(Node->mType))) {
        return false;
    }

    mResult = Id;

    return true;
}

int32 FAnimTransitionGraph::GetResult() const {
    return mResult;
}

bool FAnimTransitionGraph::IsBool(EAnimRuleNodeType Type) {
    return Type == EAnimRuleNodeType::Bool || Type == EAnimRuleNodeType::GetBool || (Type >= EAnimRuleNodeType::Greater && Type <= EAnimRuleNodeType::Not) || Type == EAnimRuleNodeType::Finished;
}

int32 FAnimTransitionGraph::GetInputCount(EAnimRuleNodeType Type) {
    return Type == EAnimRuleNodeType::Not ? 1 : (Type >= EAnimRuleNodeType::Greater && Type <= EAnimRuleNodeType::Or ? 2 : 0);
}

bool FAnimTransitionGraph::IsBoolInput(EAnimRuleNodeType Type) {
    return Type == EAnimRuleNodeType::And || Type == EAnimRuleNodeType::Or || Type == EAnimRuleNodeType::Not;
}

const char* FAnimTransitionGraph::GetTitle(EAnimRuleNodeType Type) {
    static constexpr const char* Titles[]{"Float", "Bool", "Get Float", "Get Bool", ">", ">=", "<", "<=", "==", "!=", "And", "Or", "Not", "State Time", "Time Remaining", "Finished"};

    return Type < EAnimRuleNodeType::Count ? Titles[static_cast<std::size_t>(Type)] : "Unknown";
}

bool FAnimTransitionGraph::Validate() const {
    // 파일에서 읽은 그래프에도 편집 시와 같은 ID, 타입 및 비순환 조건을 적용한다.
    if (mNodes.size() > UAnimGraph::MaxNodes || (mResult != -1 && (FindNode(mResult) == nullptr || !IsBool(FindNode(mResult)->mType)))) {
        return false;
    }

    for (std::size_t Index{}; Index < mNodes.size(); ++Index) {
        const auto& Node{mNodes[Index]};

        if (Node.mId < 0 || Node.mId == std::numeric_limits<int32>::max() || Node.mType >= EAnimRuleNodeType::Count || !std::isfinite(Node.mValue) || !std::isfinite(Node.mX) || !std::isfinite(Node.mY) || std::any_of(mNodes.begin(), mNodes.begin() + Index, [&Node](const auto& Other) {
            return Other.mId == Node.mId;
        })) {
            return false;
        }

        for (int32 Pin{}; Pin < 2; ++Pin) {
            const int32 Input{Pin == 0 ? Node.mInputA : Node.mInputB};

            if (Input == -1) {
                continue;
            }

            const auto* Source{FindNode(Input)};

            if (Pin >= GetInputCount(Node.mType) || Source == nullptr || IsBool(Source->mType) != IsBoolInput(Node.mType) || Reaches(Input, Node.mId)) {
                return false;
            }
        }
    }

    return true;
}

void FAnimTransitionGraph::RenameVariable(const FString& OldName, const FString& NewName) {
    for (auto& Node : mNodes) {
        if ((Node.mType == EAnimRuleNodeType::GetFloat || Node.mType == EAnimRuleNodeType::GetBool) && Node.mName == OldName) {
            Node.mName = NewName;
        }
    }
}

void FAnimTransitionGraph::Serialize(FAnimAssetArchive& Archive) {
    Archive.Serialize("Result", mResult);
    uint32 Count{static_cast<uint32>(mNodes.size())};

    if (!Archive.BeginArray("Nodes", Count)) {
        return;
    }

    if (Count > UAnimGraph::MaxNodes) {
        Archive.SetError("Too many transition rule nodes");
    } else if (Archive.IsLoading()) {
        mNodes.assign(Count, FAnimRuleNode{});
        mNextId = 0;
    }

    for (uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        auto& Node{mNodes[Index]};
        uint32 Type{static_cast<uint32>(Node.mType)};
        Archive.Serialize("Id", Node.mId);
        Archive.Serialize("Type", Type);
        Archive.Serialize("A", Node.mInputA);
        Archive.Serialize("B", Node.mInputB);
        Archive.Serialize("Name", Node.mName);
        Archive.Serialize("Value", Node.mValue);
        Archive.Serialize("Bool", Node.mBoolValue);
        Archive.Serialize("X", Node.mX);
        Archive.Serialize("Y", Node.mY);
        Archive.EndObject();

        if (Type >= static_cast<uint32>(EAnimRuleNodeType::Count)) {
            Archive.SetError("Invalid transition rule node type");
        } else {
            Node.mType = static_cast<EAnimRuleNodeType>(Type);
        }

        if (Archive.IsLoading() && Node.mId >= 0 && Node.mId < std::numeric_limits<int32>::max()) {
            mNextId = std::max(mNextId, Node.mId + 1);
        }
    }

    Archive.EndArray();

    if (!Archive.HasError() && !Validate()) {
        Archive.SetError("Invalid transition rule graph");
    }
}

const std::vector<FAnimStateData>& FAnimStateMachineData::GetStates() const {
    return mStates;
}

const std::vector<FAnimTransitionData>& FAnimStateMachineData::GetTransitions() const {
    return mTransitions;
}

FAnimStateData* FAnimStateMachineData::FindState(int32 Id) {
    return const_cast<FAnimStateData*>(std::as_const(*this).FindState(Id));
}

const FAnimStateData* FAnimStateMachineData::FindState(int32 Id) const {
    const auto Found{std::find_if(mStates.begin(), mStates.end(), [Id](const auto& State) {
        return State.mId == Id;
    })};

    return Found != mStates.end() ? &*Found : nullptr;
}

FAnimTransitionData* FAnimStateMachineData::FindTransition(int32 Id) {
    return const_cast<FAnimTransitionData*>(std::as_const(*this).FindTransition(Id));
}

const FAnimTransitionData* FAnimStateMachineData::FindTransition(int32 Id) const {
    const auto Found{std::find_if(mTransitions.begin(), mTransitions.end(), [Id](const auto& Transition) {
        return Transition.mId == Id;
    })};

    return Found != mTransitions.end() ? &*Found : nullptr;
}

int32 FAnimStateMachineData::AddState(const FString& Name, float X, float Y, const UAnimGraph& Owner) {
    if (Name.empty() || mStates.size() >= MaxStates || mNextStateId == std::numeric_limits<int32>::max() || !std::isfinite(X) || !std::isfinite(Y) || std::any_of(mStates.begin(), mStates.end(), [&Name](const auto& State) {
        return State.mName == Name;
    })) {
        return -1;
    }

    FAnimStateData State{};
    // 배열 인덱스와 독립된 ID를 발급해 중간 상태 삭제 후에도 전환 참조를 유지한다.
    State.mId = mNextStateId++;
    State.mName = Name;
    State.mX = X;
    State.mY = Y;
    State.mGraph = UAnimGraph::CreateStateGraph();

    for (const auto& Variable : Owner.GetVariables()) {
        State.mGraph->AddVariable(Variable);
    }

    mStates.push_back(State);

    if (mEntryState < 0) {
        mEntryState = State.mId;
    }

    return State.mId;
}

bool FAnimStateMachineData::RemoveState(int32 Id) {
    if (std::erase_if(mStates, [Id](const auto& State) {
        return State.mId == Id;
    }) == 0) {
        return false;
    }

    // 삭제한 상태를 출발지나 도착지로 사용하는 전환도 함께 제거한다.
    std::erase_if(mTransitions, [Id](const auto& Transition) {
        return Transition.mFromState == Id || Transition.mToState == Id;
    });

    if (mEntryState == Id) {
        mEntryState = mStates.empty() ? -1 : mStates.front().mId;
    }

    return true;
}

int32 FAnimStateMachineData::AddTransition(int32 From, int32 To) {
    if ((From != AnyStateId && FindState(From) == nullptr) || FindState(To) == nullptr || mTransitions.size() >= UAnimGraph::MaxNodes || mNextTransitionId == std::numeric_limits<int32>::max()) {
        return -1;
    }

    FAnimTransitionData Transition{};
    // 전환 자체의 편집 및 선택에도 상태 ID와 별도의 고유 번호를 사용한다.
    Transition.mId = mNextTransitionId++;
    Transition.mFromState = From;
    Transition.mToState = To;
    mTransitions.push_back(Transition);

    return Transition.mId;
}

bool FAnimStateMachineData::RemoveTransition(int32 Id) {
    return std::erase_if(mTransitions, [Id](const auto& Transition) {
        return Transition.mId == Id;
    }) != 0;
}

bool FAnimStateMachineData::SetEntryState(int32 Id) {
    if (FindState(Id) == nullptr) {
        return false;
    }

    mEntryState = Id;

    return true;
}

int32 FAnimStateMachineData::GetEntryState() const {
    return mEntryState;
}

bool FAnimStateMachineData::SetAnyStatePosition(float X, float Y) {
    if (!std::isfinite(X) || !std::isfinite(Y) || (mAnyStateX == X && mAnyStateY == Y)) {
        return false;
    }

    mAnyStateX = X;
    mAnyStateY = Y;

    return true;
}

float FAnimStateMachineData::GetAnyStateX() const {
    return mAnyStateX;
}

float FAnimStateMachineData::GetAnyStateY() const {
    return mAnyStateY;
}

void FAnimStateMachineData::CopyFrom(const FAnimStateMachineData& Source) {
    if (this == &Source) {
        return;
    }

    mStates = Source.mStates;
    mTransitions = Source.mTransitions;
    mEntryState = Source.mEntryState;
    mNextStateId = Source.mNextStateId;
    mNextTransitionId = Source.mNextTransitionId;
    mAnyStateX = Source.mAnyStateX;
    mAnyStateY = Source.mAnyStateY;

    // shared_ptr만 복사하면 원본과 편집본이 같은 내부 그래프를 수정하므로 깊게 복사한다.
    for (auto& State : mStates) {
        auto Graph{UAnimGraph::CreateStateGraph()};

        if (State.mGraph != nullptr) {
            Graph->CopyFrom(*State.mGraph);
        }

        State.mGraph = std::move(Graph);
    }
}

void FAnimStateMachineData::Serialize(FAnimAssetArchive& Archive) {
    Archive.Serialize("Entry", mEntryState);

    if (Archive.IsLoading()) {
        mAnyStateX = -180.0f;
        mAnyStateY = 180.0f;
    }

    if (!Archive.IsLoading() || Archive.HasField("AnyStateX") || Archive.HasField("AnyStateY")) {
        Archive.Serialize("AnyStateX", mAnyStateX);
        Archive.Serialize("AnyStateY", mAnyStateY);
    }

    if (!std::isfinite(mAnyStateX) || !std::isfinite(mAnyStateY)) {
        Archive.SetError("Invalid Any State position");
    }

    uint32 Count{static_cast<uint32>(mStates.size())};

    if (!Archive.BeginArray("States", Count)) {
        return;
    }

    if (Count > MaxStates) {
        Archive.SetError("Too many animation states");
    } else if (Archive.IsLoading()) {
        mStates.assign(Count, FAnimStateData{});
        mNextStateId = 0;
    }

    for (uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        auto& State{mStates[Index]};
        Archive.Serialize("Id", State.mId);
        Archive.Serialize("Name", State.mName);
        Archive.Serialize("X", State.mX);
        Archive.Serialize("Y", State.mY);
        Archive.Serialize("ResetOnEntry", State.mResetOnEntry);

        if (Archive.IsLoading()) {
            State.mGraph = UAnimGraph::CreateStateGraph();
        }

        if (State.mGraph == nullptr) {
            Archive.SetError("Missing state pose graph");
        } else if (Archive.BeginObject("Graph")) {
            State.mGraph->Serialize(Archive);
            Archive.EndObject();
        }

        Archive.EndObject();

        if (State.mId < 0 || State.mId == std::numeric_limits<int32>::max() || State.mName.empty() || !std::isfinite(State.mX) || !std::isfinite(State.mY) || std::any_of(mStates.begin(), mStates.begin() + Index, [&State](const auto& Other) {
            return Other.mId == State.mId || Other.mName == State.mName;
        })) {
            Archive.SetError("Invalid or duplicate animation state");
        } else {
            // 카운터는 별도로 저장하지 않고 읽은 ID의 최댓값 다음 번호로 복원한다.
            mNextStateId = std::max(mNextStateId, State.mId + 1);
        }
    }

    Archive.EndArray();
    Count = static_cast<uint32>(mTransitions.size());

    if (Archive.HasError() || !Archive.BeginArray("Transitions", Count)) {
        return;
    }

    if (Count > UAnimGraph::MaxNodes) {
        Archive.SetError("Too many animation transitions");
    } else if (Archive.IsLoading()) {
        mTransitions.assign(Count, FAnimTransitionData{});
        mNextTransitionId = 0;
    }

    for (uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        auto& Transition{mTransitions[Index]};
        Archive.Serialize("Id", Transition.mId);
        Archive.Serialize("From", Transition.mFromState);
        Archive.Serialize("To", Transition.mToState);
        Archive.Serialize("Duration", Transition.mDuration);
        Archive.Serialize("Priority", Transition.mPriority);

        if (Archive.BeginObject("Rule")) {
            Transition.mRule.Serialize(Archive);
            Archive.EndObject();
        }

        Archive.EndObject();

        if (Transition.mId < 0 || Transition.mId == std::numeric_limits<int32>::max() || (Transition.mFromState != AnyStateId && FindState(Transition.mFromState) == nullptr) || FindState(Transition.mToState) == nullptr || !std::isfinite(Transition.mDuration) || Transition.mDuration < 0.0f || std::any_of(mTransitions.begin(), mTransitions.begin() + Index, [&Transition](const auto& Other) {
            return Other.mId == Transition.mId;
        })) {
            Archive.SetError("Invalid animation transition");
        } else {
            // 전환 ID도 기존 항목과 충돌하지 않는 다음 번호부터 발급한다.
            mNextTransitionId = std::max(mNextTransitionId, Transition.mId + 1);
        }
    }

    Archive.EndArray();

    if ((!mStates.empty() && FindState(mEntryState) == nullptr) || (mStates.empty() && mEntryState != -1)) {
        Archive.SetError("Invalid animation state machine entry");
    }
}
