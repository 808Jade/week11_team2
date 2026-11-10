#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include <vector>

#include <functional>
#include <memory>

class UAnimSequenceBase;
class UBlendSpace;
struct FAnimGraphNode;

struct FAnimNode_Base;

enum class EAnimGraphParameterType : uint8 {

    Float,

    Bool,

    AnimSequence,

    BlendSpace
};

struct FAnimGraphParameterInfo {
public:

    FString mName{};

    EAnimGraphParameterType mType{EAnimGraphParameterType::Float};

    FString mDefault{};

    bool mBindable{};

    float mMin{};

    float mMax{};
};

struct FAnimGraphNodeBuildContext {
public:

    float GetFloat(const FString& Name) const;

    bool GetBool(const FString& Name) const;

    FString GetString(const FString& Name) const;

    FString GetBindingName(const FString& Name) const;

    const float* GetBinding(const FString& Name) const;

public:

    const FAnimGraphNode* mNode{};

    const std::vector<FAnimGraphParameterInfo>* mParameters{};

    std::function<const UAnimSequenceBase*(const FString&)> mLoadSequence{};

    std::function<const UBlendSpace*(const FString&)> mLoadBlendSpace{};

    std::function<const float*(const FString&)> mFindVariable{};
};

struct FAnimGraphNodeType {
public:

    FString mType{};

    FString mTitle{};

    std::vector<FString> mPoseInputs{};

    std::vector<FAnimGraphParameterInfo> mParameters{};

    std::function<std::unique_ptr<FAnimNode_Base>(const FAnimGraphNodeBuildContext&)> mCreate{};
};

class FAnimGraphNodeRegistry {
public:

    static const std::vector<FAnimGraphNodeType>& GetNodeTypes();

    static const FAnimGraphNodeType* Find(const FString& Type);
};
