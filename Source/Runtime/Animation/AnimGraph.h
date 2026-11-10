#pragma once

#include "Asset/RenderAsset.h"
#include <vector>

class FAnimAssetArchive;

#include "AnimGraph.generated.h"

struct FAnimGraphParameter {
public:

    FString mName{};

    FString mValue{};
    int32 mGetterId{-1};
};

struct FAnimGraphNode {
public:

    const FString* FindParameter(const FString& Name) const;

    void SetParameter(const FString& Name, const FString& Value, int32 GetterId = -1);

public:

    int32 mId{};

    FString mType{};

    FString mName{};

    float mX{};

    float mY{};

    std::vector<FAnimGraphParameter> mParameters{};
};

struct FAnimGraphLink {
public:

    int32 mFromNode{};

    int32 mToNode{};

    int32 mToPin{};
};

enum class EAnimGraphVariableSource : uint8 {

    Manual,

    OwnerSpeed
};

struct FAnimGraphVariable {
public:

    FString mName{};

    float mDefault{};

    EAnimGraphVariableSource mSource{EAnimGraphVariableSource::Manual};
};

struct FAnimGraphVariableGetter {
public:
    int32 mId{};
    FString mVariable{};
    float mX{};
    float mY{};
};

UCLASS()
class UAnimGraph final : public URenderAsset {
    GENERATED_BODY()

public:

    static constexpr int32 OutputNodeId{-1};

    static constexpr uint32 MaxNodes{256};

public:
    UAnimGraph() = default;

    ~UAnimGraph() override = default;

public:
    const std::vector<FAnimGraphNode>& GetNodes() const;
    const std::vector<FAnimGraphLink>& GetLinks() const;
    const std::vector<FAnimGraphVariable>& GetVariables() const;

    FAnimGraphNode* FindNode(int32 Id);
    const FAnimGraphNode* FindNode(int32 Id) const;

    int32 AddNode(const FString& Type, const FString& Name, float X, float Y);

    bool RemoveNode(int32 Id);

    bool Connect(int32 FromNode, int32 ToNode, int32 ToPin);

    bool Disconnect(int32 ToNode, int32 ToPin);

    bool WouldCreateCycle(int32 FromNode, int32 ToNode) const;

    int32 FindInput(int32 ToNode, int32 ToPin) const;

    void SetNodePosition(int32 Id, float X, float Y);

    float GetOutputX() const;

    float GetOutputY() const;

    bool AddVariable(const FAnimGraphVariable& Variable);

    bool SetVariable(int32 Index, const FAnimGraphVariable& Variable);

    bool RemoveVariable(int32 Index);

    const std::vector<FAnimGraphVariableGetter>& GetVariableGetters() const;
    const FAnimGraphVariableGetter* FindVariableGetter(int32 Id) const;
    const FAnimGraphVariableGetter* FindBoundGetter(const FAnimGraphNode& Node, const FString& Parameter) const;
    int32 AddVariableGetter(const FString& Variable, float X, float Y);
    bool SetVariableGetterPosition(int32 Id, float X, float Y);
    bool RemoveVariableGetter(int32 Id);

    void CopyFrom(const UAnimGraph& Source);

    uint64 GetRevision() const;

    void Serialize(FAnimAssetArchive& Archive);

private:
    const FAnimGraphVariableGetter* ResolveGetter(const FAnimGraphParameter& Binding) const;
    void SerializeVariableGetters(FAnimAssetArchive& Archive);

private:
    uint64 mRevision{};

    std::vector<FAnimGraphNode> mNodes{};

    std::vector<FAnimGraphLink> mLinks{};

    std::vector<FAnimGraphVariable> mVariables{};
    std::vector<FAnimGraphVariableGetter> mVariableGetters{};
    int32 mNextGetterId{};

    int32 mNextNodeId{};

    float mOutputX{400.0f};

    float mOutputY{0.0f};
};
