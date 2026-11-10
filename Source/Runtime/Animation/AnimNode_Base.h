#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include <vector>
#include "Math/Transform.h"
#include "Containers/Array.h"

class UAnimInstance;
struct FAnimNode_Base;

struct FAnimationUpdateContext {
public:

    FAnimationUpdateContext(UAnimInstance& AnimInstance, float DeltaTime, uint64 UpdateCounter);

public:

    FAnimationUpdateContext FractionalWeight(float Multiplier) const;

    UAnimInstance& GetAnimInstance() const;

    float GetDeltaTime() const;

    float GetFinalBlendWeight() const;

    uint64 GetUpdateCounter() const;

private:

    UAnimInstance* mAnimInstance{};

    float mDeltaTime{};

    float mWeight{1.0f};

    uint64 mUpdateCounter{};
};

struct FPoseContext {
public:

    explicit FPoseContext(UAnimInstance& AnimInstance);
    FPoseContext(UAnimInstance& AnimInstance, TArray<FTransform>&& ReusablePose);

public:

    UAnimInstance& GetAnimInstance() const;

    void ResetToReferencePose();

public:

    TArray<FTransform> mPose{};

private:

    UAnimInstance* mAnimInstance{};
};

struct FPoseLink {
public:

    void Initialize(UAnimInstance& AnimInstance);

    void Update(const FAnimationUpdateContext& Context);

    void Evaluate(FPoseContext& Output);

    float GetDebugWeight(uint64 UpdateCounter) const;

public:

    FAnimNode_Base* mLinkedNode{};

private:
    uint64 mLastUpdateCounter{};
    float mLastWeight{};
};

struct FAnimNodePoseInput {
public:

    FString mName{};

    FPoseLink* mLink{};

    float mWeight{};
};

enum class EAnimNodeValueType : uint8 {

    Float,

    Bool,

    Text
};

struct FAnimNodeValue {
public:

    FString mName{};

    EAnimNodeValueType mType{EAnimNodeValueType::Text};

    float* mFloat{};

    bool* mBool{};

    FString mText{};

    float mMin{};

    float mMax{};

    FString mBinding{};
    const char* mBindingParameter{};
};

struct FAnimNode_Base {
public:
    FAnimNode_Base() = default;

    virtual ~FAnimNode_Base() = default;

    FAnimNode_Base(const FAnimNode_Base&) = delete;

    FAnimNode_Base& operator=(const FAnimNode_Base&) = delete;

public:

    virtual void Initialize(UAnimInstance& AnimInstance);

    virtual void Update(const FAnimationUpdateContext& Context);

    virtual void Evaluate(FPoseContext& Output);

    virtual const char* GetNodeType() const = 0;

    virtual void GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs);

    virtual void GetDebugValues(std::vector<FAnimNodeValue>& OutValues);

    float GetDebugWeight(uint64 UpdateCounter) const;

    void RecordUpdate(const FAnimationUpdateContext& Context);

public:

    FString mName{};

    int32 mGraphNodeId{-1};

private:

    float mLastWeight{};

    uint64 mLastUpdateCounter{};
};
