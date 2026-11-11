#include "EnginePCH.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include <unordered_map>
#include "Asset/FbxImporter/FbxPath.h"

#include "Engine/World.h"
#include "Animation/AnimGraphNodeRegistry.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimValueNode.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Components/SkeletalMeshComponent.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

UAnimGraphInstance::UAnimGraphInstance() = default;

UAnimGraphInstance::~UAnimGraphInstance() = default;

const FString& UAnimGraphInstance::GetAnimGraph() const {
    return mAnimGraph;
}

bool UAnimGraphInstance::SetAnimGraph(const FString& Path) {
    const FString Key{FAssetUtils::GetAssetKey(Path)};
    if (!Path.empty() && (Key.empty() || UAssetManager::GetAssetByPath<UAnimGraph>(Key) == nullptr)) {
        return false;
    }

    mAnimGraph = Key;

    return true;
}

bool UAnimGraphInstance::SetFloatVariable(const FString& Name, float Value) {
    for (FAnimGraphRuntimeVariable& Variable : mVariables) {
        if (Variable.mName == Name && Variable.mType == EAnimGraphVariableType::Float) {
            Variable.mValue = std::isfinite(Value) ? Value : 0.0f;

            return true;
        }
    }

    return false;
}

float UAnimGraphInstance::GetFloatVariable(const FString& Name) const {
    for (const FAnimGraphRuntimeVariable& Variable : mVariables) {
        if (Variable.mName == Name && Variable.mType == EAnimGraphVariableType::Float) {
            return Variable.mValue;
        }
    }

    return 0.0f;
}

bool UAnimGraphInstance::SetBoolVariable(const FString& Name, bool Value) {
    for (auto& Variable : mVariables) {
        if (Variable.mName == Name && Variable.mType == EAnimGraphVariableType::Bool) {
            Variable.mBoolValue = Value;

            return true;
        }
    }

    return false;
}

bool UAnimGraphInstance::GetBoolVariable(const FString& Name) const {
    for (const auto& Variable : mVariables) {
        if (Variable.mName == Name && Variable.mType == EAnimGraphVariableType::Bool) {
            return Variable.mBoolValue;
        }
    }

    return false;
}

bool UAnimGraphInstance::HasGraphVariable(const FString& Name, bool IsBool) const {
    return std::any_of(mVariables.begin(), mVariables.end(), [&Name, IsBool](const auto& Variable) {
        return Variable.mName == Name && (Variable.mType == EAnimGraphVariableType::Bool) == IsBool;
    });
}

EAnimGraphVariableSource UAnimGraphInstance::GetVariableSource(const FString& Name) const {
    const auto Variable{std::find_if(mVariables.begin(), mVariables.end(), [&Name](const FAnimGraphRuntimeVariable& Candidate) {
        return Candidate.mName == Name;
    })};

    return Variable != mVariables.end() ? Variable->mSource : EAnimGraphVariableSource::Manual;
}

FAnimNode_AssetPlayerBase* UAnimGraphInstance::FindAssetPlayer(const FString& Name) const {
    // 중첩 그래프에서 이름이 겹치면 재시작할 대상을 임의로 선택하지 않는다.
    if (Name.empty()) {
        return nullptr;
    }

    FAnimNode_AssetPlayerBase* Player{};

    for (const std::unique_ptr<FAnimNode_Base>& Node : mNodes) {
        if (Node == nullptr || Node->mName != Name) {
            continue;
        }

        auto* Candidate{dynamic_cast<FAnimNode_AssetPlayerBase*>(Node.get())};

        if (Candidate != nullptr) {
            if (Player != nullptr) {
                return nullptr;
            }

            Player = Candidate;
        }
    }

    return Player;
}

void UAnimGraphInstance::GetDebugVariables(std::vector<FAnimNodeValue>& OutValues) {
    for (FAnimGraphRuntimeVariable& Variable : mVariables) {
        FAnimNodeValue Value{};

        Value.mName = Variable.mName;
        if (Variable.mType == EAnimGraphVariableType::Bool) {
            Value.mValue = &Variable.mBoolValue;
        } else {
            Value.mValue = FAnimNodeFloatValue{&Variable.mValue};
        }
        OutValues.push_back(std::move(Value));
    }
}

