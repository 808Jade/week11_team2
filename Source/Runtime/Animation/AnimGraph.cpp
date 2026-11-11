#include "EnginePCH.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimAssetArchive.h"
#include "UObject/UObjectGlobals.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace {
    uint64 NextGraphRevision() {
        static uint64 Counter{};

        return ++Counter;
    }

    constexpr uint32 AnimGraphFormatVersion{1};
    constexpr std::string_view BindingSuffix{".Binding"};
}

const FString* FAnimGraphNode::FindParameter(const FString& Name) const {
    const auto Found{std::find_if(mParameters.begin(), mParameters.end(), [&Name](const FAnimGraphParameter& Parameter) {
        return Parameter.mName == Name;
    })};

    return Found != mParameters.end() ? &Found->mValue : nullptr;
}

void FAnimGraphNode::SetParameter(const FString& Name, const FString& Value, int32 GetterId) {
    const auto Found{std::find_if(mParameters.begin(), mParameters.end(), [&Name](const FAnimGraphParameter& Parameter) {
        return Parameter.mName == Name;
    })};

    if (Value.empty()) {
        if (Found != mParameters.end()) {
            mParameters.erase(Found);
        }

        return;
    }

    if (Found != mParameters.end()) {
        Found->mValue = Value;
        Found->mGetterId = GetterId;
    } else {
        mParameters.push_back({Name, Value, GetterId});
    }
}

std::shared_ptr<UAnimGraph> UAnimGraph::CreateStateGraph() {
    // 상태 내부 그래프는 shared_ptr로 수명을 관리하므로 전역 UObject 정리 대상에서 제외한다.
    std::shared_ptr<UAnimGraph> Graph{NewObject<UAnimGraph>()};
    Graph->DetachFromObjectRegistry();

    return Graph;
}

const std::vector<FAnimGraphNode>& UAnimGraph::GetNodes() const {
    return mNodes;
}

const std::vector<FAnimGraphLink>& UAnimGraph::GetLinks() const {
    return mLinks;
}

const std::vector<FAnimGraphVariable>& UAnimGraph::GetVariables() const {
    return mVariables;
}

FAnimGraphNode* UAnimGraph::FindNode(int32 Id) {
    const auto Found{std::find_if(mNodes.begin(), mNodes.end(), [Id](const FAnimGraphNode& Node) {
        return Node.mId == Id;
    })};

    return Found != mNodes.end() ? &*Found : nullptr;
}

const FAnimGraphNode* UAnimGraph::FindNode(int32 Id) const {
    return const_cast<UAnimGraph*>(this)->FindNode(Id);
}

int32 UAnimGraph::AddNode(const FString& Type, const FString& Name, float X, float Y) {
    if (mNodes.size() >= MaxNodes || mNextNodeId == std::numeric_limits<int32>::max() || !std::isfinite(X) || !std::isfinite(Y)) {
        return -2;
    }

    FAnimGraphNode Node{};

    Node.mId = mNextNodeId++;
    Node.mType = Type;
    Node.mName = Name;
    Node.mX = X;
    Node.mY = Y;

    if (Type == "StateMachine") {
        mStateMachines.try_emplace(Node.mId);
    }

    mNodes.push_back(std::move(Node));

    return mNodes.back().mId;
}

bool UAnimGraph::RemoveNode(int32 Id) {
    const auto Found{std::find_if(mNodes.begin(), mNodes.end(), [Id](const FAnimGraphNode& Node) {
        return Node.mId == Id;
    })};

    if (Found == mNodes.end()) {
        return false;
    }

    mStateMachines.erase(Id);
    mNodes.erase(Found);
    std::erase_if(mLinks, [Id](const FAnimGraphLink& Link) {
        return Link.mFromNode == Id || Link.mToNode == Id;
    });

    return true;
}

bool UAnimGraph::WouldCreateCycle(int32 FromNode, int32 ToNode) const {
    // 목적지에서 출발 노드로 돌아가는 기존 경로가 있으면 새 연결이 순환을 만든다.
    if (FromNode == ToNode) {
        return true;
    }
    std::vector<int32> Pending{ToNode};
    std::vector<int32> Visited{};

    while (!Pending.empty()) {
        const int32 Current{Pending.back()};

        Pending.pop_back();

        if (Current == FromNode) {
            return true;
        }

        if (std::find(Visited.begin(), Visited.end(), Current) != Visited.end()) {
            continue;
        }

        Visited.push_back(Current);

        for (const FAnimGraphLink& Link : mLinks) {
            if (Link.mFromNode == Current) {
                Pending.push_back(Link.mToNode);
            }
        }
    }

    return false;
}

