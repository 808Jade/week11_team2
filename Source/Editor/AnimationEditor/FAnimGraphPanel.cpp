#include "EnginePCH.h"
#include "FAnimGraphPanel.h"
#include "GameFramework/Actor.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequenceBase.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Animation/AnimNode_Base.h"
#include "Animation/AnimNode_BlendSpacePlayer.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimValueNode.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "FAnimGraphNodeBuilder.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <unordered_map>

namespace ed = ax::NodeEditor;

namespace {

    constexpr uint64 OutputKey{1};
    constexpr uint64 OutputPinBits{0xFF};
    constexpr uint64 FloatInputBit{uint64{1} << 60};
    constexpr uint64 GetterBit{uint64{1} << 61};
    constexpr uint64 SavedGetterBit{uint64{1} << 59};

    constexpr float ColumnSpacing{340.0f};
    constexpr float RowSpacing{260.0f};
    constexpr float MinNodeWidth{230.0f};

    constexpr ImVec4 ActiveColor{0.96f, 0.77f, 0.40f, 1.0f};
    constexpr ImVec4 InactiveColor{0.33f, 0.40f, 0.51f, 1.0f};
    constexpr ImVec4 BoolColor{0.82f, 0.24f, 0.28f, 1.0f};
    constexpr ImVec4 StateColor{0.45f, 0.72f, 1.0f, 1.0f};

    std::uintptr_t StateNodeKey(int32 Id) {
        return Id == FAnimStateMachineData::AnyStateId ? 16 : (static_cast<std::uintptr_t>(Id) + 2) * 16;
    }

    bool IsVisited(const UAnimInstance& Instance, const FAnimNode_Base* Node) {
        const auto& Visits{Instance.GetDebugVisits()};

        return std::any_of(Visits.begin(), Visits.end(), [Node](const FAnimNodeVisit& Visit) {
            return Visit.mNode == Node && Visit.mSampleIndex < 0;
        });
    }

    FString RuleValueText(const FAnimRuleDebugValue* Value) {
        if (Value != nullptr) {
            if (const auto* Bool{std::get_if<bool>(&Value->mValue)}) {
                return *Bool ? "true" : "false";
            }

            if (const auto* Float{std::get_if<float>(&Value->mValue)}) {
                return std::format("{:.3f}", *Float);
            }
        }

        return "invalid";
    }

    void RejectGraphEdits() {
        // 실행 상태를 보는 캔버스이므로 연결이나 노드의 생성 및 삭제 요청은 반영하지 않는다.
        if (ed::BeginCreate()) {
            ed::PinId Start{};
            ed::PinId End{};

            if (ed::QueryNewLink(&Start, &End)) {
                ed::RejectNewItem(InactiveColor);
            }
        }

        ed::EndCreate();

        if (ed::BeginDelete()) {
            ed::LinkId Link{};
            ed::NodeId Node{};

            while (ed::QueryDeletedLink(&Link)) {
                ed::RejectDeletedItem();
            }

            while (ed::QueryDeletedNode(&Node)) {
                ed::RejectDeletedItem();
            }
        }

        ed::EndDelete();
    }

    struct FFloatLink {
    public:
        uint64 mInput{};
        uint64 mTarget{};
        uint64 mGetterKey{};
        const FAnimGraphVariableGetter* mGetter{};
        const FAnimValueNode_Base* mValueNode{};
        FString mParameter{};
        float mValue{};
    };

    struct FAnimNodeValueDisplay {
    public:
        FString mName{};
        FString mFloatLabel{};
        const FAnimValueNode_Base* mValueNode{};
        const FAnimGraphVariableGetter* mGetter{};
    };

    uint64 NodeKey(const UAnimInstance& Instance, const FAnimNode_Base* Node) {
        if (const auto* Graph{Cast<UAnimGraphInstance>(&Instance)}) {
            if (const uint64 Key{Graph->GetNodeKey(Node)}; Key != 0) {
                return (Key << 8) | (uint64{1} << 56);
            }
        }

        const auto Base{reinterpret_cast<std::uintptr_t>(&Instance)};
        const auto Address{reinterpret_cast<std::uintptr_t>(Node)};

        if (Address >= Base && Address - Base < (std::uintptr_t{1} << 24)) {
            const uint64 ClassBits{static_cast<uint64>(std::hash<FString>{}(Instance.GetClass()->Name) & 0xFFFF) << 34};

            return ((static_cast<uint64>(Address - Base) + 1) << 8) | ClassBits | (uint64{1} << 57);
        }

        return (static_cast<uint64>(Address) << 8) | (uint64{1} << 62);
    }

    uint64 InputPinKey(uint64 Node, std::size_t Index) {
        return Node | static_cast<uint64>(Index + 1);
    }

    uint64 OutputPinKey(uint64 Node) {
        return Node | OutputPinBits;
    }

    ImVec4 Mix(const ImVec4& From, const ImVec4& To, float Alpha) {
        return {From.x + (To.x - From.x) * Alpha, From.y + (To.y - From.y) * Alpha, From.z + (To.z - From.z) * Alpha, From.w + (To.w - From.w) * Alpha};
    }

    void CollectNodes(FAnimNode_Base* Node, std::vector<FAnimNode_Base*>& Nodes) {
        if (Node == nullptr || std::find(Nodes.begin(), Nodes.end(), Node) != Nodes.end()) {
            return;
        }

        Nodes.push_back(Node);

        if (dynamic_cast<FAnimNode_StateMachine*>(Node) != nullptr) {
            return;
        }

        std::vector<FAnimNodePoseInput> Inputs{};

        Node->GetPoseInputs(Inputs);

        for (const FAnimNodePoseInput& Input : Inputs) {
            CollectNodes(Input.mLink->mLinkedNode, Nodes);
        }
    }

    void AssignDepth(FAnimNode_Base* Node, int32 Depth, std::unordered_map<FAnimNode_Base*, int32>& Depths, int32 Guard) {

        if (Node == nullptr || Guard < 0) {
            return;
        }

        auto& Current{Depths[Node]};

        Current = std::max(Current, Depth);

        if (dynamic_cast<FAnimNode_StateMachine*>(Node) != nullptr) {
            return;
        }

        std::vector<FAnimNodePoseInput> Inputs{};

        Node->GetPoseInputs(Inputs);

        for (const FAnimNodePoseInput& Input : Inputs) {
            AssignDepth(Input.mLink->mLinkedNode, Depth + 1, Depths, Guard - 1);
        }
    }
}

FAnimGraphPanel::FAnimGraphPanel() = default;

FAnimGraphPanel::~FAnimGraphPanel() {
    ClearGraphContexts();
}

