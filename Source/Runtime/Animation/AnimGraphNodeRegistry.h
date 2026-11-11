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

    // 노드 속성 조회와 편집 UI에서 사용하는 파라미터 이름이다.
    FString mName{};

    // Float, Bool 또는 애니메이션 에셋 선택 중 사용할 속성 종류이다.
    EAnimGraphParameterType mType{EAnimGraphParameterType::Float};

    // 노드 정의에 값이 없을 때 해석할 기본 문자열이다.
    FString mDefault{};

    // 이 속성에 변수 값을 제공하는 Get 노드를 연결할 수 있는지 지정한다.
    bool mBindable{};

    // Float 편집 UI에 제공할 최솟값이다.
    float mMin{};

    // Float 편집 UI에 제공할 최댓값이다.
    float mMax{};
};

// 저장된 파라미터의 기본값 해석과 에셋 로더를 노드 생성 함수에 전달한다.
struct FAnimGraphNodeBuildContext {
public:

    // 저장 문자열을 유한한 Float로 해석하고 실패하면 등록된 기본값을 사용한다.
    float GetFloat(const FString& Name) const;

    // 파라미터 또는 기본 문자열이 true인지 해석한다.
    bool GetBool(const FString& Name) const;

    // 현재 노드의 파라미터를 우선 반환하고 없으면 타입의 기본값을 사용한다.
    FString GetString(const FString& Name) const;

public:

    // 현재 실행 객체로 생성할 원본 포즈 노드 정의이다.
    const FAnimGraphNode* mNode{};

    // 현재 노드 타입의 속성 종류 및 기본값 목록이다.
    const std::vector<FAnimGraphParameterInfo>* mParameters{};

    // 시퀀스 경로를 공유 시퀀스 에셋으로 변환하는 외부 로더이다.
    std::function<const UAnimSequenceBase*(const FString&)> mLoadSequence{};

    // Blend Space 경로를 공유 에셋으로 변환하는 외부 로더이다.
    std::function<const UBlendSpace*(const FString&)> mLoadBlendSpace{};
};

// 에디터의 핀과 속성 표시, 런타임 노드 생성을 하나의 타입 정의에서 공유한다.
struct FAnimGraphNodeType {
public:

    // 직렬화된 노드 정의와 레지스트리를 연결하는 고유 타입 키이다.
    FString mType{};

    // 생성 메뉴와 노드 헤더에 표시할 타입 이름이다.
    FString mTitle{};

    // 노드가 제공하는 포즈 입력 핀의 이름과 순서이다.
    std::vector<FString> mPoseInputs{};

    // 편집 UI와 실행 생성이 공유하는 파라미터 메타데이터이다.
    std::vector<FAnimGraphParameterInfo> mParameters{};

    // 빌드 컨텍스트로 새 실행 노드를 생성하고 소유권을 반환하는 팩터리이다.
    std::function<std::unique_ptr<FAnimNode_Base>(const FAnimGraphNodeBuildContext&)> mCreate{};
};

class FAnimGraphNodeRegistry {
public:

    // 에디터 생성 메뉴와 실행 그래프 구성이 공유하는 노드 타입 목록을 반환한다.
    static const std::vector<FAnimGraphNodeType>& GetNodeTypes();

    // 저장된 타입 키에 대응하는 노드 메타데이터와 생성 함수를 찾는다.
    static const FAnimGraphNodeType* Find(const FString& Type);
};