bool UAnimGraph::Connect(int32 FromNode, int32 ToNode, int32 ToPin) {
    if (FindNode(FromNode) == nullptr || (ToNode != OutputNodeId && FindNode(ToNode) == nullptr) || ToPin < 0 || (ToNode == OutputNodeId && ToPin != 0) || WouldCreateCycle(FromNode, ToNode)) {
        return false;
    }

    // 입력 핀 하나에는 출력 하나만 연결되도록 기존 연결을 교체한다.
    Disconnect(ToNode, ToPin);
    mLinks.push_back({FromNode, ToNode, ToPin});

    return true;
}

bool UAnimGraph::Disconnect(int32 ToNode, int32 ToPin) {
    return std::erase_if(mLinks, [ToNode, ToPin](const FAnimGraphLink& Link) {
        return Link.mToNode == ToNode && Link.mToPin == ToPin;
    }) > 0;
}

int32 UAnimGraph::FindInput(int32 ToNode, int32 ToPin) const {
    for (const FAnimGraphLink& Link : mLinks) {
        if (Link.mToNode == ToNode && Link.mToPin == ToPin) {
            return Link.mFromNode;
        }
    }

    return OutputNodeId;
}

void UAnimGraph::SetNodePosition(int32 Id, float X, float Y) {
    if (!std::isfinite(X) || !std::isfinite(Y)) {
        return;
    }

    if (Id == OutputNodeId) {
        mOutputX = X;
        mOutputY = Y;
    } else if (FAnimGraphNode* Node{FindNode(Id)}) {
        Node->mX = X;
        Node->mY = Y;
    }
}

float UAnimGraph::GetOutputX() const {
    return mOutputX;
}

float UAnimGraph::GetOutputY() const {
    return mOutputY;
}

bool UAnimGraph::AddVariable(const FAnimGraphVariable& Variable) {
    if (Variable.mName.empty() || !std::isfinite(Variable.mDefault) || std::any_of(mVariables.begin(), mVariables.end(), [&Variable](const FAnimGraphVariable& Existing) {
        return Existing.mName == Variable.mName;
    })) {
        return false;
    }

    mVariables.push_back(Variable);
    UpdateChildVariables({}, {});

    return true;
}

bool UAnimGraph::SetVariable(int32 Index, const FAnimGraphVariable& Variable) {
    if (Index < 0 || static_cast<std::size_t>(Index) >= mVariables.size() || Variable.mName.empty() || !std::isfinite(Variable.mDefault)) {
        return false;
    }

    for (std::size_t Other{}; Other < mVariables.size(); ++Other) {
        if (Other != static_cast<std::size_t>(Index) && mVariables[Other].mName == Variable.mName) {
            return false;
        }
    }

    const FString OldName{mVariables[static_cast<std::size_t>(Index)].mName};

    mVariables[static_cast<std::size_t>(Index)] = Variable;

    // 변수 이름을 바꿔도 Get 노드와 파라미터 바인딩이 같은 변수를 계속 참조하게 한다.
    if (OldName != Variable.mName) {
        for (FAnimGraphVariableGetter& Getter : mVariableGetters) {
            if (Getter.mVariable == OldName) {
                Getter.mVariable = Variable.mName;
            }
        }

        for (FAnimGraphNode& Node : mNodes) {
            for (FAnimGraphParameter& Parameter : Node.mParameters) {
                if (Parameter.mName.ends_with(BindingSuffix) && Parameter.mValue == OldName) {
                    Parameter.mValue = Variable.mName;
                }
            }
        }
    }

    UpdateChildVariables(OldName, Variable.mName);

    return true;
}

