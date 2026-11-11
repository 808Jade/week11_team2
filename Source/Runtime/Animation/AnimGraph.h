#pragma once

#include "Animation/AnimStateMachineData.h"
#include "Asset/RenderAsset.h"
#include <unordered_map>

#include "AnimGraph.generated.h"

// 상수 문자열 또는 변수 바인딩을 저장하며, GetterId로 같은 변수의 여러 Get 노드를 구분한다.
struct FAnimGraphParameter {
public:

    // 일반 속성 이름 또는 변수 연결을 나타내는 .Binding 속성 키이다.
    FString mName{};

    // 직렬화 가능한 상수 문자열 또는 바인딩할 변수 이름이다.
    FString mValue{};
    // 연결한 명시적 Get 노드의 ID이며 지정하지 않은 경우 -1이다.
    int32 mGetterId{-1};
};

// 에셋에는 실행 객체 대신 노드 타입, 파라미터와 에디터 배치를 저장한다.
struct FAnimGraphNode {
public:

    // 이름에 맞는 저장 파라미터를 찾고 없으면 nullptr를 반환한다.
    const FString* FindParameter(const FString& Name) const;

    // 파라미터를 추가 또는 갱신하고 빈 값은 해당 파라미터의 제거로 처리한다.
    void SetParameter(const FString& Name, const FString& Value, int32 GetterId = -1);

public:

    // 포즈 연결과 에디터 선택에 사용하는 노드의 고유 ID이다.
    int32 mId{};

    // 레지스트리에서 실행 노드 생성 함수와 핀 구성을 찾을 타입 키이다.
    FString mType{};

    // 노드 표시와 플레이어 이름 조회에 사용하는 사용자 지정 이름이다.
    FString mName{};

    // 포즈 그래프 편집기에 저장할 가로 배치 좌표이다.
    float mX{};

    // 포즈 그래프 편집기에 저장할 세로 배치 좌표이다.
    float mY{};

    // 상수 속성과 변수 입력 바인딩을 저장하는 파라미터 목록이다.
    std::vector<FAnimGraphParameter> mParameters{};
};

// 출력 노드 또는 다른 노드의 입력 핀을 안정적인 노드 ID로 연결한다.
struct FAnimGraphLink {
public:

    // 포즈를 제공하는 출력 노드의 고유 ID이다.
    int32 mFromNode{};

    // 포즈를 받는 노드의 ID이며 최종 출력은 OutputNodeId를 사용한다.
    int32 mToNode{};

    // 대상 노드의 포즈 입력 목록에서 연결할 핀 인덱스이다.
    int32 mToPin{};
};

enum class EAnimGraphVariableSource : uint8 {

    Manual,

    OwnerSpeed
};

enum class EAnimGraphVariableType : uint8 {
    Float,
    Bool
};

// 변수의 타입과 초기값을 정의한다. Manual은 외부 입력, OwnerSpeed는 자동 속도 갱신에 사용한다.
struct FAnimGraphVariable {
public:

    // 값 입력과 전환 규칙 및 Lua가 공통으로 사용하는 변수 이름이다.
    FString mName{};

    // 새 실행 인스턴스의 Float 변수에 복사할 기본값이다.
    float mDefault{};

    // 외부에서 설정하는 Manual 변수인지 자동 이동 속도 변수인지 지정한다.
    EAnimGraphVariableSource mSource{EAnimGraphVariableSource::Manual};
    // 값을 Float와 Bool 중 어떤 타입으로 해석할지 지정한다.
    EAnimGraphVariableType mType{EAnimGraphVariableType::Float};
    // 새 실행 인스턴스의 Bool 변수에 복사할 기본값이다.
    bool mBoolDefault{};
};

struct FAnimGraphVariableGetter {
public:
    // 같은 변수의 여러 Get 노드를 개별적으로 구분하는 고유 ID이다.
    int32 mId{};
    // 현재 값을 조회할 그래프 변수 이름이다.
    FString mVariable{};
    // Get 노드의 저장된 가로 배치 좌표이다.
    float mX{};
    // Get 노드의 저장된 세로 배치 좌표이다.
    float mY{};
};

