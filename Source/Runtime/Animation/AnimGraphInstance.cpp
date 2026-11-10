#include "EnginePCH.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"
#include "Engine/World.h"
#include <unordered_map>
#include "Animation/AnimGraphNodeRegistry.h"
#include "Animation/AnimNode_AssetPlayerBase.h"
#include "Animation/AnimNode_Base.h"
#include "Components/SkeletalMeshComponent.h"

#include <algorithm>
#include <cmath>

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
        if (Variable.mName == Name) {
            Variable.mValue = std::isfinite(Value) ? Value : 0.0f;

            return true;
        }
    }

    return false;
}

float UAnimGraphInstance::GetFloatVariable(const FString& Name) const {
    for (const FAnimGraphRuntimeVariable& Variable : mVariables) {
        if (Variable.mName == Name) {
            return Variable.mValue;
        }
    }

    return 0.0f;
}

FAnimNode_AssetPlayerBase* UAnimGraphInstance::FindAssetPlayer(int32 NodeId) const {
    const auto Node{std::find_if(mNodes.begin(), mNodes.end(), [NodeId](const std::unique_ptr<FAnimNode_Base>& Candidate) {
        return Candidate->mGraphNodeId == NodeId;
    })};

    return Node != mNodes.end() ? dynamic_cast<FAnimNode_AssetPlayerBase*>(Node->get()) : nullptr;
}

void UAnimGraphInstance::GetDebugVariables(std::vector<FAnimNodeValue>& OutValues) {
    for (FAnimGraphRuntimeVariable& Variable : mVariables) {
        FAnimNodeValue Value{};

        Value.mName = Variable.mName;
        Value.mType = EAnimNodeValueType::Float;
        Value.mFloat = &Variable.mValue;
        Value.mBinding = Variable.mSource == EAnimGraphVariableSource::OwnerSpeed ? FString{"Owner Speed"} : FString{};
        OutValues.push_back(std::move(Value));
    }
}

bool UAnimGraphInstance::GetNodeLayout(const FAnimNode_Base* Node, float& X, float& Y) const {
    if (Node == nullptr) {
        X = mOutputX;
        Y = mOutputY;

        return !mNodes.empty();
    }

    for (std::size_t Index{}; Index < mNodes.size(); ++Index) {
        if (mNodes[Index].get() == Node) {
            X = mNodePositions[Index * 2];
            Y = mNodePositions[Index * 2 + 1];

            return true;
        }
    }

    return false;
}

const FAnimGraphVariableGetter* UAnimGraphInstance::GetVariableGetter(int32 NodeId, const FString& Parameter) const {
    const FAnimGraphNode* Node{mSourceGraph != nullptr && mGraphRevision == mSourceGraph->GetRevision() ? mSourceGraph->FindNode(NodeId) : nullptr};

    return Node != nullptr ? mSourceGraph->FindBoundGetter(*Node, Parameter) : nullptr;
}

void UAnimGraphInstance::NativeInitializeAnimation() {
    mSourceGraph = nullptr;
    mGraphRevision = {};
    SetRootNode(nullptr);
    mNodes.clear();
    mNodePositions.clear();
    mVariables.clear();

    const UAnimGraph* Graph{mAnimGraph.empty() ? nullptr : UAssetManager::GetAssetByPath<UAnimGraph>(mAnimGraph)};
    if (Graph == nullptr) {
        return;
    }

    mSourceGraph = Graph;
    mGraphRevision = Graph->GetRevision();
    mOutputX = Graph->GetOutputX();
    mOutputY = Graph->GetOutputY();

    mVariables.reserve(Graph->GetVariables().size());

    for (const FAnimGraphVariable& Variable : Graph->GetVariables()) {
        mVariables.push_back({Variable.mName, Variable.mDefault, Variable.mSource});
    }

    FAnimGraphNodeBuildContext Context{};

    Context.mLoadSequence = [](const FString& Path) {
        return Path.empty() ? nullptr : UAssetManager::GetAssetByPath<UAnimSequenceBase>(Path);
    };
    Context.mLoadBlendSpace = [](const FString& Path) {
        return Path.empty() ? nullptr : UAssetManager::GetAssetByPath<UBlendSpace>(Path);
    };
    Context.mFindVariable = [this](const FString& Name) -> const float* {
        for (const FAnimGraphRuntimeVariable& Variable : mVariables) {
            if (Variable.mName == Name) {
                return &Variable.mValue;
            }
        }

        return nullptr;
    };

    std::unordered_map<int32, FAnimNode_Base*> NodesById{};

    for (const FAnimGraphNode& Data : Graph->GetNodes()) {
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
        Node->mGraphNodeId = Data.mId;
        NodesById.emplace(Data.mId, Node.get());
        mNodePositions.push_back(Data.mX);
        mNodePositions.push_back(Data.mY);
        mNodes.push_back(std::move(Node));
    }

    for (const FAnimGraphLink& Link : Graph->GetLinks()) {
        const auto From{NodesById.find(Link.mFromNode)};

        if (From == NodesById.end()) {
            continue;
        }

        if (Link.mToNode == UAnimGraph::OutputNodeId) {
            SetRootNode(From->second);

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
}

void UAnimGraphInstance::NativeUpdateAnimation(float) {
    USkeletalMeshComponent* Owner{GetSkelMeshComponent()};
    const UWorld* World{Owner != nullptr ? Owner->GetWorld() : nullptr};

    if (World != nullptr && World->GetWorldType() == EWorldType::PIE && mSourceGraph != nullptr && mGraphRevision != mSourceGraph->GetRevision()) {
        NativeInitializeAnimation();

        if (GetRootNode() != nullptr) {
            GetRootNode()->Initialize(*this);
        }
    }

    const FVector& Velocity{GetOwnerVelocity()};
    const float Speed{std::sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y)};

    for (FAnimGraphRuntimeVariable& Variable : mVariables) {
        if (Variable.mSource == EAnimGraphVariableSource::OwnerSpeed) {
            Variable.mValue = Speed;
        }
    }
}