bool UAnimGraph::RemoveVariable(int32 Index) {
    if (Index < 0 || static_cast<std::size_t>(Index) >= mVariables.size()) {
        return false;
    }

    const FString Name{mVariables[static_cast<std::size_t>(Index)].mName};

    std::erase_if(mVariableGetters, [&Name](const FAnimGraphVariableGetter& Getter) {
        return Getter.mVariable == Name;
    });

    for (FAnimGraphNode& Node : mNodes) {
        std::erase_if(Node.mParameters, [&Name](const FAnimGraphParameter& Parameter) {
            return Parameter.mName.ends_with(BindingSuffix) && Parameter.mValue == Name;
        });
    }

    mVariables.erase(mVariables.begin() + Index);
    UpdateChildVariables(Name, {});

    return true;
}

const std::vector<FAnimGraphVariableGetter>& UAnimGraph::GetVariableGetters() const {
    return mVariableGetters;
}

const FAnimGraphVariableGetter* UAnimGraph::FindVariableGetter(int32 Id) const {
    const auto Found{std::find_if(mVariableGetters.begin(), mVariableGetters.end(), [Id](const FAnimGraphVariableGetter& Getter) {
        return Getter.mId == Id;
    })};

    return Found == mVariableGetters.end() ? nullptr : &*Found;
}

const FAnimGraphVariableGetter* UAnimGraph::FindBoundGetter(const FAnimGraphNode& Node, const FString& Parameter) const {
    const FString Key{Parameter + ".Binding"};
    const auto Found{std::find_if(Node.mParameters.begin(), Node.mParameters.end(), [&Key](const FAnimGraphParameter& Binding) {
        return Binding.mName == Key;
    })};

    return Found == Node.mParameters.end() ? nullptr : ResolveGetter(*Found);
}

int32 UAnimGraph::AddVariableGetter(const FString& Variable, float X, float Y) {
    if (mVariableGetters.size() >= MaxNodes || mNextGetterId == std::numeric_limits<int32>::max() || !std::isfinite(X) || !std::isfinite(Y) || std::none_of(mVariables.begin(), mVariables.end(), [&Variable](const FAnimGraphVariable& Entry) {
        return Entry.mName == Variable && Entry.mType == EAnimGraphVariableType::Float;
    })) {
        return -1;
    }

    const int32 Id{mNextGetterId++};

    mVariableGetters.push_back({Id, Variable, X, Y});

    return Id;
}

bool UAnimGraph::SetVariableGetterPosition(int32 Id, float X, float Y) {
    auto* Getter{const_cast<FAnimGraphVariableGetter*>(FindVariableGetter(Id))};

    if (Getter == nullptr || !std::isfinite(X) || !std::isfinite(Y)) {
        return false;
    }

    Getter->mX = X;
    Getter->mY = Y;

    return true;
}

bool UAnimGraph::RemoveVariableGetter(int32 Id) {
    const FAnimGraphVariableGetter* Getter{FindVariableGetter(Id)};

    if (Getter == nullptr) {
        return false;
    }

    for (FAnimGraphNode& Node : mNodes) {
        std::erase_if(Node.mParameters, [this, Getter](const FAnimGraphParameter& Parameter) {
            return Parameter.mName.ends_with(BindingSuffix) && Parameter.mValue == Getter->mVariable && ResolveGetter(Parameter) == Getter;
        });
    }

    mVariableGetters.erase(mVariableGetters.begin() + (Getter - mVariableGetters.data()));

    return true;
}

const FAnimGraphVariableGetter* UAnimGraph::ResolveGetter(const FAnimGraphParameter& Binding) const {
    // 명시적인 Get 노드 ID를 우선하고, ID가 없는 기존 바인딩은 변수 이름으로 찾는다.
    if (const FAnimGraphVariableGetter* Getter{FindVariableGetter(Binding.mGetterId)}; Getter != nullptr && Getter->mVariable == Binding.mValue) {
        return Getter;
    }

    const auto Found{std::find_if(mVariableGetters.begin(), mVariableGetters.end(), [&Binding](const FAnimGraphVariableGetter& Getter) {
        return Getter.mVariable == Binding.mValue;
    })};

    return Found == mVariableGetters.end() ? nullptr : &*Found;
}

