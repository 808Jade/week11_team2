#pragma once

#include "Animation/AnimGraph.h"
#include "Animation/AnimInstance.h"

#include <memory>

#include "AnimGraphInstance.generated.h"

struct FAnimNode_AssetPlayerBase;

struct FAnimGraphRuntimeVariable {
public:

    FString mName{};

    float mValue{};

    EAnimGraphVariableSource mSource{EAnimGraphVariableSource::Manual};
};

UCLASS()
class UAnimGraphInstance final : public UAnimInstance {
    GENERATED_BODY()

public:
    UAnimGraphInstance();

    ~UAnimGraphInstance() override;

public:
    const FString& GetAnimGraph() const;
    bool SetAnimGraph(const FString& Path);

    bool SetFloatVariable(const FString& Name, float Value);

    float GetFloatVariable(const FString& Name) const;
    FAnimNode_AssetPlayerBase* FindAssetPlayer(int32 NodeId) const;

    void GetDebugVariables(std::vector<FAnimNodeValue>& OutValues) override;

    bool GetNodeLayout(const FAnimNode_Base* Node, float& X, float& Y) const override;
    const FAnimGraphVariableGetter* GetVariableGetter(int32 NodeId, const FString& Parameter) const;

protected:
    void NativeInitializeAnimation() override;

    void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    const UAnimGraph* mSourceGraph{};
    uint64 mGraphRevision{};

    FString mAnimGraph{};

    std::vector<std::unique_ptr<FAnimNode_Base>> mNodes{};

    std::vector<float> mNodePositions{};

    float mOutputX{};
    float mOutputY{};

    std::vector<FAnimGraphRuntimeVariable> mVariables{};
};