void FAnimGraphPanel::Draw(USkeletalMeshComponent* Component, bool Playing, bool& Open) {
    const ImVec2 WorkPosition{ImGui::GetMainViewport()->WorkPos};
    ImGui::SetNextWindowPos({WorkPosition.x + 70.0f, WorkPosition.y + 150.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({920.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Anim Graph Debug", &Open)) {
        DrawContent(Component, Playing);
    }
    ImGui::End();
}

void FAnimGraphPanel::ClearGraphContexts() {
    for (const auto& [Key, Canvas] : mGraphContexts) {
        ed::DestroyEditor(Canvas.mEditor);
    }

    mGraphContexts.clear();
    mNavigation.clear();
    mCanvas = nullptr;
}

void FAnimGraphPanel::ActivateView(const FGraphView& View) {
    mView = View;
    auto [Found, Created]{mGraphContexts.try_emplace(View.mKey)};
    mCanvas = &Found->second;

    if (Created) {
        ed::Config Config{};
        Config.SettingsFile = nullptr;
        mCanvas->mEditor = ed::CreateEditor(&Config);
    }

    mLayoutRequested = false;
}

void FAnimGraphPanel::SynchronizeView(UAnimInstance& Instance) {
    // 재생 노드가 재생성되면 중첩 뷰의 포인터도 무효해지므로 루트부터 다시 연결한다.
    auto* Graph{Cast<UAnimGraphInstance>(&Instance)};
    const UAnimGraph* Source{Graph != nullptr ? Graph->GetSourceGraph() : nullptr};
    const uint64 Generation{Graph != nullptr ? Graph->GetGraphGeneration() : 0};

    if (mCanvas != nullptr && mInstanceUUID == Instance.GetUUID() && mGraphGeneration == Generation && mSourceGraph == Source) {
        return;
    }

    ClearGraphContexts();
    mGraphInstance = Graph;
    mSourceGraph = Source;
    mInstanceUUID = Instance.GetUUID();
    mGraphGeneration = Generation;
    FGraphView View{};
    View.mGraph = Source;
    View.mRoot = Instance.GetRootNode();
    ActivateView(View);
}

void FAnimGraphPanel::Navigate(FGraphView View) {
    View.mKey = std::format("{}/{}:{}:{}", mView.mKey, static_cast<int>(View.mMode), View.mMachineId, View.mTransition);
    mNavigation.push_back(mView);
    ActivateView(View);
}

bool FAnimGraphPanel::OpenStateMachine(int32 NodeId) {
    if (mGraphInstance == nullptr || mView.mGraph == nullptr || mView.mMode != EGraphView::Pose || mView.mGraph->FindStateMachine(NodeId) == nullptr) {
        return false;
    }

    auto* Machine{dynamic_cast<FAnimNode_StateMachine*>(mGraphInstance->FindRuntimeNode(*mView.mGraph, NodeId))};

    if (Machine == nullptr) {
        return false;
    }

    FGraphView View{};
    View.mGraph = mView.mGraph;
    View.mMachine = Machine;
    View.mMachineId = NodeId;
    View.mMode = EGraphView::Machine;
    View.mLabel = Machine->mName.empty() ? "State Machine" : Machine->mName;
    Navigate(View);

    return true;
}

bool FAnimGraphPanel::OpenState(int32 StateId) {
    const auto* Machine{mView.mGraph != nullptr && mView.mMode == EGraphView::Machine ? mView.mGraph->FindStateMachine(mView.mMachineId) : nullptr};
    const auto* State{Machine != nullptr ? Machine->FindState(StateId) : nullptr};
    const auto* Runtime{mView.mMachine != nullptr ? mView.mMachine->FindState(StateId) : nullptr};

    if (State == nullptr || State->mGraph == nullptr || Runtime == nullptr) {
        return false;
    }

    FGraphView View{};
    View.mGraph = State->mGraph.get();
    View.mRoot = Runtime->mPose.mLinkedNode;
    View.mMode = EGraphView::Pose;
    View.mMachineId = StateId;
    View.mLabel = State->mName;
    Navigate(View);

    return true;
}

bool FAnimGraphPanel::OpenTransitionRule(int32 TransitionId) {
    const auto* Machine{mView.mGraph != nullptr && mView.mMode == EGraphView::Machine ? mView.mGraph->FindStateMachine(mView.mMachineId) : nullptr};
    const auto* Transition{Machine != nullptr ? Machine->FindTransition(TransitionId) : nullptr};

    if (Transition == nullptr) {
        return false;
    }

    FGraphView View{mView};
    View.mMode = EGraphView::Rule;
    View.mTransition = TransitionId;
    View.mLabel = std::format("{} -> {}", Transition->mFromState == FAnimStateMachineData::AnyStateId ? FString{"Any State"} : Machine->FindState(Transition->mFromState)->mName, Machine->FindState(Transition->mToState)->mName);
    Navigate(View);

    return true;
}

bool FAnimGraphPanel::NavigateTo(std::size_t Depth) {
    if (Depth >= mNavigation.size()) {
        return false;
    }

    const FGraphView View{mNavigation[Depth]};
    mNavigation.resize(Depth);
    ActivateView(View);

    return true;
}

void FAnimGraphPanel::DrawNavigation() {
    int32 Destination{-1};
    const float Height{ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + 4.0f};

    if (ImGui::BeginChild("DebugGraphNavigation", {0.0f, Height}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (std::size_t Index{}; Index <= mNavigation.size(); ++Index) {
            const bool Current{Index == mNavigation.size()};
            const auto& View{Current ? mView : mNavigation[Index]};

            if (Index > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(Index));
            ImGui::BeginDisabled(Current);

            if (ImGui::Button(View.mLabel.c_str())) {
                Destination = static_cast<int32>(Index);
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    if (Destination >= 0) {
        NavigateTo(static_cast<std::size_t>(Destination));
    }
}

void FAnimGraphPanel::CollectViewNodes(std::vector<FAnimNode_Base*>& Nodes) const {
    CollectNodes(mView.mRoot, Nodes);

    if (mGraphInstance == nullptr || mView.mGraph == nullptr) {
        return;
    }

    for (const auto& Source : mView.mGraph->GetNodes()) {
        auto* Node{mGraphInstance->FindRuntimeNode(*mView.mGraph, Source.mId)};

        if (Node != nullptr && std::find(Nodes.begin(), Nodes.end(), Node) == Nodes.end()) {
            Nodes.push_back(Node);
        }
    }
}

void FAnimGraphPanel::DrawContent(USkeletalMeshComponent* Component, bool Playing) {
    UAnimInstance* Instance{Component != nullptr ? Component->GetAnimInstance() : nullptr};

    if (Component == nullptr) {
        ImGui::TextDisabled("Select an actor with a Skeletal Mesh Component.");

        return;
    }

    if (Instance == nullptr) {
        ImGui::TextDisabled("%s has no animation instance.", Component->GetName().c_str());
        ImGui::TextWrapped("Assign an Anim Graph asset in Details.");

        return;
    }

    SynchronizeView(*Instance);

    ImGui::Text("%s", Component->GetOwner() != nullptr ? Component->GetOwner()->GetName().c_str() : Component->GetName().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s | %s", Instance->GetClass()->Name.c_str(), Playing ? "Playing" : "Editor preview");
    ImGui::SameLine();

    if (ImGui::SmallButton("Rearrange")) {
        mLayoutRequested = true;
    }

    if (ImGui::BeginTable("AnimGraphLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Variables", ImGuiTableColumnFlags_WidthFixed, mVariablesWidth);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        if (ImGui::BeginChild("AnimVariables")) {
            DrawVariables(*Instance, Playing);
        }

        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        DrawNavigation();

        if (mView.mMode == EGraphView::Machine) {
            DrawStateMachine(*Instance, Playing);
        } else if (mView.mMode == EGraphView::Rule) {
            DrawTransitionRule(*Instance, Playing);
        } else {
            DrawGraph(*Instance, Playing);
        }
        ImGui::EndTable();
    }
}

void FAnimGraphPanel::DrawVariables(UAnimInstance& Instance, bool Playing) {
    ImGui::SeparatorText("Variables");

    bool HasVariables{};

    const auto* Graph{Cast<UAnimGraphInstance>(&Instance)};
    std::vector<FAnimNodeValue> RuntimeVariables{};

    Instance.GetDebugVariables(RuntimeVariables);

    if (!RuntimeVariables.empty()) {
        ImGui::SeparatorText("Graph Variables");
    }

    for (const FAnimNodeValue& Variable : RuntimeVariables) {
        HasVariables = true;

        ImGui::PushID(Variable.mName.c_str());

        if (const FAnimNodeFloatValue* Float{std::get_if<FAnimNodeFloatValue>(&Variable.mValue)}; Float != nullptr && Float->mValue != nullptr) {
            const bool OwnerSpeed{Graph != nullptr && Graph->GetVariableSource(Variable.mName) == EAnimGraphVariableSource::OwnerSpeed};

            // 매 프레임 자동 갱신되는 속도는 표시만 하고 Manual 변수는 PIE에서 직접 조절한다.
            if (Playing && !OwnerSpeed) {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##Value", Float->mValue, 0.01f, 0.0f, 0.0f, (Variable.mName + ": %.3f").c_str());
            } else {
                ImGui::Text("%s: %.3f", Variable.mName.c_str(), *Float->mValue);

                if (OwnerSpeed) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("<- Owner Speed");
                }
            }
        }

        if (bool* const* Bool{std::get_if<bool*>(&Variable.mValue)}; Bool != nullptr && *Bool != nullptr) {
            ImGui::BeginDisabled(!Playing);
            ImGui::Checkbox(Variable.mName.c_str(), *Bool);
            ImGui::EndDisabled();
        }

        ImGui::PopID();
    }

    if (!HasVariables) {
        ImGui::TextDisabled("No graph variables.");
    }

    if (Playing) {
        const FVector& Velocity{Instance.GetOwnerVelocity()};

        ImGui::SeparatorText("Owner");
        ImGui::Text("Velocity: %.2f, %.2f, %.2f", Velocity.X, Velocity.Y, Velocity.Z);
    } else {
        ImGui::Spacing();
        ImGui::TextWrapped("Edit graph defaults in the Anim Graph editor.");
    }
}

bool FAnimGraphPanel::PlaceNodes(const UAnimInstance& Instance, FAnimNode_Base* Root, bool All) {
    // 원본 좌표를 우선 사용하며 좌표가 없는 노드는 루트와의 깊이에 따라 자동 배치한다.
    std::vector<FAnimNode_Base*> Nodes{};

    CollectViewNodes(Nodes);

    if (All) {
        mCanvas->mPlacedNodes.clear();
    }

    const bool Missing{!mCanvas->mPlacedNodes.contains(OutputKey) || std::any_of(Nodes.begin(), Nodes.end(), [this, &Instance](const FAnimNode_Base* Node) {
        return !mCanvas->mPlacedNodes.contains(NodeKey(Instance, Node));
    })};

    if (!Missing) {
        return false;
    }

    std::unordered_map<FAnimNode_Base*, int32> Depths{};
    std::unordered_map<int32, std::vector<FAnimNode_Base*>> Columns{};

    AssignDepth(Root, 1, Depths, static_cast<int32>(Nodes.size()));

    for (FAnimNode_Base* Node : Nodes) {
        Columns[Depths[Node]].push_back(Node);
    }

    const auto Place{[this](uint64 Key, float X, float Y) {
        ed::SetNodePosition(ed::NodeId{static_cast<std::uintptr_t>(Key)}, {X, Y});
        mCanvas->mPlacedNodes.insert(Key);
    }};
    float X{};
    float Y{};

    if (!mCanvas->mPlacedNodes.contains(OutputKey)) {
        if (mView.mGraph != nullptr) {
            Place(OutputKey, mView.mGraph->GetOutputX(), mView.mGraph->GetOutputY());
        } else if (Instance.GetNodeLayout(nullptr, X, Y)) {
            Place(OutputKey, X, Y);
        } else {
            Place(OutputKey, 0.0f, 0.0f);
        }
    }

    for (const auto& [Depth, Column] : Columns) {
        const float Top{-0.5f * static_cast<float>(Column.size() - 1) * RowSpacing};

        for (std::size_t Row{}; Row < Column.size(); ++Row) {
            const uint64 Key{NodeKey(Instance, Column[Row])};

            if (mCanvas->mPlacedNodes.contains(Key)) {
                continue;
            }

            if (const auto* Source{mGraphInstance != nullptr ? mGraphInstance->GetSourceNode(Column[Row]) : nullptr}) {
                Place(Key, Source->mX, Source->mY);
            } else if (Instance.GetNodeLayout(Column[Row], X, Y)) {
                Place(Key, X, Y);
            } else {
                Place(Key, -static_cast<float>(Depth) * ColumnSpacing, Top + static_cast<float>(Row) * RowSpacing);
            }
        }
    }

    return true;
}

void FAnimGraphPanel::DrawGraph(UAnimInstance& Instance, bool Playing) {
    FAnimNode_Base* Root{mView.mRoot};
    const auto* Graph{Cast<UAnimGraphInstance>(&Instance)};
    const uint64 GraphKey{Graph != nullptr ? (static_cast<uint64>(std::hash<FString>{}(Graph->GetAnimGraph())) & 0xFFFFF) << 39 : 0};
    std::vector<FFloatLink> FloatLinks{};
    std::unordered_map<const FAnimValueNode_Base*, uint64> ValueNodeKeys{};

    std::unordered_map<const FAnimNode_Base*, float> NodeWeights{};
    std::unordered_map<const FPoseLink*, float> LinkWeights{};
    std::unordered_map<const FAnimNode_Base*, std::vector<FBlendSampleWeight>> SampleWeights{};

    if (Playing) {
        const auto& Visits{Instance.GetDebugVisits()};
        std::vector<float> Weights{};
        std::vector<float> SampleTotals{};

        Weights.resize(Visits.size());
        SampleTotals.resize(Visits.size());

        for (const FAnimNodeVisit& Visit : Visits) {
            if (Visit.mSampleIndex >= 0) {
                SampleTotals[static_cast<std::size_t>(Visit.mParentIndex)] += Visit.mWeight;
            }
        }

        // 부모 기여도에 로컬 가중치를 곱해 최종 포즈에 대한 노드와 연결의 기여도를 구한다.
        for (std::size_t Index{}; Index < Visits.size(); ++Index) {
            const FAnimNodeVisit& Visit{Visits[Index]};
            float Weight{Visit.mWeight};

            if (Visit.mParentIndex >= 0) {
                const std::size_t Parent{static_cast<std::size_t>(Visit.mParentIndex)};
                Weight *= Weights[Parent];

                if (Visit.mSampleIndex >= 0) {
                    Weight /= SampleTotals[Parent];
                }
            }

            Weights[Index] = Weight;

            if (Visit.mSampleIndex >= 0) {
                auto& Samples{SampleWeights[Visit.mNode]};
                const auto Existing{std::find_if(Samples.begin(), Samples.end(), [&Visit](const FBlendSampleWeight& Sample) {
                    return Sample.mSampleIndex == Visit.mSampleIndex;
                })};

                if (Existing != Samples.end()) {
                    Existing->mWeight += Weight;
                } else {
                    Samples.push_back({Visit.mSampleIndex, Weight});
                }

                continue;
            }

            NodeWeights[Visit.mNode] += Weight;

            if (Visit.mLink != nullptr) {
                LinkWeights[Visit.mLink] += Weight;
            }
        }
    }

    const auto WeightOf{[](const auto& Weights, const auto* Item) {
        const auto Weight{Weights.find(Item)};
        return Weight != Weights.end() ? std::clamp(Weight->second, 0.0f, 1.0f) : 0.0f;
    }};
    const auto LinkColor{[](float Weight) {
        return Weight > 0.0f ? Mix(InactiveColor, ActiveColor, 0.35f + 0.65f * Weight) : InactiveColor;
    }};

    ImGui::TextDisabled("Double-click a State Machine to inspect its states");
    ed::SetCurrentEditor(mCanvas->mEditor);
    FAnimGraphNodeBuilder::ApplyStyle();
    ed::Begin("AnimGraphEditor", ImGui::GetContentRegionAvail());

    bool Placed{PlaceNodes(Instance, Root, mLayoutRequested)};
    mLayoutRequested = false;

    std::vector<FAnimNode_Base*> Nodes{};

    CollectViewNodes(Nodes);

    FAnimGraphNodeBuilder OutputBuilder{};
    const ImGuiStyle& Style{ImGui::GetStyle()};
    const ImVec4 FloatColor{FAnimGraphNodeBuilder::FloatColor()};

    ed::PushStyleColor(ed::StyleColor_NodeBorder, Root != nullptr && WeightOf(NodeWeights, Root) > 0.0f ? ActiveColor : InactiveColor);
    OutputBuilder.FitHeader("Output Pose", "Output Pose");
    OutputBuilder.FitPins("Result");
    OutputBuilder.Begin(ed::NodeId{static_cast<std::uintptr_t>(OutputKey)}, 160.0f);
    OutputBuilder.Header("Output Pose", "Output Pose");
    OutputBuilder.InputPin(ed::PinId{static_cast<std::uintptr_t>(OutputKey)}, "Result", Root != nullptr, FAnimGraphNodeBuilder::PoseColor());
    OutputBuilder.End();
    ed::PopStyleColor();

    for (FAnimNode_Base* Node : Nodes) {
        const uint64 Key{NodeKey(Instance, Node)};
        const float Weight{WeightOf(NodeWeights, Node)};
        const auto* BlendSpacePlayer{dynamic_cast<const FAnimNode_BlendSpacePlayer*>(Node)};
        const char* Type{Node->GetNodeType().c_str()};
        const char* Title{Node->mName.empty() ? Type : Node->mName.c_str()};
        const FString WeightText{Playing ? std::format("{:.0f}%", Weight * 100.0f) : FString{}};
        std::vector<FAnimNodePoseInput> Inputs{};
        std::vector<FAnimNodeValueInput> ValueInputs{};
        std::vector<FAnimNodeValue> Values{};
        std::vector<FString> InputLabels{};
        std::vector<FAnimNodeValueDisplay> Displays{};
        FAnimGraphNodeBuilder Builder{};

        Node->GetPoseInputs(Inputs);
        if (dynamic_cast<FAnimNode_StateMachine*>(Node) != nullptr) {
            Inputs.clear();
        }
        Node->GetValueInputs(ValueInputs);
        Node->GetDebugValues(Values);

        const auto RecordedSamples{SampleWeights.find(Node)};

        if (BlendSpacePlayer != nullptr && BlendSpacePlayer->mBlendSpace != nullptr && RecordedSamples != SampleWeights.end()) {
            const auto& Samples{BlendSpacePlayer->mBlendSpace->GetSamples()};

            for (const FBlendSampleWeight& SampleWeight : RecordedSamples->second) {
                if (SampleWeight.mSampleIndex < 0 || static_cast<std::size_t>(SampleWeight.mSampleIndex) >= Samples.size()) {
                    continue;
                }

                const FBlendSample& Sample{Samples[static_cast<std::size_t>(SampleWeight.mSampleIndex)]};
                FAnimNodeValue Entry{};
                Entry.mName = std::format("Sample {}", SampleWeight.mSampleIndex);
                Entry.mValue = std::format("{} @ {:.2f} : {:.0f}%", Sample.mAnimation != nullptr ? FbxPath::ToString(FbxPath::FromString(Sample.mAnimation->GetPath()).stem()) : "?", Sample.mSampleValue.X, SampleWeight.mWeight * 100.0f);
                Values.push_back(std::move(Entry));
            }
        }

        Displays.resize(Values.size());
        InputLabels.reserve(Inputs.size());
        Builder.FitHeader(Title, Type);

        for (const FAnimNodePoseInput& Input : Inputs) {
            InputLabels.push_back(Playing ? std::format("{}  {:.0f}%", Input.mName, WeightOf(LinkWeights, Input.mLink) * 100.0f) : Input.mName);
            Builder.FitPins(InputLabels.back().c_str(), InputLabels.size() == 1 ? "Pose" : nullptr);
        }

        if (Inputs.empty()) {
            Builder.FitPins(nullptr, "Pose");
        }

        if (Playing) {
            Builder.FitProperty("Weight", WeightText.c_str());
        }

        for (std::size_t Index{}; Index < Values.size(); ++Index) {
            const FAnimNodeValue& Value{Values[Index]};
            FAnimNodeValueDisplay& Display{Displays[Index]};

            Display.mName = Value.mName;

            if (const FAnimNodeFloatValue* Float{std::get_if<FAnimNodeFloatValue>(&Value.mValue)}; Float != nullptr && Float->mValue != nullptr) {
                const bool BlendSpaceX{BlendSpacePlayer != nullptr && Float->mValue == &BlendSpacePlayer->mX};

                if (BlendSpaceX && BlendSpacePlayer->mBlendSpace != nullptr) {
                    const auto& Axis{BlendSpacePlayer->mBlendSpace->GetBlendParameter(EBlendSpaceAxis::X)};
                    Display.mName = std::format("{} ({})", Value.mName, Axis.mDisplayName);
                }

                Display.mFloatLabel = std::format("{:.3f}", *Float->mValue);
                const auto Input{std::find_if(ValueInputs.begin(), ValueInputs.end(), [&Value](const FAnimNodeValueInput& Candidate) {
                    return Candidate.mName == Value.mName;
                })};

                if (Input != ValueInputs.end()) {
                    Display.mValueNode = Input->mLink->GetLinkedNode();
                }

                if (Display.mValueNode != nullptr) {
                    Display.mGetter = Graph != nullptr ? Graph->GetSourceGetter(Display.mValueNode) : nullptr;
                    Display.mFloatLabel = Playing ? Display.mName + "  " + Display.mFloatLabel : Display.mName;
                    Builder.FitPins(Display.mFloatLabel.c_str());
                    continue;
                }

                if (Playing) {
                    const FString WidgetText{Display.mName + ": " + Display.mFloatLabel};

                    Builder.FitText(WidgetText.c_str(), 2.0f * Style.FramePadding.x);

                    continue;
                }
            } else if (bool* const* Bool{std::get_if<bool*>(&Value.mValue)}; Bool != nullptr && *Bool != nullptr) {
                Builder.FitText(Value.mName.c_str(), ImGui::GetFrameHeight() + Style.ItemInnerSpacing.x);

                continue;
            }

            const FString* Text{std::get_if<FString>(&Value.mValue)};
            Builder.FitProperty(Display.mName.c_str(), Text != nullptr ? Text->c_str() : Display.mFloatLabel.c_str());
        }

        ed::PushStyleColor(ed::StyleColor_NodeBorder, Mix(ed::GetStyle().Colors[ed::StyleColor_NodeBorder], ActiveColor, 0.55f * Weight));
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 1.0f + 0.75f * Weight);
        Builder.Begin(ed::NodeId{static_cast<std::uintptr_t>(Key)}, MinNodeWidth);
        Builder.Header(Title, Type);

        for (std::size_t Pin{}; Pin < Inputs.size(); ++Pin) {
            Builder.InputPin(ed::PinId{static_cast<std::uintptr_t>(InputPinKey(Key, Pin))}, InputLabels[Pin].c_str(), Inputs[Pin].mLink->mLinkedNode != nullptr, FAnimGraphNodeBuilder::PoseColor());

            if (Pin == 0) {
                Builder.OutputPin(ed::PinId{static_cast<std::uintptr_t>(OutputPinKey(Key))}, "Pose", true, FAnimGraphNodeBuilder::PoseColor(), true);
            }
        }

        if (Inputs.empty()) {
            Builder.OutputPin(ed::PinId{static_cast<std::uintptr_t>(OutputPinKey(Key))}, "Pose", true, FAnimGraphNodeBuilder::PoseColor(), false);
        }

        for (std::size_t Index{}; Index < Values.size(); ++Index) {
            const FAnimNodeValueDisplay& Display{Displays[Index]};

            if (Display.mValueNode != nullptr) {
                const FAnimNodeValue& Value{Values[Index]};
                const uint64 Pin{FloatInputBit | InputPinKey(Key, Index)};
                const FAnimGraphVariableGetter* Getter{Display.mGetter};
                const uint64 ValueKey{ValueNodeKeys.try_emplace(Display.mValueNode, ValueNodeKeys.size() + 1).first->second};
                const uint64 GetterKey{GetterBit | GraphKey | (Graph != nullptr ? SavedGetterBit | (Graph->GetValueNodeKey(Display.mValueNode) << 8) : ValueKey << 8)};

                Builder.InputPin(ed::PinId{static_cast<std::uintptr_t>(Pin)}, Display.mFloatLabel.c_str(), true, FloatColor);
                FloatLinks.push_back({Pin, Key, GetterKey, Getter, Display.mValueNode, Display.mName, *std::get<FAnimNodeFloatValue>(Value.mValue).mValue});
            }
        }

        Builder.BeginBody();

        if (Playing) {
            Builder.DrawProperty("Weight", WeightText.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.19f, 0.23f, 0.30f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ActiveColor);
            ImGui::ProgressBar(Weight, {Builder.GetWidth(), 4.0f}, "");
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }

        for (std::size_t Index{}; Index < Values.size(); ++Index) {
            const FAnimNodeValueDisplay& Display{Displays[Index]};

            if (Display.mValueNode != nullptr) {
                continue;
            }

            const FAnimNodeValue& Value{Values[Index]};
            ImGui::PushID(Value.mName.c_str());

            if (const FAnimNodeFloatValue* Float{std::get_if<FAnimNodeFloatValue>(&Value.mValue)}; Float != nullptr && Float->mValue != nullptr) {
                if (!Playing) {
                    Builder.DrawProperty(Display.mName.c_str(), Display.mFloatLabel.c_str());
                } else {
                    ImGui::SetNextItemWidth(Builder.GetWidth());

                    if (Float->mMin < Float->mMax) {
                        ImGui::SliderFloat("##Value", Float->mValue, Float->mMin, Float->mMax, (Display.mName + ": %.3f").c_str());
                    } else {
                        ImGui::DragFloat("##Value", Float->mValue, 0.01f, 0.0f, 0.0f, (Display.mName + ": %.3f").c_str());
                    }
                }
            } else if (bool* const* Bool{std::get_if<bool*>(&Value.mValue)}; Bool != nullptr && *Bool != nullptr) {
                ImGui::BeginDisabled(!Playing);
                ImGui::Checkbox(Value.mName.c_str(), *Bool);
                ImGui::EndDisabled();
            } else {
                const FString* Text{std::get_if<FString>(&Value.mValue)};
                Builder.DrawProperty(Value.mName.c_str(), Text != nullptr ? Text->c_str() : "");
            }

            ImGui::PopID();
        }

        Builder.End();
        ed::PopStyleVar();
        ed::PopStyleColor();
    }

    std::unordered_set<uint64> DrawnGetters{};

    for (const FFloatLink& Link : FloatLinks) {
        const uint64 Key{Link.mGetterKey};

        if (!DrawnGetters.insert(Key).second) {
            continue;
        }

        if (mCanvas->mPlacedNodes.insert(Key).second) {
            const ImVec2 Target{ed::GetNodePosition(ed::NodeId{static_cast<std::uintptr_t>(Link.mTarget)})};
            const ImVec2 Position{Link.mGetter != nullptr ? ImVec2{Link.mGetter->mX, Link.mGetter->mY} : ImVec2{Target.x - 245.0f, Target.y - 170.0f}};

            ed::SetNodePosition(ed::NodeId{static_cast<std::uintptr_t>(Key)}, Position);
            Placed = true;
        }

        const FString Value{std::format("{:.3f}", Link.mValue)};
        FAnimGraphNodeBuilder::DrawFloatGetter(ed::NodeId{static_cast<std::uintptr_t>(Key)}, ed::PinId{static_cast<std::uintptr_t>(OutputPinKey(Key))}, Link.mValueNode->GetDisplayName().c_str(), Value.c_str(), true);
    }

    for (const FFloatLink& Link : FloatLinks) {
        const auto Input{static_cast<std::uintptr_t>(Link.mInput)};
        const auto Output{static_cast<std::uintptr_t>(OutputPinKey(Link.mGetterKey))};

        ed::Link(ed::LinkId{Input}, ed::PinId{Output}, ed::PinId{Input}, FloatColor, 2.0f);
    }

    if (Playing) {
        const std::uintptr_t Hovered{ed::GetHoveredLink().Get()};
        const auto Link{std::find_if(FloatLinks.begin(), FloatLinks.end(), [Hovered](const FFloatLink& Candidate) {
            return Candidate.mInput == Hovered;
        })};

        if (Link != FloatLinks.end()) {
            ed::Suspend();
            ImGui::SetTooltip("%s -> %s  %.3f", Link->mValueNode->GetDisplayName().c_str(), Link->mParameter.c_str(), Link->mValue);
            ed::Resume();
        }
    }

    const bool Pulse{Playing && std::fmod(ImGui::GetTime(), 1.5) < static_cast<double>(ImGui::GetIO().DeltaTime)};
    const auto DrawLink{[Pulse, &LinkColor](uint64 Id, uint64 From, uint64 To, float Weight) {
        const ed::LinkId Link{static_cast<std::uintptr_t>(Id)};

        ed::Link(Link, ed::PinId{static_cast<std::uintptr_t>(From)}, ed::PinId{static_cast<std::uintptr_t>(To)}, LinkColor(Weight), 1.5f + 2.0f * Weight);

        if (Pulse && Weight > 0.0f) {
            ed::Flow(Link);
        }
    }};

    if (Root != nullptr) {
        DrawLink(OutputKey, OutputPinKey(NodeKey(Instance, Root)), OutputKey, WeightOf(NodeWeights, Root));
    }

    for (FAnimNode_Base* Node : Nodes) {
        const uint64 Key{NodeKey(Instance, Node)};
        std::vector<FAnimNodePoseInput> Inputs{};

        Node->GetPoseInputs(Inputs);
        if (dynamic_cast<FAnimNode_StateMachine*>(Node) != nullptr) {
            Inputs.clear();
        }

        for (std::size_t Pin{}; Pin < Inputs.size(); ++Pin) {
            if (FAnimNode_Base* Linked{Inputs[Pin].mLink->mLinkedNode}) {
                const float Weight{WeightOf(LinkWeights, Inputs[Pin].mLink)};

                DrawLink(InputPinKey(Key, Pin), OutputPinKey(NodeKey(Instance, Linked)), InputPinKey(Key, Pin), Weight);
            }
        }
    }

    if (mCanvas->mFitRequested && !Placed) {
        ed::NavigateToContent(0.0f);
    }

    mCanvas->mFitRequested = Placed;

    if (ed::BeginCreate()) {
        ed::PinId Start{};
        ed::PinId End{};

        if (ed::QueryNewLink(&Start, &End)) {
            ed::RejectNewItem(InactiveColor);
        }
    }

    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId Link{};
        ed::NodeId DeletedNode{};

        while (ed::QueryDeletedLink(&Link)) {
            ed::RejectDeletedItem();
        }

        while (ed::QueryDeletedNode(&DeletedNode)) {
            ed::RejectDeletedItem();
        }
    }

    ed::EndDelete();
    const std::uintptr_t DoubleClicked{ed::GetDoubleClickedNode().Get()};
    int32 MachineId{-1};

    for (auto* Node : Nodes) {
        if (NodeKey(Instance, Node) == DoubleClicked && dynamic_cast<FAnimNode_StateMachine*>(Node) != nullptr) {
            const auto* Source{Graph != nullptr ? Graph->GetSourceNode(Node) : nullptr};
            MachineId = Source != nullptr ? Source->mId : -1;
            break;
        }
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);

    if (MachineId >= 0) {
        OpenStateMachine(MachineId);
    }
}

void FAnimGraphPanel::DrawStateMachine(UAnimInstance& Instance, bool Playing) {
    // 실행 노드가 실제 평가된 경우에만 현재 상태와 전환 가중치를 활성 색상으로 표시한다.
    const auto* Machine{mView.mGraph != nullptr ? mView.mGraph->FindStateMachine(mView.mMachineId) : nullptr};
    const auto* Runtime{mView.mMachine};

    if (Machine == nullptr || Runtime == nullptr) {
        return;
    }

    const bool Active{Playing && IsVisited(Instance, Runtime)};
    const int32 Current{Runtime->GetCurrentState()};
    const int32 Target{Runtime->GetTargetState()};
    const float Alpha{Runtime->GetBlendAlpha()};
    ImGui::TextDisabled("Double-click a state or transition to open it | %s", Active ? "Active" : "Inactive");
    ed::SetCurrentEditor(mCanvas->mEditor);
    FAnimGraphNodeBuilder::ApplyStyle();
    ed::Begin("DebugStateMachine");
    const bool Placed{mLayoutRequested || mCanvas->mPlacedNodes.empty()};
    mLayoutRequested = false;

    if (Placed) {
        ed::SetNodePosition(ed::NodeId{1}, {-180.0f, 0.0f});
        ed::SetNodePosition(ed::NodeId{StateNodeKey(FAnimStateMachineData::AnyStateId)}, {Machine->GetAnyStateX(), Machine->GetAnyStateY()});

        for (const auto& State : Machine->GetStates()) {
            ed::SetNodePosition(ed::NodeId{StateNodeKey(State.mId)}, {State.mX, State.mY});
        }

        mCanvas->mPlacedNodes.insert(1);
    }

    FAnimGraphNodeBuilder Entry{};
    Entry.FitHeader("Entry", "Entry");
    Entry.FitPins(nullptr, "State");
    Entry.Begin(ed::NodeId{1}, 120.0f);
    Entry.Header("Entry", "Entry");
    Entry.OutputPin(ed::PinId{2}, "State", Machine->GetEntryState() >= 0, StateColor, false);
    Entry.End();

    const std::uintptr_t AnyStateKey{StateNodeKey(FAnimStateMachineData::AnyStateId)};
    const bool AnyStateConnected{std::any_of(Machine->GetTransitions().begin(), Machine->GetTransitions().end(), [](const auto& Transition) {
        return Transition.mFromState == FAnimStateMachineData::AnyStateId;
    })};
    FAnimGraphNodeBuilder AnyState{};
    AnyState.FitHeader("Any State", "Any State");
    AnyState.FitPins(nullptr, "Out");
    AnyState.Begin(ed::NodeId{AnyStateKey}, 140.0f);
    AnyState.Header("Any State", "Any State");
    AnyState.OutputPin(ed::PinId{AnyStateKey + 2}, "Out", AnyStateConnected, StateColor, false);
    AnyState.End();

    for (const auto& State : Machine->GetStates()) {
        const auto* StateRuntime{Runtime->FindState(State.mId)};
        const float Weight{!Active ? 0.0f : State.mId == Target ? Alpha : State.mId == Current ? (Target >= 0 ? 1.0f - Alpha : 1.0f) : 0.0f};
        const FString WeightText{std::format("{:.0f}%", Weight * 100.0f)};
        const FString TimeText{std::format("{:.2f} s", StateRuntime != nullptr ? StateRuntime->mElapsed : 0.0f)};
        const char* Status{Weight > 0.0f ? (State.mId == Target ? "Blending In" : Target >= 0 ? "Blending Out" : "Current") : "Inactive"};
        const std::uintptr_t Key{StateNodeKey(State.mId)};
        FAnimGraphNodeBuilder Builder{};
        Builder.FitHeader(State.mName.c_str(), "State");
        Builder.FitPins("In", "Out");
        Builder.FitProperty("Status", Status);
        Builder.FitProperty("Weight", WeightText.c_str());
        Builder.FitProperty("Time", TimeText.c_str());
        ed::PushStyleColor(ed::StyleColor_NodeBorder, Weight > 0.0f ? ActiveColor : InactiveColor);
        Builder.Begin(ed::NodeId{Key}, 170.0f);
        Builder.Header(State.mName.c_str(), "State");
        Builder.InputPin(ed::PinId{Key + 1}, "In", true, StateColor);
        Builder.OutputPin(ed::PinId{Key + 2}, "Out", true, StateColor, true);
        Builder.BeginBody();
        Builder.DrawProperty("Status", Status);
        Builder.DrawProperty("Weight", WeightText.c_str());
        Builder.DrawProperty("Time", TimeText.c_str());
        Builder.End();
        ed::PopStyleColor();
    }

    if (Machine->FindState(Machine->GetEntryState()) != nullptr) {
        ed::Link(ed::LinkId{1}, ed::PinId{2}, ed::PinId{StateNodeKey(Machine->GetEntryState()) + 1}, StateColor, 2.5f);
    }

    for (const auto& Transition : Machine->GetTransitions()) {
        const bool Blending{Active && Runtime->GetActiveTransition() == Transition.mId};
        const ed::LinkId Link{static_cast<std::uintptr_t>(Transition.mId) + 2};
        ed::Link(Link, ed::PinId{StateNodeKey(Transition.mFromState) + 2}, ed::PinId{StateNodeKey(Transition.mToState) + 1}, Blending ? ActiveColor : StateColor, Blending ? 4.0f : 2.0f);
    }

    const auto Hovered{ed::GetHoveredLink().Get()};
    const auto* Transition{Hovered >= 2 ? Machine->FindTransition(static_cast<int32>(Hovered - 2)) : nullptr};

    if (Transition != nullptr) {
        const auto* From{Machine->FindState(Transition->mFromState)};
        const auto* To{Machine->FindState(Transition->mToState)};
        const bool AnyStateSource{Transition->mFromState == FAnimStateMachineData::AnyStateId};
        const auto* State{Runtime->FindState(AnyStateSource ? Current : Transition->mFromState)};
        const bool SourceActive{Active && State != nullptr && (AnyStateSource || Transition->mFromState == Current)};
        const bool AlreadyActive{AnyStateSource && Transition->mToState == Current};
        const FAnimationUpdateContext Context{Instance, 0.0f};
        const bool Allowed{FAnimTransitionEvaluator::Evaluate(Transition->mRule, Context, State != nullptr ? State->mPose.mLinkedNode : nullptr, State != nullptr ? State->mElapsed : 0.0f)};
        ed::Suspend();
        ImGui::SetTooltip("%s -> %s\nRule: %s | Blend: %.3f s | Priority: %d\n%s\nDouble-click to inspect the rule", AnyStateSource ? "Any State" : From != nullptr ? From->mName.c_str() : "?", To != nullptr ? To->mName.c_str() : "?", Allowed ? "true" : "false", Transition->mDuration, Transition->mPriority, !SourceActive ? "Source state inactive" : AlreadyActive ? "Target state already active" : Target >= 0 ? "Rules paused while blending" : "Source state active");
        ed::Resume();
    }

    RejectGraphEdits();
    const auto ClickedNode{ed::GetDoubleClickedNode().Get()};
    const auto ClickedLink{ed::GetDoubleClickedLink().Get()};

    if (mCanvas->mFitRequested && !Placed) {
        ed::NavigateToContent(0.0f);
    }

    mCanvas->mFitRequested = Placed;
    ed::End();
    ed::SetCurrentEditor(nullptr);

    if (ClickedNode >= 32) {
        OpenState(static_cast<int32>(ClickedNode / 16 - 2));
    } else if (ClickedLink >= 2) {
        OpenTransitionRule(static_cast<int32>(ClickedLink - 2));
    }
}

void FAnimGraphPanel::DrawTransitionRule(UAnimInstance& Instance, bool Playing) {
    // 시간을 진행하지 않고 출발 상태의 현재 값으로 각 규칙 노드를 평가해 판정 과정을 보여준다.
    const auto* Machine{mView.mGraph != nullptr ? mView.mGraph->FindStateMachine(mView.mMachineId) : nullptr};
    const auto* Transition{Machine != nullptr ? Machine->FindTransition(mView.mTransition) : nullptr};
    const auto* Runtime{mView.mMachine};

    if (Transition == nullptr || Runtime == nullptr) {
        return;
    }

    const auto& Graph{Transition->mRule};
    const bool AnyStateSource{Transition->mFromState == FAnimStateMachineData::AnyStateId};
    const int32 Current{Runtime->GetCurrentState()};
    const auto* State{Runtime->FindState(AnyStateSource ? Current : Transition->mFromState)};
    const FAnimationUpdateContext Context{Instance, 0.0f};
    std::vector<FAnimRuleDebugValue> Values{};
    FAnimTransitionEvaluator::GetDebugValues(Graph, Context, State != nullptr ? State->mPose.mLinkedNode : nullptr, State != nullptr ? State->mElapsed : 0.0f, Values);
    const auto ValueFor{[&Values](int32 Id) -> const FAnimRuleDebugValue* {
        const auto Found{std::find_if(Values.begin(), Values.end(), [Id](const auto& Value) {
            return Value.mId == Id;
        })};

        return Found != Values.end() ? &*Found : nullptr;
    }};
    const bool Active{Playing && IsVisited(Instance, Runtime) && State != nullptr && (AnyStateSource || Current == Transition->mFromState)};
    const bool AlreadyActive{AnyStateSource && Current == Transition->mToState};
    const FString ResultText{RuleValueText(ValueFor(Graph.GetResult()))};
    ImGui::TextDisabled("Live values | %s | Blend %.3f s | Priority %d", !Active ? "Source state inactive" : AlreadyActive ? "Target state already active" : Runtime->GetTargetState() >= 0 ? "Rules paused while blending" : "Source state active", Transition->mDuration, Transition->mPriority);
    ed::SetCurrentEditor(mCanvas->mEditor);
    FAnimGraphNodeBuilder::ApplyStyle();
    ed::Begin("DebugTransitionRule");
    const bool Placed{mLayoutRequested || mCanvas->mPlacedNodes.empty()};
    mLayoutRequested = false;

    if (Placed) {
        ed::SetNodePosition(ed::NodeId{1}, {450.0f, 0.0f});

        for (const auto& Node : Graph.GetNodes()) {
            ed::SetNodePosition(ed::NodeId{StateNodeKey(Node.mId)}, {Node.mX, Node.mY});
        }

        mCanvas->mPlacedNodes.insert(1);
    }

    FAnimGraphNodeBuilder Result{};
    Result.FitHeader("Result", "Can Enter Transition");
    Result.FitPins("Can Enter");
    Result.FitProperty("Value", ResultText.c_str());
    Result.Begin(ed::NodeId{1}, 180.0f);
    Result.Header("Result", "Can Enter Transition");
    Result.InputPin(ed::PinId{1}, "Can Enter", Graph.GetResult() >= 0, BoolColor);
    Result.BeginBody();
    Result.DrawProperty("Value", ResultText.c_str());
    Result.End();

    for (const auto& Node : Graph.GetNodes()) {
        const auto Key{StateNodeKey(Node.mId)};
        const char* Title{FAnimTransitionGraph::GetTitle(Node.mType)};
        const bool Bool{FAnimTransitionGraph::IsBool(Node.mType)};
        const int32 InputCount{FAnimTransitionGraph::GetInputCount(Node.mType)};
        const ImVec4 InputColor{FAnimTransitionGraph::IsBoolInput(Node.mType) ? BoolColor : FAnimGraphNodeBuilder::FloatColor()};
        const FString Value{RuleValueText(ValueFor(Node.mId))};
        FAnimGraphNodeBuilder Builder{};
        Builder.FitHeader(Title, Title);
        Builder.FitPins(InputCount > 0 ? "A" : nullptr, Bool ? "Bool" : "Float");
        Builder.FitText(Node.mName.c_str());
        Builder.FitProperty("Value", Value.c_str());
        Builder.Begin(ed::NodeId{Key}, 160.0f);
        Builder.Header(Title, Title);

        if (InputCount > 0) {
            Builder.InputPin(ed::PinId{Key + 1}, "A", Node.mInputA >= 0, InputColor);
        }

        Builder.OutputPin(ed::PinId{Key + 3}, Bool ? "Bool" : "Float", true, Bool ? BoolColor : FAnimGraphNodeBuilder::FloatColor(), InputCount > 0);

        if (InputCount > 1) {
            Builder.InputPin(ed::PinId{Key + 2}, "B", Node.mInputB >= 0, InputColor);
        }

        Builder.BeginBody();

        if (!Node.mName.empty()) {
            Builder.DrawText(Node.mName.c_str(), StateColor);
        }

        Builder.DrawProperty("Value", Value.c_str());
        Builder.End();
    }

    for (const auto& Node : Graph.GetNodes()) {
        const auto Key{StateNodeKey(Node.mId)};
        const ImVec4 Color{FAnimTransitionGraph::IsBoolInput(Node.mType) ? BoolColor : FAnimGraphNodeBuilder::FloatColor()};

        for (int32 Pin{}; Pin < FAnimTransitionGraph::GetInputCount(Node.mType); ++Pin) {
            const int32 Input{Pin == 0 ? Node.mInputA : Node.mInputB};

            if (Input >= 0) {
                ed::Link(ed::LinkId{Key + Pin + 1}, ed::PinId{StateNodeKey(Input) + 3}, ed::PinId{Key + Pin + 1}, Color, 2.0f);
            }
        }
    }

    if (Graph.GetResult() >= 0) {
        ed::Link(ed::LinkId{1}, ed::PinId{StateNodeKey(Graph.GetResult()) + 3}, ed::PinId{1}, BoolColor, 2.0f);
    }

    RejectGraphEdits();

    if (mCanvas->mFitRequested && !Placed) {
        ed::NavigateToContent(0.0f);
    }

    mCanvas->mFitRequested = Placed;
    ed::End();
    ed::SetCurrentEditor(nullptr);
}