// 여러 인스턴스가 공유하는 그래프 구성이다. 실제 재생 상태와 실행 노드는 UAnimGraphInstance가 가진다.
UCLASS()
class UAnimGraph final : public URenderAsset {
    GENERATED_BODY()

public:

    // 일반 노드 ID와 구분하는 최종 출력 노드의 예약 ID이다.
    static constexpr int32 OutputNodeId{-1};

    // 포즈 및 규칙 그래프의 노드 수를 제한하는 상한이다.
    static constexpr uint32 MaxNodes{256};

public:
    // 비어 있는 그래프 정의와 기본 출력 배치를 생성한다.
    UAnimGraph() = default;

    // 그래프 구성과 소유한 상태 내부 그래프를 해제한다.
    ~UAnimGraph() override = default;

public:
    // UObject 타입 정보를 유지하면서 shared_ptr로 수명을 관리할 상태 내부 그래프를 생성한다.
    static std::shared_ptr<UAnimGraph> CreateStateGraph();

    // 저장된 포즈 노드 정의 목록을 반환한다.
    const std::vector<FAnimGraphNode>& GetNodes() const;
    // 포즈 출력과 입력 핀 사이의 연결 목록을 반환한다.
    const std::vector<FAnimGraphLink>& GetLinks() const;
    // 실행 인스턴스에 복사할 변수 정의와 기본값 목록을 반환한다.
    const std::vector<FAnimGraphVariable>& GetVariables() const;

    // 고유 ID에 해당하는 포즈 노드 정의를 찾으며 없으면 nullptr를 반환한다.
    FAnimGraphNode* FindNode(int32 Id);
    // 고유 ID에 해당하는 포즈 노드 정의를 찾으며 없으면 nullptr를 반환한다.
    const FAnimGraphNode* FindNode(int32 Id) const;

    // 타입, 이름과 위치로 노드를 추가하고 상태 머신 타입이면 별도 정의도 준비한다.
    int32 AddNode(const FString& Type, const FString& Name, float X, float Y);

    // 노드와 관련 포즈 연결 및 해당 상태 머신 정의를 제거한다.
    bool RemoveNode(int32 Id);

    // 순환을 검사하고 대상 입력의 기존 포즈 연결을 새 연결로 교체한다.
    bool Connect(int32 FromNode, int32 ToNode, int32 ToPin);

    // 대상 노드와 핀으로 지정한 포즈 연결을 제거한다.
    bool Disconnect(int32 ToNode, int32 ToPin);

    // 새 포즈 연결이 그래프에 순환 경로를 만들지 검사한다.
    bool WouldCreateCycle(int32 FromNode, int32 ToNode) const;

    // 대상 입력 핀에 연결된 출발 노드의 ID를 찾는다.
    int32 FindInput(int32 ToNode, int32 ToPin) const;

    // 포즈 노드 또는 최종 출력 노드의 에디터 좌표를 변경한다.
    void SetNodePosition(int32 Id, float X, float Y);

    // 최종 출력 노드의 저장된 가로 좌표를 반환한다.
    float GetOutputX() const;

    // 최종 출력 노드의 저장된 세로 좌표를 반환한다.
    float GetOutputY() const;

    // 이름과 기본값을 검증해 변수를 추가하고 내부 그래프에도 동기화한다.
    bool AddVariable(const FAnimGraphVariable& Variable);

    // 변수 설정을 바꾸고 이름 변경을 Get 노드, 바인딩과 내부 그래프에 전파한다.
    bool SetVariable(int32 Index, const FAnimGraphVariable& Variable);

    // 변수와 관련 Get 노드 및 바인딩을 제거하고 내부 그래프에도 전파한다.
    bool RemoveVariable(int32 Index);