void UAnimGraph::CopyFrom(const UAnimGraph& Source) {
    if (this == &Source) {
        return;
    }

    mStateMachines.clear();

    for (const auto& [Id, Machine] : Source.mStateMachines) {
        mStateMachines[Id].CopyFrom(Machine);
    }

    mNodes = Source.mNodes;
    mLinks = Source.mLinks;
    mVariables = Source.mVariables;
    mVariableGetters = Source.mVariableGetters;
    mNextGetterId = Source.mNextGetterId;
    mNextNodeId = Source.mNextNodeId;
    mOutputX = Source.mOutputX;
    mOutputY = Source.mOutputY;
    mRevision = NextGraphRevision();
}

FAnimStateMachineData* UAnimGraph::FindStateMachine(int32 NodeId) {
    return const_cast<FAnimStateMachineData*>(std::as_const(*this).FindStateMachine(NodeId));
}

const FAnimStateMachineData* UAnimGraph::FindStateMachine(int32 NodeId) const {
    const auto Found{mStateMachines.find(NodeId)};

    return Found != mStateMachines.end() ? &Found->second : nullptr;
}

void UAnimGraph::SynchronizeVariables(const std::vector<FAnimGraphVariable>& Variables) {
    mVariables = Variables;
    UpdateChildVariables({}, {});
}

void UAnimGraph::UpdateChildVariables(const FString& OldName, const FString& NewName) {
    // 부모 변수 변경을 상태 내부 그래프와 전환 규칙에 전파하고 무효해진 Float 연결을 정리한다.
    const auto HasFloat{[this](const FString& Name) {
        return std::any_of(mVariables.begin(), mVariables.end(), [&Name](const auto& Variable) {
            return Variable.mName == Name && Variable.mType == EAnimGraphVariableType::Float;
        });
    }};

    std::erase_if(mVariableGetters, [&HasFloat](const auto& Getter) {
        return !HasFloat(Getter.mVariable);
    });

    for (auto& Node : mNodes) {
        std::erase_if(Node.mParameters, [&HasFloat](const auto& Parameter) {
            return Parameter.mName.ends_with(BindingSuffix) && !HasFloat(Parameter.mValue);
        });
    }

    for (auto& [Id, Machine] : mStateMachines) {
        for (const auto& State : Machine.GetStates()) {
            if (State.mGraph == nullptr) {
                continue;
            }

            if (!OldName.empty()) {
                const auto& Variables{State.mGraph->GetVariables()};
                const auto Found{std::find_if(Variables.begin(), Variables.end(), [&OldName](const auto& Variable) {
                    return Variable.mName == OldName;
                })};

                if (Found != Variables.end()) {
                    const int32 Index{static_cast<int32>(Found - Variables.begin())};

                    if (NewName.empty()) {
                        State.mGraph->RemoveVariable(Index);
                    } else {
                        auto Variable{*Found};
                        Variable.mName = NewName;
                        State.mGraph->SetVariable(Index, Variable);
                    }
                }
            }

            State.mGraph->SynchronizeVariables(mVariables);
        }

        for (const auto& Transition : Machine.GetTransitions()) {
            Machine.FindTransition(Transition.mId)->mRule.RenameVariable(OldName, NewName);
        }
    }
}

void UAnimGraph::SerializeStateMachines(FAnimAssetArchive& Archive) {
    if (Archive.HasError()) {
        return;
    }

    if (Archive.IsLoading()) {
        mStateMachines.clear();

        if (!Archive.HasField("StateMachines")) {
            if (std::any_of(mNodes.begin(), mNodes.end(), [](const auto& Node) {
                return Node.mType == "StateMachine";
            })) {
                Archive.SetError("Missing animation state machine data");
            }

            return;
        }
    }

    static thread_local uint32 Depth{};

    if (Depth >= 16) {
        Archive.SetError("Animation state machine nesting limit exceeded");

        return;
    }

    uint32 Count{static_cast<uint32>(mStateMachines.size())};

    if (!Archive.BeginArray("StateMachines", Count)) {
        return;
    }

    if (Count > MaxNodes) {
        Archive.SetError("Too many animation state machines");
    }

    std::vector<int32> Ids{};

    if (!Archive.IsLoading()) {
        for (const auto& [Id, Machine] : mStateMachines) {
            Ids.push_back(Id);
        }

        std::sort(Ids.begin(), Ids.end());
    }

    ++Depth;

    for (uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        int32 Id{Archive.IsLoading() ? -1 : Ids[Index]};
        Archive.Serialize("Node", Id);
        const auto* Node{FindNode(Id)};

        if (Node == nullptr || Node->mType != "StateMachine" || (Archive.IsLoading() && mStateMachines.contains(Id))) {
            Archive.SetError("Invalid or duplicate state machine node");
        } else {
            mStateMachines[Id].Serialize(Archive);
        }

        Archive.EndObject();
    }

    --Depth;
    Archive.EndArray();

    if (!Archive.HasError()) {
        for (const auto& Node : mNodes) {
            if (Node.mType == "StateMachine" && !mStateMachines.contains(Node.mId)) {
                Archive.SetError("Missing animation state machine data");
            }
        }
    }
}