bool UAnimGraphInstance::GetNodeLayout(const FAnimNode_Base* Node, float& X, float& Y) const {
    if (mSourceGraph == nullptr || mGraphRevision != mSourceGraph->GetRevision()) {
        return false;
    }

    if (Node == nullptr) {
        X = mSourceGraph->GetOutputX();
        Y = mSourceGraph->GetOutputY();

        return std::any_of(mNodes.begin(), mNodes.end(), [](const auto& Candidate) {
            return Candidate != nullptr;
        });
    }

    const FAnimGraphNode* Source{GetSourceNode(Node)};

    if (Source == nullptr) {
        return false;
    }

    X = Source->mX;
    Y = Source->mY;

    const auto Found{std::find_if(mNodes.begin(), mNodes.end(), [Node](const auto& Candidate) {
        return Candidate.get() == Node;
    })};

    return Found != mNodes.end() && mNodeGraphs[static_cast<std::size_t>(Found - mNodes.begin())] == mSourceGraph;
}

const FAnimGraphNode* UAnimGraphInstance::GetSourceNode(const FAnimNode_Base* Node) const {
    // 원본 배열이 교체된 뒤에는 이전 요소를 가리키는 포인터를 사용하지 않는다.
    if (Node == nullptr || mSourceGraph == nullptr || mGraphRevision != mSourceGraph->GetRevision()) {
        return nullptr;
    }

    const auto Found{std::find_if(mNodes.begin(), mNodes.end(), [Node](const auto& Candidate) {
        return Candidate.get() == Node;
    })};

    return Found != mNodes.end() ? mNodeSources[static_cast<std::size_t>(Found - mNodes.begin())] : nullptr;
}

const FAnimGraphVariableGetter* UAnimGraphInstance::GetSourceGetter(const FAnimValueNode_Base* Node) const {
    if (Node == nullptr || mSourceGraph == nullptr || mGraphRevision != mSourceGraph->GetRevision()) {
        return nullptr;
    }

    const auto Found{std::find_if(mValueNodes.begin(), mValueNodes.end(), [Node](const auto& Candidate) {
        return Candidate.get() == Node;
    })};

    if (Found == mValueNodes.end()) {
        return nullptr;
    }

    const std::size_t Index{static_cast<std::size_t>(Found - mValueNodes.begin())};
    return Index < mGetterSources.size() ? mGetterSources[Index] : nullptr;
}

uint64 UAnimGraphInstance::GetNodeKey(const FAnimNode_Base* Node) const {
    const auto Found{std::find_if(mNodes.begin(), mNodes.end(), [Node](const auto& Candidate) {
        return Candidate.get() == Node;
    })};

    return Found != mNodes.end() ? static_cast<uint64>(Found - mNodes.begin()) + 1 : 0;
}

uint64 UAnimGraphInstance::GetValueNodeKey(const FAnimValueNode_Base* Node) const {
    const auto Found{std::find_if(mValueNodes.begin(), mValueNodes.end(), [Node](const auto& Candidate) {
        return Candidate.get() == Node;
    })};

    return Found != mValueNodes.end() ? static_cast<uint64>(Found - mValueNodes.begin()) + 1 : 0;
}

const UAnimGraph* UAnimGraphInstance::GetSourceGraph() const {
    return mSourceGraph != nullptr && mGraphRevision == mSourceGraph->GetRevision() ? mSourceGraph : nullptr;
}

uint64 UAnimGraphInstance::GetGraphGeneration() const {
    return mGraphGeneration;
}

FAnimNode_Base* UAnimGraphInstance::FindRuntimeNode(const UAnimGraph& Graph, int32 NodeId) const {
    if (GetSourceGraph() == nullptr) {
        return nullptr;
    }

    for (std::size_t Index{}; Index < mNodes.size(); ++Index) {
        if (mNodeGraphs[Index] == &Graph && mNodeSources[Index]->mId == NodeId) {
            return mNodes[Index].get();
        }
    }

    return nullptr;
}

