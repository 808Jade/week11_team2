#include "EnginePCH.h"
#include "Animation/AnimGraphNodeRegistry.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimNode_TwoWayBlend.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace {
    const FString* FindDefault(const std::vector<FAnimGraphParameterInfo>* Parameters, const FString& Name) {
        if (Parameters == nullptr) {
            return nullptr;
        }

        const auto Found{std::find_if(Parameters->begin(), Parameters->end(), [&Name](const FAnimGraphParameterInfo& Info) {
            return Info.mName == Name;
        })};

        return Found != Parameters->end() ? &Found->mDefault : nullptr;
    }

    std::vector<FAnimGraphNodeType> CreateNodeTypes() {
        std::vector<FAnimGraphNodeType> Types{};

        const FAnimGraphParameterInfo PlayRate{"PlayRate", EAnimGraphParameterType::Float, "1", false, -3.0f, 3.0f};
        const FAnimGraphParameterInfo Loop{"Loop", EAnimGraphParameterType::Bool, "true"};

        Types.push_back({"SequencePlayer", "Sequence Player", {}, {{"Sequence", EAnimGraphParameterType::AnimSequence}, PlayRate, Loop}, [](const FAnimGraphNodeBuildContext& Context) {
            auto Node{std::make_unique<FAnimNode_SequencePlayer>()};

            Node->mSequence = Context.mLoadSequence ? Context.mLoadSequence(Context.GetString("Sequence")) : nullptr;
            Node->mPlayRate = Context.GetFloat("PlayRate");
            Node->mLoop = Context.GetBool("Loop");

            return std::unique_ptr<FAnimNode_Base>{std::move(Node)};
        }});

        Types.push_back({"TwoWayBlend", "Two-Way Blend", {"A", "B"}, {{"Alpha", EAnimGraphParameterType::Float, "0", true, 0.0f, 1.0f}}, [](const FAnimGraphNodeBuildContext& Context) {
            auto Node{std::make_unique<FAnimNode_TwoWayBlend>()};

            Node->mAlpha = Context.GetFloat("Alpha");
            Node->mAlphaBinding = Context.GetBinding("Alpha");
            Node->mAlphaBindingName = Context.GetBindingName("Alpha");

            return std::unique_ptr<FAnimNode_Base>{std::move(Node)};
        }});

        Types.push_back({"BlendSpacePlayer", "Blend Space Player", {}, {{"BlendSpace", EAnimGraphParameterType::BlendSpace}, {"X", EAnimGraphParameterType::Float, "0", true}, PlayRate, Loop}, [](const FAnimGraphNodeBuildContext& Context) {
            auto Node{std::make_unique<FAnimNode_BlendSpacePlayer>()};

            Node->mBlendSpace = Context.mLoadBlendSpace ? Context.mLoadBlendSpace(Context.GetString("BlendSpace")) : nullptr;
            Node->mX = Context.GetFloat("X");
            Node->mXBinding = Context.GetBinding("X");
            Node->mXBindingName = Context.GetBindingName("X");
            Node->mPlayRate = Context.GetFloat("PlayRate");
            Node->mLoop = Context.GetBool("Loop");

            return std::unique_ptr<FAnimNode_Base>{std::move(Node)};
        }});

        return Types;
    }
}

float FAnimGraphNodeBuildContext::GetFloat(const FString& Name) const {
    const FString Text{GetString(Name)};
    float Value{};
    const auto [End, Code]{std::from_chars(Text.data(), Text.data() + Text.size(), Value)};

    if (Code == std::errc{} && End == Text.data() + Text.size() && std::isfinite(Value)) {
        return Value;
    }

    const FString* Default{FindDefault(mParameters, Name)};

    if (Default != nullptr && std::from_chars(Default->data(), Default->data() + Default->size(), Value).ec == std::errc{}) {
        return Value;
    }

    return 0.0f;
}

bool FAnimGraphNodeBuildContext::GetBool(const FString& Name) const {
    return GetString(Name) == "true";
}

FString FAnimGraphNodeBuildContext::GetString(const FString& Name) const {
    if (mNode != nullptr) {
        if (const FString* Value{mNode->FindParameter(Name)}) {
            return *Value;
        }
    }

    const FString* Default{FindDefault(mParameters, Name)};

    return Default != nullptr ? *Default : FString{};
}

FString FAnimGraphNodeBuildContext::GetBindingName(const FString& Name) const {
    const FString* Value{mNode != nullptr ? mNode->FindParameter(Name + ".Binding") : nullptr};

    return Value != nullptr ? *Value : FString{};
}

const float* FAnimGraphNodeBuildContext::GetBinding(const FString& Name) const {
    const FString Variable{GetBindingName(Name)};

    return !Variable.empty() && mFindVariable ? mFindVariable(Variable) : nullptr;
}

const std::vector<FAnimGraphNodeType>& FAnimGraphNodeRegistry::GetNodeTypes() {
    static const std::vector<FAnimGraphNodeType> Types{CreateNodeTypes()};

    return Types;
}

const FAnimGraphNodeType* FAnimGraphNodeRegistry::Find(const FString& Type) {
    const auto& Types{GetNodeTypes()};
    const auto Found{std::find_if(Types.begin(), Types.end(), [&Type](const FAnimGraphNodeType& Entry) {
        return Entry.mType == Type;
    })};

    return Found != Types.end() ? &*Found : nullptr;
}