uint64 UAnimGraph::GetRevision() const {
    return mRevision;
}

void UAnimGraph::SerializeVariableGetters(FAnimAssetArchive& Archive) {
    uint32 Count{static_cast<uint32>(mVariableGetters.size())};

    if (!Archive.BeginArray("VariableGetters", Count)) {
        return;
    }

    if (Archive.IsLoading() && Count > MaxNodes) {
        Archive.SetError("Too many anim graph variable getters");
    }

    for (uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        FAnimGraphVariableGetter Getter{Archive.IsLoading() ? FAnimGraphVariableGetter{} : mVariableGetters[Index]};

        Archive.Serialize("Id", Getter.mId);
        Archive.Serialize("Variable", Getter.mVariable);
        Archive.Serialize("X", Getter.mX);
        Archive.Serialize("Y", Getter.mY);
        Archive.EndObject();

        if (Archive.IsLoading() && !Archive.HasError()) {
            const bool Valid{Getter.mId >= 0 && Getter.mId < std::numeric_limits<int32>::max() && FindVariableGetter(Getter.mId) == nullptr && !Getter.mVariable.empty() && std::isfinite(Getter.mX) && std::isfinite(Getter.mY) && std::any_of(mVariables.begin(), mVariables.end(), [&Getter](const FAnimGraphVariable& Variable) {
                return Variable.mName == Getter.mVariable;
            })};

            if (!Valid) {
                Archive.SetError("Invalid anim graph variable getter");
            } else {
                mNextGetterId = std::max(mNextGetterId, Getter.mId + 1);
                mVariableGetters.push_back(std::move(Getter));
            }
        }
    }

    Archive.EndArray();
}