void UAnimGraphInstance::NativeInitializeAnimation() {
    ++mGraphGeneration;
    mSourceGraph = nullptr;
    mGraphRevision = {};
    SetRootNode(nullptr);
    mNodes.clear();
    mValueNodes.clear();
    mNodeSources.clear();
    mNodeGraphs.clear();
    mGetterSources.clear();
    mVariables.clear();

    const UAnimGraph* Graph{mAnimGraph.empty() ? nullptr : UAssetManager::GetAssetByPath<UAnimGraph>(mAnimGraph)};

    if (Graph == nullptr) {
        return;
    }

    mSourceGraph = Graph;
    mGraphRevision = Graph->GetRevision();

    mVariables.reserve(Graph->GetVariables().size());

    for (const FAnimGraphVariable& Variable : Graph->GetVariables()) {
        mVariables.push_back({Variable.mName, Variable.mDefault, Variable.mSource, Variable.mType, Variable.mBoolDefault});
    }

    FAnimGraphNodeBuildContext Context{};

    Context.mLoadSequence = [](const FString& Path) {
        return Path.empty() ? nullptr : UAssetManager::GetAssetByPath<UAnimSequenceBase>(Path);
    };
    Context.mLoadBlendSpace = [](const FString& Path) {
        return Path.empty() ? nullptr : UAssetManager::GetAssetByPath<UBlendSpace>(Path);
    };
    SetRootNode(BuildGraph(*Graph, Context, 0));
}

