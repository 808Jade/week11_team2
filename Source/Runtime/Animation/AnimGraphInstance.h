#pragma once

#include "Animation/AnimGraph.h"
#include "Animation/AnimInstance.h"

#include <memory>
#include <vector>

struct FAnimNode_AssetPlayerBase;
struct FAnimValueNode_Base;
struct FAnimGraphNodeBuildContext;

// 같은 그래프 에셋을 사용하는 캐릭터도 변수 값을 서로 독립적으로 보관한다.
struct FAnimGraphRuntimeVariable {
public:

    // 그래프 정의에서 복사한 변수 조회 이름이다.
    FString mName{};

    // 이 실행 인스턴스만 사용하는 현재 Float 값이다.
    float mValue{};

    // 이 변수의 값을 외부 입력 또는 자동 이동 속도 중 어디서 갱신할지 지정한다.
    EAnimGraphVariableSource mSource{EAnimGraphVariableSource::Manual};
    // 현재 변수를 Float 또는 Bool로 해석할 타입 정보이다.
    EAnimGraphVariableType mType{};
    // 이 실행 인스턴스만 사용하는 현재 Bool 값이다.
    bool mBoolValue{};
};

#include "AnimGraphInstance.generated.h"

// 그래프 정의를 실행 노드로 구성하고, 캐릭터별 변수와 재생 상태를 소유한다.
UCLASS()
class UAnimGraphInstance final : public UAnimInstance {
    GENERATED_BODY()

public:
    // 그래프 경로와 실행 데이터가 비어 있는 인스턴스를 생성한다.
    UAnimGraphInstance();

    // 이 인스턴스가 소유하는 모든 포즈 및 값 실행 노드를 해제한다.
    ~UAnimGraphInstance() override;

public:
    // 실행 대상으로 지정한 그래프의 정규화된 에셋 키를 반환한다.
    const FString& GetAnimGraph() const;
    // 그래프 에셋을 로드할 수 있는지 확인하고 실행 대상 경로를 지정한다.
    bool SetAnimGraph(const FString& Path);

    // 이름과 타입이 일치하는 Float 변수에 유한한 값을 설정한다.
    bool SetFloatVariable(const FString& Name, float Value);

    // 이름에 맞는 Float 값 또는 찾지 못한 경우 0을 반환한다.
    float GetFloatVariable(const FString& Name) const override;
    // 이름과 타입이 일치하는 Bool 변수의 현재 값을 설정한다.
    bool SetBoolVariable(const FString& Name, bool Value);
    // 이름에 맞는 Bool 값 또는 찾지 못한 경우 false를 반환한다.
    bool GetBoolVariable(const FString& Name) const override;
    // 요청한 이름과 Float 또는 Bool 타입의 변수가 존재하는지 확인한다.
    bool HasGraphVariable(const FString& Name, bool IsBool) const override;
    // 변수의 값 공급 방식을 반환하며 찾지 못하면 Manual을 반환한다.
    EAnimGraphVariableSource GetVariableSource(const FString& Name) const;

    // 모든 중첩 실행 노드에서 이름이 유일한 에셋 플레이어를 찾는다.
    FAnimNode_AssetPlayerBase* FindAssetPlayer(const FString& Name) const;

    // 이 인스턴스의 현재 변수 주소를 디버그 UI의 편집 가능한 속성으로 노출한다.
    void GetDebugVariables(std::vector<FAnimNodeValue>& OutValues) override;

    // 유효한 원본 정의에서 루트 그래프 노드의 저장 좌표를 제공한다.
    bool GetNodeLayout(const FAnimNode_Base* Node, float& X, float& Y) const override;
    // 리비전이 일치할 때 실행 포즈 노드에 대응하는 원본 정의를 반환한다.
    const FAnimGraphNode* GetSourceNode(const FAnimNode_Base* Node) const;
    // 명시적 값 노드의 원본 Get 배치를 찾으며 암시적 Get에는 nullptr를 반환한다.
    const FAnimGraphVariableGetter* GetSourceGetter(const FAnimValueNode_Base* Node) const;
    // 포즈 실행 노드의 소유 배열 위치를 바탕으로 디버그 키를 생성한다.
    uint64 GetNodeKey(const FAnimNode_Base* Node) const;
    // 값 실행 노드의 소유 배열 위치를 바탕으로 디버그 키를 생성한다.
    uint64 GetValueNodeKey(const FAnimValueNode_Base* Node) const;
    // 현재 실행 데이터와 리비전이 일치하는 원본 그래프만 반환한다.
    const UAnimGraph* GetSourceGraph() const;
    // 실행 노드를 다시 구성할 때마다 증가하는 세대 번호를 반환한다.
    uint64 GetGraphGeneration() const;
    // 원본 그래프 주소와 포즈 노드 ID에 대응하는 현재 실행 노드를 찾는다.
    FAnimNode_Base* FindRuntimeNode(const UAnimGraph& Graph, int32 NodeId) const;

private:
    // 그래프 정의를 실행 노드로 변환하고 중첩 상태, 값 입력과 포즈 연결을 구성한다.
    FAnimNode_Base* BuildGraph(const UAnimGraph& Source, const FAnimGraphNodeBuildContext& BaseContext, uint32 Depth);

    // 이전 실행 데이터를 해제하고 변수 기본값 및 전체 실행 그래프를 재구성한다.
    void NativeInitializeAnimation() override;

    // PIE 에셋 교체를 반영하고 OwnerSpeed 변수에 수평 이동 속도를 기록한다.
    void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    // 실행 데이터를 생성한 공유 루트 그래프 에셋의 비소유 포인터이다.
    const UAnimGraph* mSourceGraph{};
    // 실행 그래프를 구성할 때 읽은 원본 에셋 리비전이다.
    uint64 mGraphRevision{};
    // 재구성 전후에 같은 주소가 재사용되어도 디버그 대상의 세대를 구분한다.
    uint64 mGraphGeneration{};
    // 씬과 컴포넌트에서 지정한 그래프의 정규화된 에셋 키이다.
    FString mAnimGraph{};

    // 상태 내부 그래프까지 포함한 모든 포즈 실행 노드의 소유 배열이다.
    std::vector<std::unique_ptr<FAnimNode_Base>> mNodes{};
    // 명시적 및 암시적 변수 Get 값을 제공하는 실행 노드의 소유 배열이다.
    std::vector<std::unique_ptr<FAnimValueNode_Base>> mValueNodes{};
    // 실행 노드와 원본 정의를 대응시켜 에디터 배치와 PIE 디버그 선택을 연결한다.
    std::vector<const FAnimGraphNode*> mNodeSources{};
    // 각 포즈 실행 노드의 원본 정의를 가진 그래프를 대응시킨 배열이다.
    std::vector<const UAnimGraph*> mNodeGraphs{};
    // 각 값 노드의 명시적 원본 배치를 가리키며 암시적 Get 노드는 nullptr이다.
    std::vector<const FAnimGraphVariableGetter*> mGetterSources{};

    // 공유 에셋과 분리해 이 인스턴스가 소유하는 현재 변수 값 목록이다.
    std::vector<FAnimGraphRuntimeVariable> mVariables{};
};