    // 캔버스에 명시적으로 배치한 변수 Get 노드 목록을 반환한다.
    const std::vector<FAnimGraphVariableGetter>& GetVariableGetters() const;
    // Get 노드의 고유 ID로 해당 배치 정보를 찾는다.
    const FAnimGraphVariableGetter* FindVariableGetter(int32 Id) const;
    // 노드 파라미터의 바인딩에 대응하는 명시적 변수 Get 노드를 찾는다.
    const FAnimGraphVariableGetter* FindBoundGetter(const FAnimGraphNode& Node, const FString& Parameter) const;
    // 기존 Float 변수를 조회할 독립적인 Get 노드와 배치 좌표를 추가한다.
    int32 AddVariableGetter(const FString& Variable, float X, float Y);
    // 지정 Get 노드의 에디터 좌표를 변경한다.
    bool SetVariableGetterPosition(int32 Id, float X, float Y);
    // Get 노드와 그 노드에 대응하는 파라미터 바인딩을 제거한다.
    bool RemoveVariableGetter(int32 Id);

    // 중첩 상태 머신까지 독립적으로 복사하고 원본 참조 검사용 리비전을 갱신한다.
    void CopyFrom(const UAnimGraph& Source);

    // 최근 CopyFrom에 따른 그래프 구성 교체 리비전을 반환한다.
    uint64 GetRevision() const;

    // 상태 머신 포즈 노드의 ID에 대응하는 별도 상태 및 전환 정의를 찾는다.
    FAnimStateMachineData* FindStateMachine(int32 NodeId);
    // 상태 머신 포즈 노드의 ID에 대응하는 별도 상태 및 전환 정의를 찾는다.
    const FAnimStateMachineData* FindStateMachine(int32 NodeId) const;
    // 부모 변수 목록을 반영하고 유효하지 않은 Get 노드와 바인딩을 정리한다.
    void SynchronizeVariables(const std::vector<FAnimGraphVariable>& Variables);

    // 변수, 포즈 노드, 연결과 중첩 상태 머신 구성을 저장하거나 불러온다.
    void Serialize(FAnimAssetArchive& Archive);

private:
    // 바인딩의 명시적 Get ID를 우선 해석하고 필요하면 변수 이름으로 찾는다.
    const FAnimGraphVariableGetter* ResolveGetter(const FAnimGraphParameter& Binding) const;
    // Get 노드의 ID, 변수 이름과 에디터 배치를 저장하거나 읽는다.
    void SerializeVariableGetters(FAnimAssetArchive& Archive);
    // 포즈 노드 ID와 상태 머신 구성의 대응 관계를 저장하거나 읽는다.
    void SerializeStateMachines(FAnimAssetArchive& Archive);
    // 변수 변경을 상태 내부 그래프와 전환 규칙에 재귀적으로 전파한다.
    void UpdateChildVariables(const FString& OldName, const FString& NewName);

private:
    // CopyFrom으로 구성이 교체되면 실행 인스턴스와 에디터의 원본 참조를 무효화한다.
    uint64 mRevision{};
    // 상태 머신 포즈 노드 ID를 키로 보관하는 상태 및 전환 구성이다.
    std::unordered_map<int32, FAnimStateMachineData> mStateMachines{};

    // 실행 객체로 변환할 포즈 노드 정의의 원본 목록이다.
    std::vector<FAnimGraphNode> mNodes{};

    // 고유 노드 ID와 입력 핀 인덱스로 저장한 포즈 연결 목록이다.
    std::vector<FAnimGraphLink> mLinks{};

    // 인스턴스별 변수의 이름, 타입, 기본값과 공급 방식 정의이다.
    std::vector<FAnimGraphVariable> mVariables{};
    // 같은 변수를 여러 위치에서 조회할 수 있는 명시적 Get 노드 배치 목록이다.
    std::vector<FAnimGraphVariableGetter> mVariableGetters{};
    // Get 노드와 포즈 노드를 삭제해도 남은 노드의 ID와 연결은 유지한다.
    int32 mNextGetterId{};

    // 다음에 생성할 포즈 노드에 발급할 ID이다.
    int32 mNextNodeId{};

    // 최종 출력 노드의 에디터 가로 좌표이다.
    float mOutputX{400.0f};

    // 최종 출력 노드의 에디터 세로 좌표이다.
    float mOutputY{0.0f};
};
