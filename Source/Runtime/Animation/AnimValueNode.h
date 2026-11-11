#pragma once

#include "Core/EngineString.h"

struct FAnimationUpdateContext;

enum class EAnimValueType : uint8 {
    Float,
    Bool
};

// 본 포즈 대신 Float 등의 값을 제공하는 노드의 공통 타입 정보이다.
struct FAnimValueNode_Base {
public:
    // 값 노드가 출력할 타입을 생성 시 고정한다.
    explicit FAnimValueNode_Base(EAnimValueType ValueType);
    // 기반 포인터로 파생 값 노드를 안전하게 소멸한다.
    virtual ~FAnimValueNode_Base() = default;

public:
    // 연결 타입 검사에 사용할 출력 값 타입을 반환한다.
    EAnimValueType GetValueType() const;

    // 디버그 표시용 값 노드 타입 이름을 파생 구현에서 반환한다.
    virtual const FString& GetNodeType() const = 0;
    // 노드 타입 또는 조회 변수처럼 화면에 표시할 이름을 반환한다.
    virtual const FString& GetDisplayName() const;

private:
    // 생성 이후 바뀌지 않는 이 값 노드의 출력 타입이다.
    const EAnimValueType mValueType{};
};

struct FAnimValueNode_Float : public FAnimValueNode_Base {
public:
    // 출력 타입을 Float로 지정해 값 노드를 생성한다.
    FAnimValueNode_Float();

public:
    // 현재 인스턴스와 갱신 문맥에 맞는 Float 값을 계산한다.
    virtual float Evaluate(const FAnimationUpdateContext& Context) const = 0;
};

// 에셋의 기본값이 아니라 현재 애니메이션 인스턴스의 변수 값을 읽는다.
struct FAnimValueNode_GetFloat : public FAnimValueNode_Float {
public:
    // 현재 애니메이션 인스턴스에서 지정 변수의 Float 값을 읽는다.
    float Evaluate(const FAnimationUpdateContext& Context) const override;
    // 디버그 표시용 Get Float 타입 이름을 반환한다.
    const FString& GetNodeType() const override;
    // 타입 이름 대신 실제 조회할 변수 이름을 표시한다.
    const FString& GetDisplayName() const override;

public:
    // 실행 인스턴스에서 현재 값을 조회할 Float 변수 이름이다.
    FString mVariableName{};
};

struct FAnimValueLink_Base {
public:
    // 연결이 허용할 값 타입을 생성 시 고정한다.
    explicit FAnimValueLink_Base(EAnimValueType ValueType);
    // 기반 포인터로 파생 값 링크를 안전하게 소멸한다.
    virtual ~FAnimValueLink_Base() = default;

public:
    // 이 입력 링크가 받을 수 있는 값 타입을 반환한다.
    EAnimValueType GetValueType() const;

    // 연결된 값 노드의 비소유 포인터를 파생 링크에서 제공한다.
    virtual FAnimValueNode_Base* GetLinkedNode() const = 0;
    // 출력과 입력 타입이 호환되는 경우에만 값 노드를 연결한다.
    virtual bool SetLinkedNode(FAnimValueNode_Base* Node) = 0;

private:
    // 생성 이후 바뀌지 않는 이 입력 링크의 허용 타입이다.
    const EAnimValueType mValueType{};
};

// 연결된 Float 노드를 평가하고, 연결이 없으면 포즈 노드에 설정된 상수를 사용한다.
struct FAnimFloatLink : public FAnimValueLink_Base {
public:
    // 입력 타입이 Float인 비어 있는 값 링크를 생성한다.
    FAnimFloatLink();

public:
    // 연결된 Float 실행 노드의 비소유 포인터를 반환한다.
    FAnimValueNode_Float* GetLinkedNode() const override;
    // Float 타입을 확인해 연결하거나 nullptr로 연결을 해제한다.
    bool SetLinkedNode(FAnimValueNode_Base* Node) override;

    // 연결 노드의 Float 값을 읽고 미연결 상태에서는 전달받은 기본값을 반환한다.
    float Evaluate(const FAnimationUpdateContext& Context, float DefaultValue) const;

private:
    // 이 입력에 값을 공급하는 Float 노드의 비소유 포인터이다.
    FAnimValueNode_Float* mLinkedNode{};
};