FAnimNode_Base* UAnimGraphInstance::BuildGraph(const UAnimGraph& Source, const FAnimGraphNodeBuildContext& BaseContext, uint32 Depth) {
    // 상태 머신 안에 다시 상태 머신이 들어갈 수 있으므로 중첩 생성 깊이를 제한한다.
    if (Depth >= 16) {
        return nullptr;
    }

    const UAnimGraph* Graph{&Source};
    FAnimGraphNodeBuildContext Context{BaseContext};
    FAnimNode_Base* Root{};
    const auto& Getters{Graph->GetVariableGetters()};
    // 내부 그래프도 같은 소유 배열에 추가되므로 현재 그래프의 Get 노드 시작 위치를 보관한다.
    const std::size_t GetterOffset{mValueNodes.size()};

    for (const FAnimGraphVariableGetter& Data : Getters) {
        auto Getter{std::make_unique<FAnimValueNode_GetFloat>()};
        Getter->mVariableName = Data.mVariable;
        mValueNodes.push_back(std::move(Getter));
        mGetterSources.push_back(&Data);
    }
    std::unordered_map<int32, FAnimNode_Base*> NodesById{};

    const auto& SourceNodes{Graph->GetNodes()};
    std::vector<FAnimNode_Base*> LocalNodes{};
    LocalNodes.resize(SourceNodes.size());

    for (std::size_t Index{}; Index < SourceNodes.size(); ++Index) {
        const FAnimGraphNode& Data{SourceNodes[Index]};
        const FAnimGraphNodeType* Type{FAnimGraphNodeRegistry::Find(Data.mType)};

        if (Type == nullptr || !Type->mCreate) {
            HTR_LOG(Warning, "Unknown node type {} in anim graph {}", Data.mType, mAnimGraph);

            continue;
        }

        Context.mNode = &Data;
        Context.mParameters = &Type->mParameters;

        std::unique_ptr<FAnimNode_Base> Node{Type->mCreate(Context)};

        if (Node == nullptr) {
            continue;
        }

        Node->mName = Data.mName;
        NodesById.emplace(Data.mId, Node.get());
        LocalNodes[Index] = Node.get();
        mNodes.push_back(std::move(Node));
        mNodeSources.push_back(&Data);
        mNodeGraphs.push_back(Graph);

        // 저장된 상태 머신 구성에서 상태별 포즈 그래프를 재귀적으로 실행 노드로 만든다.
        if (auto* Machine{dynamic_cast<FAnimNode_StateMachine*>(LocalNodes[Index])}) {
            const auto* Definition{Graph->FindStateMachine(Data.mId)};

            if (Definition != nullptr) {
                Machine->Configure(*Definition);

                for (const auto& State : Definition->GetStates()) {
                    Machine->AddState(State, State.mGraph != nullptr ? BuildGraph(*State.mGraph, BaseContext, Depth + 1) : nullptr);
                }
            }
        }
    }
    // 명시적 Get 노드가 없는 이름 기반 바인딩도 실행 가능한 변수 입력으로 만든다.
    std::unordered_map<FString, FAnimValueNode_Base*> ImplicitGetters{};

    for (std::size_t Index{}; Index < LocalNodes.size(); ++Index) {
        if (LocalNodes[Index] == nullptr) {
            continue;
        }

        std::vector<FAnimNodeValueInput> Inputs{};
        LocalNodes[Index]->GetValueInputs(Inputs);

        for (const FAnimNodeValueInput& Input : Inputs) {
            const FString* VariableName{SourceNodes[Index].FindParameter(Input.mName + ".Binding")};

            if (VariableName == nullptr || std::none_of(mVariables.begin(), mVariables.end(), [VariableName](const FAnimGraphRuntimeVariable& Variable) {
                return Variable.mName == *VariableName && Variable.mType == EAnimGraphVariableType::Float;
            })) {
                continue;
            }

            const FAnimGraphVariableGetter* SourceGetter{Graph->FindBoundGetter(SourceNodes[Index], Input.mName)};

            if (SourceGetter != nullptr) {
                const std::size_t GetterIndex{static_cast<std::size_t>(SourceGetter - Getters.data())};
                Input.mLink->SetLinkedNode(mValueNodes[GetterOffset + GetterIndex].get());
                continue;
            }

            FAnimValueNode_Base*& Getter{ImplicitGetters[*VariableName]};

            if (Getter == nullptr) {
                auto Node{std::make_unique<FAnimValueNode_GetFloat>()};
                Node->mVariableName = *VariableName;
                Getter = Node.get();
                mValueNodes.push_back(std::move(Node));
                mGetterSources.push_back(nullptr);
            }

            Input.mLink->SetLinkedNode(Getter);
        }
    }

    // 모든 노드를 먼저 생성한 뒤 ID를 실제 포인터로 바꿔 포즈 입력과 최종 출력을 연결한다.
    for (const FAnimGraphLink& Link : Graph->GetLinks()) {
        const auto From{NodesById.find(Link.mFromNode)};

        if (From == NodesById.end()) {
            continue;
        }

        if (Link.mToNode == UAnimGraph::OutputNodeId) {
            Root = From->second;

            continue;
        }

        const auto To{NodesById.find(Link.mToNode)};

        if (To == NodesById.end()) {
            continue;
        }

        std::vector<FAnimNodePoseInput> Inputs{};

        To->second->GetPoseInputs(Inputs);

        if (Link.mToPin >= 0 && static_cast<std::size_t>(Link.mToPin) < Inputs.size()) {
            Inputs[static_cast<std::size_t>(Link.mToPin)].mLink->mLinkedNode = From->second;
        }
    }
    return Root;
}

void UAnimGraphInstance::NativeUpdateAnimation(float) {
    USkeletalMeshComponent* Owner{GetSkelMeshComponent()};
    const UWorld* World{Owner != nullptr ? Owner->GetWorld() : nullptr};

    // PIE에서 저장된 에셋이 교체되면 실행 노드를 다시 구성하고 재생 상태를 초기화한다.
    if (World != nullptr && World->GetWorldType() == EWorldType::PIE && mSourceGraph != nullptr && mGraphRevision != mSourceGraph->GetRevision()) {
        NativeInitializeAnimation();

        if (GetRootNode() != nullptr) {
            GetRootNode()->Initialize(*this);
        }
    }

    const FVector& Velocity{GetOwnerVelocity()};
    const float Speed{std::sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y)};

    for (FAnimGraphRuntimeVariable& Variable : mVariables) {
        if (Variable.mSource == EAnimGraphVariableSource::OwnerSpeed && Variable.mType == EAnimGraphVariableType::Float) {
            Variable.mValue = Speed;
        }
    }
}
