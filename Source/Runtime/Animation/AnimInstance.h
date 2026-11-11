#pragma once

#include "Animation/BoneContainer.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimNotifyTypes.h"
#include "UObject/Object.h"

#include <vector>
#include <unordered_map>

#include "AnimInstance.generated.h"

class UAnimSequenceBase;
class USkeletalMeshComponent;
struct FAnimNode_Base;
struct FAnimNodeValue;
struct FPoseLink;
struct FAnimationUpdateContext;

// 평가 경로의 부모와 가중치를 기록한다. SampleIndex는 Blend Space 샘플 방문에만 사용한다.
struct FAnimNodeVisit {
public:
    // 이번 평가 경로에서 방문한 실행 노드이다.
    const FAnimNode_Base* mNode{};
    // 방문을 발생시킨 포즈 입력 링크이며 루트 및 샘플 방문에서는 비어 있을 수 있다.
    const FPoseLink* mLink{};
    // 부모 방문에 대한 로컬 가중치이며 디버그 패널에서 상위 기여도와 곱한다.
    float mWeight{};
    // 방문 배열에서 부모 평가의 인덱스이며 루트는 -1이다.
    int32 mParentIndex{-1};
    // Blend Space 내부 샘플 인덱스이며 일반 노드 방문은 -1이다.
    int32 mSampleIndex{-1};
};

// 스켈레탈 메시별 본 정보, 루트 노드 평가, 이동 속도와 디버그 방문 기록을 관리한다.
UCLASS()
class UAnimInstance : public UObject {
    GENERATED_BODY()

public:
    // 소유 컴포넌트가 이 인스턴스를 가리키고 있으면 해당 참조를 해제한다.
    ~UAnimInstance() override;

public:
    // 이동 속도와 프레임별 기록을 갱신하고 파생 갱신 뒤 루트 노드를 진행한다.
    void UpdateAnimation(float DeltaSeconds);
    // 공유된 실행 노드도 한 프레임에 한 번만 갱신한다.
    void UpdateNode(FAnimNode_Base& Node, const FAnimationUpdateContext& Context);
    // 재초기화한 노드를 같은 프레임에 다시 갱신할 수 있도록 기록에서 제거한다.
    void ResetNodeUpdate(FAnimNode_Base& Node);
    // 루트에서 본 로컬 포즈를 평가하고 유효한 평가를 수행했는지 반환한다.
    bool EvaluateAnimation(TArray<FTransform>& OutPose);
    // 현재 메시의 본 컨테이너 순서로 기준 포즈를 채운다.
    void GetReferencePose(TArray<FTransform>& OutPose) const;
    // 시퀀스의 지정 시각을 현재 메시의 본 정보에 맞춰 샘플링한다.
    void EvaluateSequence(const UAnimSequenceBase& Sequence, double Time, TArray<FTransform>& OutPose) const;

    // 최종 출력 포즈를 생성하는 실행 루트의 비소유 포인터를 반환한다.
    FAnimNode_Base* GetRootNode() const;
    FRootMotionDelta GetRootMotionDelta() const;
    void SetRootMotionEnabled(bool Enabled);
    bool IsRootMotionEnabled() const;
    FRootMotionDelta ExtractSequenceEvents(const UAnimSequenceBase& Sequence, double From, double To, float PlayRate, bool Wrapped, bool IncludeStart, const FAnimationUpdateContext& Context) const;

