#include "EnginePCH.h"
#include "FAnimGraphPanel.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimNode_Base.h"
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

    struct FFloatLink {
    public:
        uint64 mInput{};
        uint64 mTarget{};
        uint64 mGetterKey{};
        const FAnimGraphVariableGetter* mGetter{};
        std::size_t mVariable{};
        FString mParameter{};
        float mValue{};
    };

    uint64 NodeKey(const UAnimInstance& Instance, const FAnimNode_Base* Node) {
        if (Node->mGraphNodeId >= 0) {
            return ((static_cast<uint64>(Node->mGraphNodeId) + 1) << 8) | (uint64{1} << 56);
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

        std::vector<FAnimNodePoseInput> Inputs{};

        Node->GetPoseInputs(Inputs);

        for (const FAnimNodePoseInput& Input : Inputs) {
            AssignDepth(Input.mLink->mLinkedNode, Depth + 1, Depths, Guard - 1);
        }
    }
}

FAnimGraphPanel::FAnimGraphPanel() {
    ed::Config Config{};

    Config.SettingsFile = nullptr;
    mEditor = ed::CreateEditor(&Config);
}

FAnimGraphPanel::~FAnimGraphPanel() {
    ed::DestroyEditor(mEditor);
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
        DrawGraph(*Instance, Playing);
        ImGui::EndTable();
    }
}

