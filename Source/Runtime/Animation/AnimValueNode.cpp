#include "EnginePCH.h"
#include "Animation/AnimValueNode.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimInstance.h"

FAnimValueNode_Base::FAnimValueNode_Base(EAnimValueType ValueType)
    : mValueType{ValueType} {
}

EAnimValueType FAnimValueNode_Base::GetValueType() const {
    return mValueType;
}

const FString& FAnimValueNode_Base::GetDisplayName() const {
    return GetNodeType();
}

FAnimValueNode_Float::FAnimValueNode_Float()
    : FAnimValueNode_Base{EAnimValueType::Float} {
}

float FAnimValueNode_GetFloat::Evaluate(const FAnimationUpdateContext& Context) const {
    return Context.GetAnimInstance().GetFloatVariable(mVariableName);
}

const FString& FAnimValueNode_GetFloat::GetNodeType() const {
    static const FString Type{"Get Float"};
    return Type;
}

const FString& FAnimValueNode_GetFloat::GetDisplayName() const {
    return mVariableName;
}

FAnimValueLink_Base::FAnimValueLink_Base(EAnimValueType ValueType)
    : mValueType{ValueType} {
}

EAnimValueType FAnimValueLink_Base::GetValueType() const {
    return mValueType;
}

FAnimFloatLink::FAnimFloatLink()
    : FAnimValueLink_Base{EAnimValueType::Float} {
}

FAnimValueNode_Float* FAnimFloatLink::GetLinkedNode() const {
    return mLinkedNode;
}

bool FAnimFloatLink::SetLinkedNode(FAnimValueNode_Base* Node) {
    // 연결 시 타입을 확인해 이후 평가는 Float 노드로 안전하게 수행한다.
    if (Node != nullptr && Node->GetValueType() != GetValueType()) {
        return false;
    }

    mLinkedNode = static_cast<FAnimValueNode_Float*>(Node);
    return true;
}

float FAnimFloatLink::Evaluate(const FAnimationUpdateContext& Context, float DefaultValue) const {
    return mLinkedNode != nullptr ? mLinkedNode->Evaluate(Context) : DefaultValue;
}