    // 마지막 포즈 평가에서 기록한 노드와 샘플 방문 경로를 반환한다.
    const std::vector<FAnimNodeVisit>& GetDebugVisits() const;
    // 현재 평가를 부모로 하는 방문을 추가하고 디버그 경로를 한 단계 내려간다.
    void BeginDebugVisit(const FAnimNode_Base& Node, const FPoseLink* Link, float Weight);
    // 현재 방문을 마치고 부모 평가 경로로 돌아간다.
    void EndDebugVisit();
    // 현재 Blend Space 방문 아래에 샘플의 인덱스와 로컬 가중치를 기록한다.
    void RecordDebugSample(const FAnimNode_Base& Node, int32 SampleIndex, float Weight);
    // 소유 메시의 월드 위치 변화로 추정한 이동 속도를 반환한다.
    const FVector& GetOwnerVelocity() const;
    // 파생 인스턴스가 디버그 패널에 노출할 변수 목록을 추가한다.
    virtual void GetDebugVariables(std::vector<FAnimNodeValue>& OutValues);
    // 규칙 및 값 노드가 조회할 Float 변수를 반환하며 기반 구현은 0을 반환한다.
    virtual float GetFloatVariable(const FString& Name) const;
    // 규칙 노드가 조회할 Bool 변수를 반환하며 기반 구현은 false를 반환한다.
    virtual bool GetBoolVariable(const FString& Name) const;
    // 이름과 요청 타입에 맞는 변수가 존재하는지 파생 인스턴스에서 확인한다.
    virtual bool HasGraphVariable(const FString& Name, bool IsBool) const;
    // 원본 에셋에 저장된 노드 좌표를 제공하며 기반 구현은 배치를 제공하지 않는다.
    virtual bool GetNodeLayout(const FAnimNode_Base* Node, float& X, float& Y) const;

protected:
    // 루트와 파생 인스턴스의 실행 데이터를 구성하는 초기화 확장점이다.
    virtual void NativeInitializeAnimation();
    // 루트 갱신 전에 파생 인스턴스의 변수와 실행 구성을 갱신하는 확장점이다.
    virtual void NativeUpdateAnimation(float DeltaSeconds);
    // Outer로 지정된 소유 스켈레탈 메시 컴포넌트를 반환한다.
    USkeletalMeshComponent* GetSkelMeshComponent() const;
    // 포즈 평가에 사용하는 메시 및 스켈레톤 본 정보를 반환한다.
    const FBoneContainer& GetRequiredBones() const;
    // 인스턴스가 평가할 루트를 지정하며 노드의 소유권은 이전하지 않는다.
    void SetRootNode(FAnimNode_Base* RootNode);
    void EndAllNotifyStates();

private:
    friend class USkeletalMeshComponent;
    // 메시와 본 정보를 검증하고 파생 데이터 및 루트 재생 상태를 초기화한다.
    bool InitializeAnimation(FString& OutError);
    void TriggerAnimNotifies(const FAnimNotifyQueue& Queue);
    TArray<FAnimNotifyDispatch> TakePendingNotifies();

private:
    // 현재 메시와 스켈레톤의 본 매핑 및 기준 포즈 정보이다.
    FBoneContainer mRequiredBones{};
    bool mRootMotionEnabled{};
    TArray<FAnimNotifyQueue::FEntry> mActiveNotifyStates{};
    TArray<FAnimNotifyDispatch> mPendingNotifies{};
    // 최종 포즈를 생성하는 실행 노드의 비소유 포인터이다.
    FAnimNode_Base* mRootNode{};
    // 다음 프레임의 속도 추정에 사용할 이전 월드 위치이다.
    FVector mLastOwnerLocation{};
    // 이전 위치와 현재 위치의 차이를 프레임 시간으로 나눈 이동 속도이다.
    FVector mOwnerVelocity{};
    // 첫 프레임에서 잘못된 이동 속도를 계산하지 않도록 이전 위치의 유효성을 표시한다.
    bool mHasLastOwnerLocation{};

    // 마지막 평가의 노드, 연결 및 Blend Space 샘플 방문 목록이다.
    std::vector<FAnimNodeVisit> mDebugVisits{};
    // 현재 진행 중인 디버그 방문 인덱스이며 방문 밖에서는 -1이다.
    int32 mCurrentDebugVisit{-1};
    // 포즈 그래프가 여러 경로에서 공유하는 노드의 중복 시간 진행을 막는다.
    std::unordered_map<FAnimNode_Base*, bool> mUpdatedNodes{};
};