void FAnimGraphPanel::DrawVariables(UAnimInstance& Instance, bool Playing) {
    ImGui::SeparatorText("Variables");

    bool HasVariables{};

    std::vector<FAnimNodeValue> RuntimeVariables{};

    Instance.GetDebugVariables(RuntimeVariables);

    if (!RuntimeVariables.empty()) {
        ImGui::SeparatorText("Graph Variables");
    }

    for (FAnimNodeValue& Variable : RuntimeVariables) {
        HasVariables = true;

        ImGui::PushID(Variable.mName.c_str());

        if (Variable.mType == EAnimNodeValueType::Float && Variable.mFloat != nullptr) {
            if (Playing && Variable.mBinding.empty()) {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##Value", Variable.mFloat, 0.01f, 0.0f, 0.0f, (Variable.mName + ": %.3f").c_str());
            } else {
                ImGui::Text("%s: %.3f", Variable.mName.c_str(), *Variable.mFloat);

                if (!Variable.mBinding.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("<- %s", Variable.mBinding.c_str());
                }
            }
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
        ImGui::Text("Update: %llu", static_cast<unsigned long long>(Instance.GetUpdateCounter()));
    } else {
        ImGui::Spacing();
        ImGui::TextWrapped("Edit graph defaults in the Anim Graph editor.");
    }
}

bool FAnimGraphPanel::PlaceNodes(const UAnimInstance& Instance, FAnimNode_Base* Root, bool All) {
    std::vector<FAnimNode_Base*> Nodes{};

    CollectNodes(Root, Nodes);

    if (All) {
        mPlacedNodes.clear();
    }

    const bool Missing{!mPlacedNodes.contains(OutputKey) || std::any_of(Nodes.begin(), Nodes.end(), [this, &Instance](const FAnimNode_Base* Node) {
        return !mPlacedNodes.contains(NodeKey(Instance, Node));
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
        mPlacedNodes.insert(Key);
    }};
    float X{};
    float Y{};

    if (!mPlacedNodes.contains(OutputKey)) {
        if (Instance.GetNodeLayout(nullptr, X, Y)) {
            Place(OutputKey, X, Y);
        } else {
            Place(OutputKey, 0.0f, 0.0f);
        }
    }

    for (const auto& [Depth, Column] : Columns) {
        const float Top{-0.5f * static_cast<float>(Column.size() - 1) * RowSpacing};

        for (std::size_t Row{}; Row < Column.size(); ++Row) {
            const uint64 Key{NodeKey(Instance, Column[Row])};

            if (mPlacedNodes.contains(Key)) {
                continue;
            }

            if (Instance.GetNodeLayout(Column[Row], X, Y)) {
                Place(Key, X, Y);
            } else {
                Place(Key, -static_cast<float>(Depth) * ColumnSpacing, Top + static_cast<float>(Row) * RowSpacing);
            }
        }
    }

    return true;
}

void FAnimGraphPanel::DrawGraph(UAnimInstance& Instance, bool Playing) {
    FAnimNode_Base* Root{Instance.GetRootNode()};
    const auto* Graph{Cast<UAnimGraphInstance>(&Instance)};
    const uint64 GraphKey{Graph != nullptr ? (static_cast<uint64>(std::hash<FString>{}(Graph->GetAnimGraph())) & 0xFFFFF) << 39 : 0};
    std::vector<FAnimNodeValue> Variables{};
    std::vector<FFloatLink> FloatLinks{};

    Instance.GetDebugVariables(Variables);

    const uint64 Counter{Instance.GetUpdateCounter()};
    const auto WeightOf{[Playing, Counter](const FAnimNode_Base* Node) {
        return Playing && Node != nullptr ? std::clamp(Node->GetDebugWeight(Counter), 0.0f, 1.0f) : 0.0f;
    }};
    const auto LinkColor{[](float Weight) {
        return Weight > 0.0f ? Mix(InactiveColor, ActiveColor, 0.35f + 0.65f * Weight) : InactiveColor;
    }};

    ed::SetCurrentEditor(mEditor);
    FAnimGraphNodeBuilder::ApplyStyle();
    ed::Begin("AnimGraphEditor", ImGui::GetContentRegionAvail());

    bool Placed{PlaceNodes(Instance, Root, mLayoutRequested)};
    mLayoutRequested = false;

    std::vector<FAnimNode_Base*> Nodes{};

    CollectNodes(Root, Nodes);

    FAnimGraphNodeBuilder OutputBuilder{};
    const ImGuiStyle& Style{ImGui::GetStyle()};
    const ImVec4 FloatColor{FAnimGraphNodeBuilder::FloatColor()};

    ed::PushStyleColor(ed::StyleColor_NodeBorder, Root != nullptr && WeightOf(Root) > 0.0f ? ActiveColor : InactiveColor);
    OutputBuilder.FitHeader("Output Pose", "Output Pose");
    OutputBuilder.FitPins("Result");
    OutputBuilder.Begin(ed::NodeId{static_cast<std::uintptr_t>(OutputKey)}, 160.0f);
    OutputBuilder.Header("Output Pose", "Output Pose");
    OutputBuilder.InputPin(ed::PinId{static_cast<std::uintptr_t>(OutputKey)}, "Result", Root != nullptr, FAnimGraphNodeBuilder::PoseColor());
    OutputBuilder.End();
    ed::PopStyleColor();

    for (FAnimNode_Base* Node : Nodes) {
        const uint64 Key{NodeKey(Instance, Node)};
        const float Weight{WeightOf(Node)};
        const char* Type{Node->GetNodeType()};
        const char* Title{Node->mName.empty() ? Type : Node->mName.c_str()};
        const FString WeightText{Playing ? std::format("{:.0f}%", Weight * 100.0f) : FString{}};
        std::vector<FAnimNodePoseInput> Inputs{};
        std::vector<FAnimNodeValue> Values{};
        std::vector<FString> InputLabels{};
        FAnimGraphNodeBuilder Builder{};

        Node->GetPoseInputs(Inputs);
        Node->GetDebugValues(Values);
        std::vector<int32> VariableIndices{};

        VariableIndices.resize(Values.size(), -1);
        InputLabels.reserve(Inputs.size());
        Builder.FitHeader(Title, Type);

        for (const FAnimNodePoseInput& Input : Inputs) {
            InputLabels.push_back(Playing ? std::format("{}  {:.0f}%", Input.mName, Input.mWeight * 100.0f) : Input.mName);
            Builder.FitPins(InputLabels.back().c_str(), InputLabels.size() == 1 ? "Pose" : nullptr);
        }

        if (Inputs.empty()) {
            Builder.FitPins(nullptr, "Pose");
        }

        if (Playing) {
            Builder.FitProperty("Weight", WeightText.c_str());
        }

        for (std::size_t Index{}; Index < Values.size(); ++Index) {
            FAnimNodeValue& Value{Values[Index]};

            if (Value.mType == EAnimNodeValueType::Float && Value.mFloat != nullptr) {
                Value.mText = std::format("{:.3f}", *Value.mFloat);

                if (!Value.mBinding.empty()) {
                    const auto Variable{std::find_if(Variables.begin(), Variables.end(), [&Value](const FAnimNodeValue& Candidate) {
                        return Candidate.mName == Value.mBinding && Candidate.mFloat != nullptr;
                    })};

                    if (Variable != Variables.end()) {
                        VariableIndices[Index] = static_cast<int32>(Variable - Variables.begin());
                        Value.mText = Playing ? Value.mName + "  " + Value.mText : Value.mName;
                        Builder.FitPins(Value.mText.c_str());

                        continue;
                    }

                    Value.mText += " <- " + Value.mBinding;
                } else if (Playing) {
                    const FString WidgetText{Value.mName + ": " + Value.mText};

                    Builder.FitText(WidgetText.c_str(), 2.0f * Style.FramePadding.x);

                    continue;
                }
            } else if (Value.mType == EAnimNodeValueType::Bool && Value.mBool != nullptr) {
                Builder.FitText(Value.mName.c_str(), ImGui::GetFrameHeight() + Style.ItemInnerSpacing.x);

                continue;
            }

            Builder.FitProperty(Value.mName.c_str(), Value.mText.c_str());
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
            if (VariableIndices[Index] >= 0) {
                const FAnimNodeValue& Value{Values[Index]};
                const uint64 Pin{FloatInputBit | InputPinKey(Key, Index)};
                const std::size_t Variable{static_cast<std::size_t>(VariableIndices[Index])};
                const FAnimGraphVariableGetter* Getter{Graph != nullptr && Value.mBindingParameter != nullptr ? Graph->GetVariableGetter(Node->mGraphNodeId, Value.mBindingParameter) : nullptr};
                const uint64 GetterKey{GetterBit | GraphKey | (Getter != nullptr ? SavedGetterBit | ((static_cast<uint64>(Getter->mId) + 1) << 8) : ((static_cast<uint64>(Variable) + 1) << 8))};

                Builder.InputPin(ed::PinId{static_cast<std::uintptr_t>(Pin)}, Value.mText.c_str(), true, FloatColor);
                FloatLinks.push_back({Pin, Key, GetterKey, Getter, Variable, Value.mName, *Value.mFloat});
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
            if (VariableIndices[Index] >= 0) {
                continue;
            }

            FAnimNodeValue& Value{Values[Index]};
            ImGui::PushID(Value.mName.c_str());

            if (Value.mType == EAnimNodeValueType::Float && Value.mFloat != nullptr) {
                if (!Value.mBinding.empty() || !Playing) {
                    Builder.DrawProperty(Value.mName.c_str(), Value.mText.c_str(), !Value.mBinding.empty());
                } else {
                    ImGui::SetNextItemWidth(Builder.GetWidth());

                    if (Value.mMin < Value.mMax) {
                        ImGui::SliderFloat("##Value", Value.mFloat, Value.mMin, Value.mMax, (Value.mName + ": %.3f").c_str());
                    } else {
                        ImGui::DragFloat("##Value", Value.mFloat, 0.01f, 0.0f, 0.0f, (Value.mName + ": %.3f").c_str());
                    }
                }
            } else if (Value.mType == EAnimNodeValueType::Bool && Value.mBool != nullptr) {
                ImGui::BeginDisabled(!Playing || !Value.mBinding.empty());
                ImGui::Checkbox(Value.mName.c_str(), Value.mBool);
                ImGui::EndDisabled();
            } else {
                Builder.DrawProperty(Value.mName.c_str(), Value.mText.c_str());
            }

            ImGui::PopID();
        }

        Builder.End();
        ed::PopStyleVar();
        ed::PopStyleColor();
    }

    std::unordered_set<uint64> DrawnGetters{};

    for (const FFloatLink& Link : FloatLinks) {
        const FAnimNodeValue& Variable{Variables[Link.mVariable]};
        const uint64 Key{Link.mGetterKey};

        if (!DrawnGetters.insert(Key).second) {
            continue;
        }

        if (mPlacedNodes.insert(Key).second) {
            const ImVec2 Target{ed::GetNodePosition(ed::NodeId{static_cast<std::uintptr_t>(Link.mTarget)})};
            const ImVec2 Position{Link.mGetter != nullptr ? ImVec2{Link.mGetter->mX, Link.mGetter->mY} : ImVec2{Target.x - 245.0f, Target.y - 170.0f}};

            ed::SetNodePosition(ed::NodeId{static_cast<std::uintptr_t>(Key)}, Position);
            Placed = true;
        }

        const FString Value{std::format("{:.3f}", *Variable.mFloat)};
        FAnimGraphNodeBuilder::DrawFloatGetter(ed::NodeId{static_cast<std::uintptr_t>(Key)}, ed::PinId{static_cast<std::uintptr_t>(OutputPinKey(Key))}, Variable.mName.c_str(), Value.c_str(), true);
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
            ImGui::SetTooltip("%s -> %s  %.3f", Variables[Link->mVariable].mName.c_str(), Link->mParameter.c_str(), Link->mValue);
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
        DrawLink(OutputKey, OutputPinKey(NodeKey(Instance, Root)), OutputKey, WeightOf(Root));
    }

    for (FAnimNode_Base* Node : Nodes) {
        const uint64 Key{NodeKey(Instance, Node)};
        std::vector<FAnimNodePoseInput> Inputs{};

        Node->GetPoseInputs(Inputs);

        for (std::size_t Pin{}; Pin < Inputs.size(); ++Pin) {
            if (FAnimNode_Base* Linked{Inputs[Pin].mLink->mLinkedNode}) {
                const float Weight{Playing ? std::clamp(Inputs[Pin].mLink->GetDebugWeight(Counter), 0.0f, 1.0f) : 0.0f};

                DrawLink(InputPinKey(Key, Pin), OutputPinKey(NodeKey(Instance, Linked)), InputPinKey(Key, Pin), Weight);
            }
        }
    }

    if (mFitRequested && !Placed) {
        ed::NavigateToContent(0.0f);
    }

    mFitRequested = Placed;

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
    ed::End();
    ed::SetCurrentEditor(nullptr);
}