void UAnimGraph::Serialize(FAnimAssetArchive& Archive) {
    uint32 Version{AnimGraphFormatVersion};

    Archive.Serialize("FormatVersion", Version);

    if (Archive.HasError()) {
        return;
    }

    if (Version != AnimGraphFormatVersion) {
        Archive.SetError("Unsupported anim graph version");

        return;
    }

    Archive.Serialize("OutputX", mOutputX);
    Archive.Serialize("OutputY", mOutputY);

    const bool LoadLegacyGetters{Archive.IsLoading() && !Archive.HasField("VariableGetters")};
    uint32 Count{static_cast<uint32>(mVariables.size())};

    if (!Archive.BeginArray("Variables", Count)) {
        return;
    }

    if (Archive.IsLoading()) {
        mVariables.assign(std::min<uint32>(Count, MaxNodes), FAnimGraphVariable{});
        mVariableGetters.clear();
        mNextGetterId = 0;
    }

    for (uint32 Index{}; Index < mVariables.size() && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        FAnimGraphVariable& Variable{mVariables[Index]};
        uint32 Source{static_cast<uint32>(Variable.mSource)};

        Archive.Serialize("Name", Variable.mName);
        Archive.Serialize("Default", Variable.mDefault);
        Archive.Serialize("Source", Source);

        if (!Archive.IsLoading() || Archive.HasField("Type")) {
            uint32 Type{static_cast<uint32>(Variable.mType)};
            Archive.Serialize("Type", Type);
            Archive.Serialize("BoolDefault", Variable.mBoolDefault);

            if (Type > static_cast<uint32>(EAnimGraphVariableType::Bool)) {
                Archive.SetError("Invalid anim graph variable type");
            } else {
                Variable.mType = static_cast<EAnimGraphVariableType>(Type);
            }
        }

        if (LoadLegacyGetters && Archive.HasField("GetterX") && Archive.HasField("GetterY")) {
            float X{};
            float Y{};

            Archive.Serialize("GetterX", X);
            Archive.Serialize("GetterY", Y);

            if (!Archive.HasError()) {
                AddVariableGetter(Variable.mName, X, Y);
            }
        }

        if (Archive.IsLoading()) {
            Variable.mSource = Source == static_cast<uint32>(EAnimGraphVariableSource::OwnerSpeed) ? EAnimGraphVariableSource::OwnerSpeed : EAnimGraphVariableSource::Manual;
        }

        Archive.EndObject();
    }

    Archive.EndArray();

    if (!LoadLegacyGetters && !Archive.HasError()) {
        SerializeVariableGetters(Archive);
    }

    Count = static_cast<uint32>(mNodes.size());

    if (Archive.HasError() || !Archive.BeginArray("Nodes", Count)) {
        return;
    }

    if (Archive.IsLoading()) {
        if (Count > MaxNodes) {
            Archive.SetError("Too many anim graph nodes");
            Archive.EndArray();

            return;
        }

        mNodes.assign(Count, FAnimGraphNode{});
    }

    for (uint32 Index{}; Index < Count && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        FAnimGraphNode& Node{mNodes[Index]};

        Archive.Serialize("Id", Node.mId);
        Archive.Serialize("Type", Node.mType);
        Archive.Serialize("Name", Node.mName);
        Archive.Serialize("X", Node.mX);
        Archive.Serialize("Y", Node.mY);

        uint32 ParameterCount{static_cast<uint32>(Node.mParameters.size())};

        if (Archive.BeginArray("Parameters", ParameterCount)) {
            if (Archive.IsLoading()) {
                Node.mParameters.assign(std::min<uint32>(ParameterCount, 64), FAnimGraphParameter{});
            }

            for (uint32 Parameter{}; Parameter < Node.mParameters.size() && !Archive.HasError(); ++Parameter) {
                if (!Archive.BeginObject(std::to_string(Parameter))) {
                    break;
                }

                FAnimGraphParameter& Value{Node.mParameters[Parameter]};

                Archive.Serialize("Name", Value.mName);
                Archive.Serialize("Value", Value.mValue);

                if (Archive.IsLoading() ? Archive.HasField("Getter") : Value.mGetterId >= 0) {
                    Archive.Serialize("Getter", Value.mGetterId);
                }

                Archive.EndObject();
            }

            Archive.EndArray();
        }

        Archive.EndObject();
    }

    Archive.EndArray();

    Count = static_cast<uint32>(mLinks.size());

    if (Archive.HasError() || !Archive.BeginArray("Links", Count)) {
        return;
    }

    if (Archive.IsLoading()) {
        mLinks.assign(std::min<uint32>(Count, MaxNodes * 4), FAnimGraphLink{});
    }

    for (uint32 Index{}; Index < mLinks.size() && !Archive.HasError(); ++Index) {
        if (!Archive.BeginObject(std::to_string(Index))) {
            break;
        }

        Archive.Serialize("From", mLinks[Index].mFromNode);
        Archive.Serialize("To", mLinks[Index].mToNode);
        Archive.Serialize("Pin", mLinks[Index].mToPin);
        Archive.EndObject();
    }

    Archive.EndArray();

    SerializeStateMachines(Archive);

    if (!Archive.IsLoading() || Archive.HasError()) {
        return;
    }
    // 저장된 연결도 편집 시와 같은 순환 검사와 입력 교체 규칙을 거쳐 복원한다.
    std::vector<FAnimGraphLink> Links{std::move(mLinks)};

    mLinks.clear();
    mNextNodeId = 0;

    for (std::size_t Index{}; Index < mNodes.size(); ++Index) {
        const int32 Id{mNodes[Index].mId};

        if (Id < 0 || Id == std::numeric_limits<int32>::max() || std::any_of(mNodes.begin(), mNodes.begin() + static_cast<std::ptrdiff_t>(Index), [Id](const FAnimGraphNode& Node) {
            return Node.mId == Id;
        })) {
            Archive.SetError("Invalid or duplicate anim graph node id");

            return;
        }

        mNextNodeId = std::max(mNextNodeId, Id + 1);
    }

    for (const FAnimGraphLink& Link : Links) {
        Connect(Link.mFromNode, Link.mToNode, Link.mToPin);
    }

    UpdateChildVariables({}, {});
}
