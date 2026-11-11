#pragma once

#include "Core/Types.h"
#include "Animation/AnimValueNode.h"
#include "Core/EngineString.h"
#include <variant>
#include <vector>
#include "Math/Transform.h"
#include "Containers/Array.h"

class UAnimInstance;
struct FAnimNode_Base;
struct FAnimNotifyQueue;

// 한 프레임의 root 이동과 회전 중심은 컴포넌트 공간 기준이다.
struct FRootMotionDelta {
public:
    FVector mTranslation{};
    FQuat mRotation{FQuat::Identity()};
    // 포즈에서 고정된 루트 본 위치. 액터를 이 점을 중심으로 돌려야 루트 본이 제자리에서 회전한다.
    FVector mRotationPivot{};
};

// 노드 갱신에 필요한 인스턴스와 프레임 시간을 함께 전달한다.
struct FAnimationUpdateContext {
public:

    // 갱신 대상 인스턴스와 이번 프레임의 시간 간격을 묶어 생성한다.
    FAnimationUpdateContext(UAnimInstance& AnimInstance, float DeltaTime, FAnimNotifyQueue* NotifyQueue = nullptr);

public:

    // 이 갱신을 수행하는 애니메이션 인스턴스를 반환한다.
    UAnimInstance& GetAnimInstance() const;

    // 이번 갱신에서 진행할 시간 간격을 초 단위로 반환한다.
    float GetDeltaTime() const;
    FAnimNotifyQueue* GetNotifyQueue() const;

private:

    // 변수 조회와 공유 노드 갱신을 담당하는 인스턴스의 비소유 포인터이다.
    UAnimInstance* mAnimInstance{};

    // 노드의 재생 시간을 진행할 프레임 간격이다.
    float mDeltaTime{};
    FAnimNotifyQueue* mNotifyQueue{};
};

// 본 로컬 변환 배열을 평가 결과로 전달하며, 연결이 없으면 기준 포즈로 복원한다.
struct FPoseContext {
public:

    // 인스턴스의 스켈레톤에 맞는 기준 포즈로 평가 버퍼를 생성한다.
    explicit FPoseContext(UAnimInstance& AnimInstance);
    // 이전 포즈 배열의 저장 공간을 넘겨받고 기준 포즈로 초기화한다.
    FPoseContext(UAnimInstance& AnimInstance, TArray<FTransform>&& ReusablePose);

public:

    // 시퀀스 평가와 본 정보 조회에 사용할 인스턴스를 반환한다.
    UAnimInstance& GetAnimInstance() const;

    // 모든 본 로컬 변환을 스켈레톤의 기준 포즈로 복원한다.
    void ResetToReferencePose();

public:

    // 평가한 본 로컬 변환 배열이며 본 컨테이너와 같은 인덱스를 사용한다.
    TArray<FTransform> mPose{};

private:

    // 본 정보와 시퀀스 샘플링 기능을 제공하는 인스턴스의 비소유 포인터이다.
    UAnimInstance* mAnimInstance{};
};

// 실행 노드를 소유하지 않고 연결하며, 갱신 중복 방지와 평가 방문 기록을 중계한다.
struct FPoseLink {
public:

    // 연결된 포즈 노드와 하위 입력의 재생 상태를 초기화한다.
    void Initialize(UAnimInstance& AnimInstance);

    // 인스턴스의 중복 갱신 방지를 거쳐 연결 노드의 시간을 진행한다.
    void Update(const FAnimationUpdateContext& Context);

    // 연결된 포즈를 평가하고 로컬 기여도를 기록하며, 연결이 없으면 기준 포즈를 출력한다.
    void Evaluate(FPoseContext& Output, float Weight = 1.0f);

public:

    // 입력 포즈를 생성하는 실행 노드의 비소유 포인터이다.
    FAnimNode_Base* mLinkedNode{};
};

struct FAnimNodePoseInput {
public:

    // 에디터와 디버그 패널에 표시할 포즈 입력 핀의 이름이다.
    FString mName{};

    // 실행 노드가 소유한 해당 입력의 포즈 링크를 가리킨다.
    FPoseLink* mLink{};
};

struct FAnimNodeValueInput {
public:
    // 파라미터 바인딩을 찾을 때 사용하는 값 입력의 이름이다.
    FString mName{};
    // 변수 Get 노드 등의 값 출력을 연결할 입력 링크이다.
    FAnimValueLink_Base* mLink{};
};

struct FAnimNodeFloatValue {
public:

    // 디버그 UI에서 읽거나 수정할 실제 Float 값의 주소이다.
    float* mValue{};

    // 디버그 슬라이더에 사용할 최솟값이며 범위가 없으면 UI가 자유 입력을 사용한다.
    float mMin{};

    // 디버그 슬라이더에 사용할 최댓값이다.
    float mMax{};
};

// 문자열은 표시용이고, 값 포인터는 PIE 디버그 패널에서 실행 값을 직접 편집할 때 사용한다.
struct FAnimNodeValue {
public:

    // 디버그 속성에 표시할 이름이다.
    FString mName{};

    // 표시 전용 문자열 또는 직접 편집할 Float 및 Bool 값의 주소이다.
    std::variant<FString, FAnimNodeFloatValue, bool*> mValue{};
};

// Initialize로 재생 상태를 준비하고, Update로 시간을 진행한 뒤 Evaluate로 포즈를 만든다.
struct FAnimNode_Base {
public:
    // 기본 실행 노드를 생성한다.
    FAnimNode_Base() = default;

    // 기반 포인터로 파생 실행 노드를 안전하게 소멸한다.
    virtual ~FAnimNode_Base() = default;

    // 실행 연결과 재생 상태가 복제되지 않도록 복사 생성을 금지한다.
    FAnimNode_Base(const FAnimNode_Base&) = delete;

    // 다른 실행 노드의 연결 및 상태를 덮어쓰는 복사 대입을 금지한다.
    FAnimNode_Base& operator=(const FAnimNode_Base&) = delete;

public:

    // 현재 노드의 갱신 기록을 지우고 모든 포즈 입력을 재귀적으로 초기화한다.
    virtual void Initialize(UAnimInstance& AnimInstance);

    // 프레임 시간과 변수에 따라 재생 상태를 갱신하는 파생 노드의 진입점이다.
    virtual void Update(const FAnimationUpdateContext& Context);
    // 현재 재생 상태로 본 포즈를 생성하는 파생 노드의 진입점이다.
    virtual void Evaluate(FPoseContext& Output);
    virtual FRootMotionDelta GetRootMotionDelta() const;

    // 에디터와 디버그 UI에 표시할 실행 노드 타입 이름을 반환한다.
    virtual const FString& GetNodeType() const = 0;
    // 초기화와 그래프 탐색에 사용할 포즈 입력 목록을 추가한다.
    virtual void GetPoseInputs(std::vector<FAnimNodePoseInput>& OutInputs);
    // 변수 출력에 연결할 수 있는 값 입력 목록을 추가한다.
    virtual void GetValueInputs(std::vector<FAnimNodeValueInput>& OutInputs);
    // 현재 재생 상태와 편집 가능한 속성을 디버그 목록에 추가한다.
    virtual void GetDebugValues(std::vector<FAnimNodeValue>& OutValues);

public:
    // 그래프 정의에서 받은 노드 이름이며 외부 플레이어 조회에도 사용한다.
    FString mName{};
};
