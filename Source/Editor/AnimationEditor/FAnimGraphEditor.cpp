#include "EnginePCH.h"
#include "FAnimGraphEditor.h"
#include "FAnimGraphNodeBuilder.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "FAnimationAssetPicker.h"
#include "Animation/AnimGraph.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "UObject/UObjectGlobals.h"

#include "Animation/AnimGraphNodeRegistry.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <utility>

namespace ed = ax::NodeEditor;

namespace {
    constexpr char WindowId[]{"###AnimGraphEditor"};
    constexpr char VariablePayload[]{"MACAW_ANIM_GRAPH_VARIABLE"};
    constexpr std::uintptr_t OutputNode{1};
    constexpr std::uintptr_t OutputInputPin{1};
    constexpr std::uintptr_t OutputPinBits{0xFF};
    // 상위 비트로 Get 노드와 값 입력을 구분하고, 하위 비트에는 노드와 핀 번호를 인코딩한다.
    constexpr std::uintptr_t GetterBit{std::uintptr_t{1} << 61};
    constexpr std::uintptr_t FloatInputBit{std::uintptr_t{1} << 60};

    std::uintptr_t GetterKey(int32 Id) {
        return GetterBit | ((static_cast<std::uintptr_t>(Id) + 1) << 8);
    }

    int32 DecodeGetter(std::uintptr_t Key) {
        return (Key & GetterBit) != 0 ? static_cast<int32>(((Key & ~GetterBit) >> 8) - 1) : -1;
    }

    std::uintptr_t NodeKey(int32 Id) {
        return Id == UAnimGraph::OutputNodeId ? OutputNode : (static_cast<std::uintptr_t>(Id) + 1) << 8;
    }

    std::uintptr_t InputPinKey(int32 Node, int32 Pin) {
        return Node == UAnimGraph::OutputNodeId ? OutputInputPin : NodeKey(Node) | static_cast<std::uintptr_t>(Pin + 1);
    }

    std::uintptr_t OutputPinKey(int32 Node) {
        return NodeKey(Node) | OutputPinBits;
    }

    std::uintptr_t FloatInputKey(int32 Node, std::size_t Parameter) {
        return FloatInputBit | InputPinKey(Node, static_cast<int32>(Parameter));
    }

    bool IsBindableFloat(const FAnimGraphParameterInfo& Info) {
        return Info.mType == EAnimGraphParameterType::Float && Info.mBindable;
    }

    const FAnimGraphParameterInfo* FindFloatInput(const FAnimGraphNode* Node, int32 Parameter) {
        const FAnimGraphNodeType* Type{Node != nullptr ? FAnimGraphNodeRegistry::Find(Node->mType) : nullptr};

        if (Type == nullptr || Parameter < 0 || static_cast<std::size_t>(Parameter) >= Type->mParameters.size()) {
            return nullptr;
        }

        const FAnimGraphParameterInfo& Info{Type->mParameters[static_cast<std::size_t>(Parameter)]};

        return IsBindableFloat(Info) ? &Info : nullptr;
    }

    struct FPinRef {
    public:
        int32 mNode{};
        int32 mPin{};
        bool mOutput{};
    };

    FPinRef DecodePin(std::uintptr_t Key) {
        if (Key == OutputInputPin) {
            return {UAnimGraph::OutputNodeId, 0, false};
        }

        const std::uintptr_t Low{Key & 0xFF};

        return {static_cast<int32>((Key >> 8) - 1), Low == OutputPinBits ? 0 : static_cast<int32>(Low) - 1, Low == OutputPinBits};
    }

    int32 DecodeNode(std::uintptr_t Key) {
        if ((Key & GetterBit) != 0) {
            return -2;
        }

        return Key == OutputNode ? UAnimGraph::OutputNodeId : static_cast<int32>((Key >> 8) - 1);
    }

    FString FormatFloat(float Value) {
        return std::format("{}", Value);
    }
    FString DisplayParameterValue(const FAnimGraphParameterInfo& Info, const FAnimGraphNode& Node) {
        const FString* Value{Node.FindParameter(Info.mName)};
        const FString Text{Value != nullptr ? *Value : Info.mDefault};

        if (Info.mType == EAnimGraphParameterType::AnimSequence || Info.mType == EAnimGraphParameterType::BlendSpace) {
            return Text.empty() ? FString{"None"} : FbxPath::ToString(FbxPath::FromString(Text).stem());
        }

        return Text;
    }
}

FAnimGraphEditor::FAnimGraphEditor() {
    ed::Config Config{};

    Config.SettingsFile = nullptr;
    mEditor = ed::CreateEditor(&Config);
    mGraphContexts.emplace("Initial", mEditor);
}

FAnimGraphEditor::~FAnimGraphEditor() {
    ClearGraphContexts();
}

bool FAnimGraphEditor::Open(const std::filesystem::path& Path, FString& Error) {
    UAnimGraph* Source{UAssetManager::GetAssetByPath<UAnimGraph>(FAssetUtils::GetAssetKey(FbxPath::ToString(Path)))};

    if (Source == nullptr) {
        Error = "Cannot load animation graph: " + FbxPath::ToString(Path);

        return false;
    }

    mNavigation.clear();
    ClearGraphContexts();
    // 저장 전 편집이 현재 실행 중인 공유 에셋에 바로 반영되지 않도록 깊은 복사본을 만든다.
    mRootEditing.reset(NewObject<UAnimGraph>());
    mEditing = mRootEditing.get();
    mEditing->CopyFrom(*Source);
    mContextNode = 0;

    mPath = Path;
    mDirty = false;
    mOpen = true;
    mApplyPositions = true;
    mSelectedNode = -2;
    mNewVariable.fill('\0');
    mError.clear();
    mView = {};
    mView.mGraph = mEditing;
    ActivateView(mView);
    ImGui::SetWindowFocus(WindowId);

    return true;
}


bool FAnimGraphEditor::IsFocused() const {
    ImGuiWindow* Window{mOpen ? ImGui::FindWindowByName(WindowId) : nullptr};
    ImGuiWindow* Focused{ImGui::GetCurrentContext()->NavWindow};

    return Window != nullptr && Focused != nullptr && ImGui::IsWindowChildOf(Focused, Window, true, false);
}

void FAnimGraphEditor::Close() {
    mOpen = false;
    mEditing = nullptr;
    mNavigation.clear();
    mRootEditing.reset();
}

bool FAnimGraphEditor::Save() {
    if (mEditing == nullptr) {
        return false;
    }

    // 상태 내부나 전환 규칙을 보고 있어도 저장 단위는 최상위 그래프 에셋이다.
    if (!UAssetManager::SaveAnimGraph(FbxPath::ToString(mPath), *mRootEditing, mError)) {
        return false;
    }

    mDirty = false;
    mError.clear();

    return true;
}

void FAnimGraphEditor::Draw() {
    if (!mOpen || mEditing == nullptr) {
        return;
    }

    const FString Title{std::format("Anim Graph - {}{}{}", FbxPath::ToString(mPath.stem()), mDirty ? " *" : "", WindowId)};
    bool Visible{true};

    ImGui::SetNextWindowSize({1180.0f, 680.0f}, ImGuiCond_FirstUseEver);

    const bool Expanded{ImGui::Begin(Title.c_str(), &Visible)};

    if (!Visible) {
        ImGui::End();
        Close();

        return;
    }

    const bool SaveRequested{IsFocused() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)};

    if (Expanded && !mError.empty()) {
        ImGui::TextColored({1.0f, 0.45f, 0.4f, 1.0f}, "%s", mError.c_str());
    }

    if (Expanded && ImGui::BeginTable("AnimGraphEditorLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Variables", ImGuiTableColumnFlags_WidthFixed, 230.0f);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);

        if (ImGui::BeginChild("Variables")) {
            DrawVariables(SaveRequested);
        }

        ImGui::EndChild();
        ImGui::TableSetColumnIndex(1);
        DrawNavigation();

        if (mView.mMode == EGraphView::Machine) {
            DrawStateMachine();
        } else if (mView.mMode == EGraphView::Rule) {
            DrawTransitionRule();
        } else {
            DrawGraph();
        }

        ImGui::EndTable();
    }

    if (SaveRequested && mDirty) {
        Save();
    }

    ImGui::End();
}

void FAnimGraphEditor::DrawVariables(bool SaveRequested) {
    const ImGuiStyle& Style{ImGui::GetStyle()};
    const float TextHeight{ImGui::GetTextLineHeight()};
    const float AddWidth{ImGui::CalcTextSize("+ Add").x + 2.0f * Style.FramePadding.x};

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, TextHeight * 0.3f);
    ImGui::TextUnformatted("Variables");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu", mRootEditing->GetVariables().size());

    ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - AddWidth - Style.ItemSpacing.x));
    const bool AddFromName{ImGui::InputTextWithHint("##NewVariable", "New variable name", mNewVariable.data(), mNewVariable.size(), ImGuiInputTextFlags_EnterReturnsTrue)};
    ImGui::SameLine();

    bool Added{};

    if (ImGui::Button("+ Add", {AddWidth, 0.0f}) || AddFromName) {
        Added = mRootEditing->AddVariable({mNewVariable.data(), 0.0f, EAnimGraphVariableSource::Manual});

        if (Added) {
            mNewVariable.fill('\0');
            mDirty = true;
            mError.clear();
        } else {
            mError = "Variable names must be nonempty and unique.";
        }
    }

    ImGui::Spacing();

    int32 Remove{-1};

    if (ImGui::BeginChild("VariableList", {}, ImGuiChildFlags_NavFlattened)) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{TextHeight * 0.45f, TextHeight * 0.35f});
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, TextHeight * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.11f, 0.14f, 0.18f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Border, {0.29f, 0.35f, 0.43f, 0.55f});

        for (std::size_t Index{}; Index < mRootEditing->GetVariables().size(); ++Index) {
            if (DrawVariable(Index, SaveRequested)) {
                Remove = static_cast<int32>(Index);
            }
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (Added) {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (Remove >= 0) {
        mRootEditing->RemoveVariable(Remove);
        mDirty = true;
    }
}

bool FAnimGraphEditor::DrawVariable(std::size_t Index, bool SaveRequested) {
    ImGui::PushID(static_cast<int>(Index));
    const bool Visible{ImGui::BeginChild("Variable", {}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)};
    bool Remove{};

    if (Visible) {
        const ImGuiStyle& Style{ImGui::GetStyle()};
        const float Width{std::max(1.0f, ImGui::GetContentRegionAvail().x)};
        const float FrameHeight{ImGui::GetFrameHeight()};
        const float GetWidth{ImGui::CalcTextSize("Get").x + 2.0f * Style.FramePadding.x};
        const float NameWidth{std::max(1.0f, Width - FrameHeight - GetWidth - 2.0f * Style.ItemSpacing.x)};
        FAnimGraphVariable Variable{mRootEditing->GetVariables()[Index]};
        std::array<char, 64> Name{};
        bool Changed{};

        std::copy_n(Variable.mName.begin(), std::min(Variable.mName.size(), Name.size() - 1), Name.begin());
        ImGui::SetNextItemWidth(NameWidth);
        const bool Rename{ImGui::InputText("##Name", Name.data(), Name.size(), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()};

        if ((Rename || (SaveRequested && ImGui::IsItemActive())) && Variable.mName != Name.data()) {
            Variable.mName = Name.data();
            Changed = true;
        }

        if (ImGui::CalcTextSize(Variable.mName.c_str()).x + 2.0f * Style.FramePadding.x > NameWidth) {
            ImGui::SetItemTooltip("%s", Variable.mName.c_str());
        }

        ImGui::SameLine();
        ImGui::Button("Get", {GetWidth, FrameHeight});

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(VariablePayload, Variable.mName.c_str(), Variable.mName.size() + 1);
            ImGui::Text("Get %s", Variable.mName.c_str());
            ImGui::EndDragDropSource();
        } else {
            ImGui::SetItemTooltip("Drag onto the canvas to add Get %s", Variable.mName.c_str());
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.0f, 0.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.55f, 0.22f, 0.25f, 0.85f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.68f, 0.25f, 0.28f, 1.0f});
        Remove = ImGui::Button("×##Remove", {FrameHeight, FrameHeight});
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip("Delete variable");

        const char* Sources[]{"Manual", "Owner Speed"};
        const float MinSourceWidth{ImGui::CalcTextSize(Sources[1]).x + FrameHeight + 2.0f * Style.FramePadding.x};
        const float MinValueWidth{ImGui::CalcTextSize("-00.000").x + 2.0f * Style.FramePadding.x};
        const float FieldWidth{Width - Style.ItemSpacing.x};
        const bool SideBySide{FieldWidth >= MinSourceWidth + MinValueWidth};
        const float SourceWidth{SideBySide ? std::clamp(FieldWidth * 0.6f, MinSourceWidth, FieldWidth - MinValueWidth) : Width};
        int Source{static_cast<int>(Variable.mSource)};

        ImGui::TextDisabled("Source");

        if (SideBySide) {
            ImGui::SameLine(0.0f, SourceWidth + Style.ItemSpacing.x - ImGui::GetItemRectSize().x);
            ImGui::TextDisabled("Default");
        }

        ImGui::SetNextItemWidth(SourceWidth);

        if (ImGui::Combo("##Source", &Source, Sources, 2)) {
            Variable.mSource = static_cast<EAnimGraphVariableSource>(Source);
            Changed = true;
        }

        if (SideBySide) {
            ImGui::SameLine();
        } else {
            ImGui::TextDisabled("Default");
        }

        ImGui::SetNextItemWidth(-1.0f);

        if (Variable.mType == EAnimGraphVariableType::Bool) {
            Changed |= ImGui::Checkbox("##Default", &Variable.mBoolDefault);
            Variable.mSource = EAnimGraphVariableSource::Manual;
        } else {
            Changed |= ImGui::DragFloat("##Default", &Variable.mDefault, 0.01f, 0.0f, 0.0f, "%.3f");
        }

        int Type{static_cast<int>(Variable.mType)};
        const char* Types[]{"Float", "Bool"};

        if (ImGui::Combo("Type", &Type, Types, 2)) {
            Variable.mType = static_cast<EAnimGraphVariableType>(Type);
            Changed = true;
        }

        if (Changed) {
            if (mRootEditing->SetVariable(static_cast<int32>(Index), Variable)) {
                mDirty = true;
                mError.clear();
            } else {
                mError = "Variable names must be nonempty and unique.";
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopID();

    return Remove;
}

int32 FAnimGraphEditor::FindVariableIndex(const FString& Name) const {
    const auto& Variables{mEditing->GetVariables()};
    const auto Found{std::find_if(Variables.begin(), Variables.end(), [&Name](const FAnimGraphVariable& Variable) {
        return Variable.mName == Name;
    })};

    return Found == Variables.end() ? -1 : static_cast<int32>(Found - Variables.begin());
}

void FAnimGraphEditor::DrawValueGetters() {
    // 변수 정의와 Get 노드를 분리해 같은 변수도 캔버스에 여러 번 배치할 수 있게 한다.
    std::vector<int32> Bound{};

    for (const FAnimGraphNode& Node : mEditing->GetNodes()) {
        const FAnimGraphNodeType* Type{FAnimGraphNodeRegistry::Find(Node.mType)};

        if (Type == nullptr) {
            continue;
        }

        for (const FAnimGraphParameterInfo& Info : Type->mParameters) {
            if (!IsBindableFloat(Info)) {
                continue;
            }

            const FString* Binding{Node.FindParameter(Info.mName + ".Binding")};

            if (Binding == nullptr) {
                continue;
            }

            if (const FAnimGraphVariableGetter* Getter{mEditing->FindBoundGetter(Node, Info.mName)}; Getter != nullptr) {
                Bound.push_back(Getter->mId);
            } else if (const int32 Id{mEditing->AddVariableGetter(*Binding, Node.mX - 245.0f, Node.mY - 170.0f)}; Id >= 0) {
                ed::SetNodePosition(ed::NodeId{GetterKey(Id)}, {Node.mX - 245.0f, Node.mY - 170.0f});
                Bound.push_back(Id);
            }
        }
    }

    for (const FAnimGraphVariableGetter& Getter : mEditing->GetVariableGetters()) {
        const int32 Index{FindVariableIndex(Getter.mVariable)};

        if (Index < 0) {
            continue;
        }

        const std::uintptr_t Key{GetterKey(Getter.mId)};

        if (mApplyPositions) {
            ed::SetNodePosition(ed::NodeId{Key}, {Getter.mX, Getter.mY});
        }

        const FAnimGraphVariable& Variable{mEditing->GetVariables()[static_cast<std::size_t>(Index)]};
        const FString Value{FormatFloat(Variable.mDefault)};
        const bool Connected{std::find(Bound.begin(), Bound.end(), Getter.mId) != Bound.end()};

        FAnimGraphNodeBuilder::DrawFloatGetter(ed::NodeId{Key}, ed::PinId{Key | OutputPinBits}, Variable.mName.c_str(), Value.c_str(), Connected);
    }
}

void FAnimGraphEditor::DrawValueLinks() {
    const ImVec4 Color{FAnimGraphNodeBuilder::FloatColor()};

    for (const FAnimGraphNode& Node : mEditing->GetNodes()) {
        const FAnimGraphNodeType* Type{FAnimGraphNodeRegistry::Find(Node.mType)};

        if (Type == nullptr) {
            continue;
        }

        for (std::size_t Index{}; Index < Type->mParameters.size(); ++Index) {
            const FAnimGraphParameterInfo& Info{Type->mParameters[Index]};
            const FAnimGraphVariableGetter* Getter{IsBindableFloat(Info) ? mEditing->FindBoundGetter(Node, Info.mName) : nullptr};

            if (Getter != nullptr) {
                const std::uintptr_t Input{FloatInputKey(Node.mId, Index)};
                const std::uintptr_t Output{GetterKey(Getter->mId) | OutputPinBits};

                ed::Link(ed::LinkId{Input}, ed::PinId{Output}, ed::PinId{Input}, Color, 2.0f);
            }
        }
    }
}

void FAnimGraphEditor::DrawGraph() {
    const ImVec2 CanvasOrigin{ImGui::GetCursorScreenPos()};
    const ImVec2 CanvasSize{ImGui::GetContentRegionAvail()};
    ed::SetCurrentEditor(mEditor);
    FAnimGraphNodeBuilder::ApplyStyle();
    ed::Begin("AnimGraphCanvas", CanvasSize);

    if (mApplyPositions) {
        ed::SetNodePosition(ed::NodeId{OutputNode}, {mEditing->GetOutputX(), mEditing->GetOutputY()});

        for (const FAnimGraphNode& Node : mEditing->GetNodes()) {
            ed::SetNodePosition(ed::NodeId{NodeKey(Node.mId)}, {Node.mX, Node.mY});
        }
    }

    const ImVec4 PoseColor{FAnimGraphNodeBuilder::PoseColor()};
    const ImVec4 FloatColor{FAnimGraphNodeBuilder::FloatColor()};
    const auto HasOutput{[this](int32 Id) {
        return std::any_of(mEditing->GetLinks().begin(), mEditing->GetLinks().end(), [Id](const FAnimGraphLink& Link) {
            return Link.mFromNode == Id;
        });
    }};
    FAnimGraphNodeBuilder OutputBuilder{};

    OutputBuilder.FitHeader("Output Pose", "Output Pose");
    OutputBuilder.FitPins("Result");
    OutputBuilder.Begin(ed::NodeId{OutputNode}, 160.0f);
    OutputBuilder.Header("Output Pose", "Output Pose");
    OutputBuilder.InputPin(ed::PinId{OutputInputPin}, "Result", mEditing->FindInput(UAnimGraph::OutputNodeId, 0) != UAnimGraph::OutputNodeId, PoseColor);
    OutputBuilder.End();

    DrawValueGetters();
    for (const FAnimGraphNode& Node : mEditing->GetNodes()) {
        const FAnimGraphNodeType* Type{FAnimGraphNodeRegistry::Find(Node.mType)};
        const char* TypeTitle{Type != nullptr ? Type->mTitle.c_str() : Node.mType.c_str()};
        const char* Title{Node.mName.empty() ? TypeTitle : Node.mName.c_str()};
        const std::size_t PinCount{Type != nullptr ? Type->mPoseInputs.size() : 0};
        const FString UnknownType{Type == nullptr ? "Unknown node type: " + Node.mType : FString{}};
        std::vector<FString> Parameters{};
        FAnimGraphNodeBuilder Builder{};

        Builder.FitHeader(Title, TypeTitle);
        Builder.FitPins(PinCount > 0 ? Type->mPoseInputs.front().c_str() : nullptr, "Pose");

        for (std::size_t Pin{1}; Pin < PinCount; ++Pin) {
            Builder.FitPins(Type->mPoseInputs[Pin].c_str());
        }

        if (Type == nullptr) {
            Builder.FitText(UnknownType.c_str());
        } else {
            Parameters.reserve(Type->mParameters.size());

            for (const FAnimGraphParameterInfo& Info : Type->mParameters) {
                FString Value{DisplayParameterValue(Info, Node)};

                if (IsBindableFloat(Info)) {
                    const FString* Binding{Node.FindParameter(Info.mName + ".Binding")};

                    Value = Binding != nullptr ? Info.mName : Info.mName + "  " + Value;
                    Builder.FitPins(Value.c_str());
                } else {
                    Builder.FitProperty(Info.mName.c_str(), Value.c_str());
                }

                Parameters.push_back(std::move(Value));
            }
        }

        Builder.Begin(ed::NodeId{NodeKey(Node.mId)}, 210.0f);
        Builder.Header(Title, TypeTitle);

        for (std::size_t Pin{}; Pin < PinCount; ++Pin) {
            const bool Connected{mEditing->FindInput(Node.mId, static_cast<int32>(Pin)) != UAnimGraph::OutputNodeId};

            Builder.InputPin(ed::PinId{InputPinKey(Node.mId, static_cast<int32>(Pin))}, Type->mPoseInputs[Pin].c_str(), Connected, PoseColor);

            if (Pin == 0) {
                Builder.OutputPin(ed::PinId{OutputPinKey(Node.mId)}, "Pose", HasOutput(Node.mId), PoseColor, true);
            }
        }

        if (PinCount == 0) {
            Builder.OutputPin(ed::PinId{OutputPinKey(Node.mId)}, "Pose", HasOutput(Node.mId), PoseColor, false);
        }

        if (Type == nullptr) {
            Builder.BeginBody();
            Builder.DrawText(UnknownType.c_str(), {1.0f, 0.45f, 0.4f, 1.0f});
        } else {
            for (std::size_t Index{}; Index < Parameters.size(); ++Index) {
                const FAnimGraphParameterInfo& Info{Type->mParameters[Index]};

                if (IsBindableFloat(Info)) {
                    const FString* Binding{Node.FindParameter(Info.mName + ".Binding")};
                    const bool Connected{Binding != nullptr && FindVariableIndex(*Binding) >= 0};

                    Builder.InputPin(ed::PinId{FloatInputKey(Node.mId, Index)}, Parameters[Index].c_str(), Connected, FloatColor);
                }
            }

            if (std::any_of(Type->mParameters.begin(), Type->mParameters.end(), [](const FAnimGraphParameterInfo& Info) {
                return !IsBindableFloat(Info);
            })) {
                Builder.BeginBody();
            }

            for (std::size_t Index{}; Index < Parameters.size(); ++Index) {
                if (const FAnimGraphParameterInfo& Info{Type->mParameters[Index]}; !IsBindableFloat(Info)) {
                    Builder.DrawProperty(Info.mName.c_str(), Parameters[Index].c_str());
                }
            }
        }

        Builder.End();
    }

    for (const FAnimGraphLink& Link : mEditing->GetLinks()) {
        const std::uintptr_t Input{InputPinKey(Link.mToNode, Link.mToPin)};

        ed::Link(ed::LinkId{Input}, ed::PinId{OutputPinKey(Link.mFromNode)}, ed::PinId{Input}, PoseColor, 2.0f);
    }

    DrawValueLinks();

    if (mFitRequested && !mApplyPositions) {
        ed::NavigateToContent(0.0f);
        mFitRequested = false;
    }
    if (ed::BeginCreate({0.98f, 0.72f, 0.2f, 1.0f}, 2.0f)) {
        ed::PinId Start{};
        ed::PinId End{};

        if (ed::QueryNewLink(&Start, &End) && Start && End) {
            std::uintptr_t From{Start.Get()};
            std::uintptr_t To{End.Get()};

            if ((From & OutputPinBits) != OutputPinBits) {
                std::swap(From, To);
            }

            // 값 연결은 변수 바인딩으로, 포즈 연결은 그래프 링크로 각각 저장한다.
            if (((From | To) & (GetterBit | FloatInputBit)) != 0) {
                const FAnimGraphVariableGetter* Getter{mEditing->FindVariableGetter(DecodeGetter(From))};
                const FPinRef Input{(To & FloatInputBit) != 0 ? DecodePin(To & ~FloatInputBit) : FPinRef{-2, -1, false}};
                FAnimGraphNode* Target{mEditing->FindNode(Input.mNode)};
                const FAnimGraphParameterInfo* Info{FindFloatInput(Target, Input.mPin)};

                if (Getter == nullptr || Info == nullptr) {
                    ed::RejectNewItem({1.0f, 0.3f, 0.3f, 1.0f}, 2.0f);
                } else if (ed::AcceptNewItem(FloatColor, 3.0f)) {
                    if (mEditing->FindBoundGetter(*Target, Info->mName) != Getter) {
                        Target->SetParameter(Info->mName + ".Binding", Getter->mVariable, Getter->mId);
                        mDirty = true;
                    }
                }
            } else {
                const FPinRef A{DecodePin(From)};
                const FPinRef B{DecodePin(To)};

                if (!A.mOutput || B.mOutput || (B.mNode != UAnimGraph::OutputNodeId && mEditing->WouldCreateCycle(A.mNode, B.mNode))) {
                    ed::RejectNewItem({1.0f, 0.3f, 0.3f, 1.0f}, 2.0f);
                } else if (ed::AcceptNewItem({0.4f, 0.9f, 0.5f, 1.0f}, 3.0f) && mEditing->Connect(A.mNode, B.mNode, B.mPin)) {
                    mDirty = true;
                }
            }
        }
    }

    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId Link{};
        ed::NodeId Node{};

        while (ed::QueryDeletedLink(&Link)) {
            if (ed::AcceptDeletedItem()) {
                const bool FloatLink{(Link.Get() & FloatInputBit) != 0};
                const FPinRef Input{DecodePin(Link.Get() & ~FloatInputBit)};

                if (FloatLink) {
                    FAnimGraphNode* Target{mEditing->FindNode(Input.mNode)};

                    if (const FAnimGraphParameterInfo* Info{FindFloatInput(Target, Input.mPin)}; Info != nullptr) {
                        const FString BindingKey{Info->mName + ".Binding"};

                        if (Target->FindParameter(BindingKey) != nullptr) {
                            Target->SetParameter(BindingKey, {});
                            mDirty = true;
                        }
                    }
                } else {
                    mDirty |= mEditing->Disconnect(Input.mNode, Input.mPin);
                }
            }
        }

        while (ed::QueryDeletedNode(&Node)) {
            const int32 Id{DecodeNode(Node.Get())};
            if (Id == UAnimGraph::OutputNodeId) {
                ed::RejectDeletedItem();
            } else if (ed::AcceptDeletedItem()) {
                if ((Node.Get() & GetterBit) != 0) {
                    mDirty |= mEditing->RemoveVariableGetter(DecodeGetter(Node.Get()));
                } else {
                    mDirty |= mEditing->RemoveNode(Id);
                }

                if (mSelectedNode == Id) {
                    mSelectedNode = -2;
                }
            }
        }
    }

    ed::EndDelete();
    const ImVec2 CanvasMouse{ImGui::GetMousePos()};
    ed::NodeId ContextNode{};
    int32 OpenMachineId{-1};

    ed::Suspend();

    const ImRect DropArea{CanvasOrigin, {CanvasOrigin.x + CanvasSize.x, CanvasOrigin.y + CanvasSize.y}};

    // 변수 목록에서 놓은 위치에 독립적인 Get 노드를 생성한다.
    if (ImGui::BeginDragDropTargetCustom(DropArea, ImGui::GetID("AnimGraphVariableDrop"))) {
        if (const ImGuiPayload* Payload{ImGui::AcceptDragDropPayload(VariablePayload)}; Payload != nullptr && Payload->Data != nullptr && Payload->DataSize > 1 && static_cast<const char*>(Payload->Data)[Payload->DataSize - 1] == '\0') {
            const FString Name{static_cast<const char*>(Payload->Data), static_cast<std::size_t>(Payload->DataSize - 1)};

            if (const int32 Id{mEditing->AddVariableGetter(Name, CanvasMouse.x, CanvasMouse.y)}; Id >= 0) {
                ed::SetNodePosition(ed::NodeId{GetterKey(Id)}, CanvasMouse);
                mDirty = true;
            } else {
                mError = "Cannot add a variable getter.";
            }
        }

        ImGui::EndDragDropTarget();
    }

    if (ed::ShowNodeContextMenu(&ContextNode)) {
        mContextNode = ContextNode.Get();
        mSelectedNode = DecodeNode(mContextNode);
        ed::SelectNode(ContextNode);
        ImGui::OpenPopup("AnimGraphNodeMenu");
    } else if (ed::ShowBackgroundContextMenu()) {
        mCreateX = CanvasMouse.x;
        mCreateY = CanvasMouse.y;
        ImGui::OpenPopup("AnimGraphCreateNode");
    }

    if (ImGui::BeginPopup("AnimGraphCreateNode")) {
        ImGui::TextDisabled("Add Node");
        ImGui::Separator();

        for (const FAnimGraphNodeType& Type : FAnimGraphNodeRegistry::GetNodeTypes()) {
            if (ImGui::MenuItem(Type.mTitle.c_str())) {
                const int32 Id{mEditing->AddNode(Type.mType, Type.mTitle, mCreateX, mCreateY)};

                if (Id >= 0) {
                    ed::SetNodePosition(ed::NodeId{NodeKey(Id)}, {mCreateX, mCreateY});
                    mSelectedNode = Id;
                    mDirty = true;
                } else {
                    mError = "The graph node limit has been reached.";
                }
            }
        }

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints({360.0f, 0.0f}, {500.0f, ImGui::GetIO().DisplaySize.y});

    if (ImGui::BeginPopup("AnimGraphNodeMenu")) {
        const int32 Id{DecodeNode(mContextNode)};
        const int32 Getter{DecodeGetter(mContextNode)};
        const bool CanDelete{mEditing->FindVariableGetter(Getter) != nullptr || mEditing->FindNode(Id) != nullptr};
        mSelectedNode = Id;
        ImGui::PushItemWidth(220.0f);

        if (const auto* Variable{mEditing->FindVariableGetter(Getter)}) {
            ImGui::SeparatorText("Variable Getter");
            ImGui::Text("Variable: %s", Variable->mVariable.c_str());
        } else if (Id == UAnimGraph::OutputNodeId) {
            ImGui::SeparatorText("Output Pose");
        } else {
            DrawSelection();
        }

        ImGui::PopItemWidth();
        ImGui::Separator();

        if (const auto* Node{mEditing->FindNode(Id)}; Node != nullptr && Node->mType == "StateMachine" && ImGui::MenuItem("Open State Machine")) {
            OpenMachineId = Id;
        }

        if (ImGui::MenuItem("Delete", "Delete", false, CanDelete)) {
            mDirty |= Getter >= 0 ? mEditing->RemoveVariableGetter(Getter) : mEditing->RemoveNode(Id);
            mSelectedNode = -2;
        }

        if (ImGui::MenuItem("Disconnect Inputs", nullptr, false, Id != -2)) {
            for (int32 Pin{}; Pin < 255; ++Pin) {
                mDirty |= mEditing->Disconnect(Id, Pin);
            }

            if (FAnimGraphNode* Target{mEditing->FindNode(Id)}; Target != nullptr) {
                const auto Removed{std::erase_if(Target->mParameters, [](const FAnimGraphParameter& Parameter) {
                    return Parameter.mName.ends_with(".Binding");
                })};

                mDirty |= Removed > 0;
            }
        }

        ImGui::EndPopup();
    }

    ed::Resume();

    std::array<ed::NodeId, 2> Selected{};
    const int SelectedCount{ed::GetSelectedNodes(Selected.data(), static_cast<int>(Selected.size()))};

    if (SelectedCount == 1) {
        mSelectedNode = DecodeNode(Selected[0].Get());
    } else if (SelectedCount == 0 && !ImGui::IsPopupOpen("AnimGraphNodeMenu")) {
        mSelectedNode = -2;
    }
    // 최초 복원 프레임 이후에만 캔버스 좌표를 에셋으로 되써 초기 배치가 덮이지 않게 한다.
    if (!mApplyPositions) {
        const ImVec2 Output{ed::GetNodePosition(ed::NodeId{OutputNode})};

        if (std::abs(Output.x - mEditing->GetOutputX()) > 0.5f || std::abs(Output.y - mEditing->GetOutputY()) > 0.5f) {
            mEditing->SetNodePosition(UAnimGraph::OutputNodeId, Output.x, Output.y);
            mDirty = true;
        }

        for (const FAnimGraphNode& Node : mEditing->GetNodes()) {
            const ImVec2 Position{ed::GetNodePosition(ed::NodeId{NodeKey(Node.mId)})};

            if (std::isfinite(Position.x) && (std::abs(Position.x - Node.mX) > 0.5f || std::abs(Position.y - Node.mY) > 0.5f)) {
                mEditing->SetNodePosition(Node.mId, Position.x, Position.y);
                mDirty = true;
            }
        }

        for (const FAnimGraphVariableGetter& Getter : mEditing->GetVariableGetters()) {
            const ImVec2 Position{ed::GetNodePosition(ed::NodeId{GetterKey(Getter.mId)})};

            if (std::abs(Position.x - Getter.mX) > 0.5f || std::abs(Position.y - Getter.mY) > 0.5f) {
                mDirty |= mEditing->SetVariableGetterPosition(Getter.mId, Position.x, Position.y);
            }
        }
    } else {
        mApplyPositions = false;
        mFitRequested = true;
    }

    const std::uintptr_t DoubleClickedKey{ed::GetDoubleClickedNode().Get()};
    const int32 DoubleClicked{DecodeNode(DoubleClickedKey)};
    const auto* OpenNode{mEditing->FindNode(DoubleClicked)};
    const bool OpenMachine{(DoubleClickedKey & GetterBit) == 0 && OpenNode != nullptr && OpenNode->mType == "StateMachine"};
    ed::End();
    ed::SetCurrentEditor(nullptr);

    // Node Editor의 현재 캔버스를 닫은 뒤 컨텍스트를 바꿔 중첩 그래프로 이동한다.
    if (OpenMachineId >= 0 || OpenMachine) {
        OpenStateMachine(OpenMachineId >= 0 ? OpenMachineId : DoubleClicked);
    }
}

void FAnimGraphEditor::DrawSelection() {
    ImGui::SeparatorText("Node Properties");

    FAnimGraphNode* Node{mEditing->FindNode(mSelectedNode)};

    if (Node == nullptr) {
        return;
    }

    const FAnimGraphNodeType* Type{FAnimGraphNodeRegistry::Find(Node->mType)};

    ImGui::PushID(Node->mId);
    ImGui::TextDisabled("%s", Type != nullptr ? Type->mTitle.c_str() : Node->mType.c_str());

    std::array<char, 64> Name{};

    std::copy_n(Node->mName.begin(), std::min(Node->mName.size(), Name.size() - 1), Name.begin());

    if (ImGui::InputText("Name", Name.data(), Name.size())) {
        Node->mName = Name.data();
        mDirty = true;
    }

    if (Type == nullptr) {
        ImGui::PopID();

        return;
    }

    if (!Type->mParameters.empty()) {
        ImGui::SeparatorText("Parameters");
    }

    for (const FAnimGraphParameterInfo& Info : Type->mParameters) {
        const FString* Stored{Node->FindParameter(Info.mName)};
        FString Value{Stored != nullptr ? *Stored : Info.mDefault};

        ImGui::PushID(Info.mName.c_str());

        switch (Info.mType) {
            case EAnimGraphParameterType::Float: {
                const FString BindingKey{Info.mName + ".Binding"};
                const FString* Binding{Node->FindParameter(BindingKey)};
                float Number{};

                std::from_chars(Value.data(), Value.data() + Value.size(), Number);

                if (Info.mBindable) {
                    ImGui::SetNextItemWidth(110.0f);

                    if (ImGui::BeginCombo("##Binding", Binding != nullptr ? Binding->c_str() : "Constant")) {
                        const FString SelectedBinding{Binding != nullptr ? *Binding : FString{}};

                        if (ImGui::Selectable("Constant", Binding == nullptr)) {
                            Node->SetParameter(BindingKey, {});
                            mDirty = true;
                        }

                        for (const FAnimGraphVariable& Variable : mEditing->GetVariables()) {
                            if (Variable.mType == EAnimGraphVariableType::Float && ImGui::Selectable(Variable.mName.c_str(), SelectedBinding == Variable.mName) && SelectedBinding != Variable.mName) {
                                Node->SetParameter(BindingKey, Variable.mName);
                                mDirty = true;
                            }
                        }

                        ImGui::EndCombo();
                    }

                    Binding = Node->FindParameter(BindingKey);
                    ImGui::SameLine();
                }

                ImGui::BeginDisabled(Binding != nullptr);
                ImGui::SetNextItemWidth(-ImGui::CalcTextSize(Info.mName.c_str()).x - ImGui::GetStyle().ItemInnerSpacing.x);

                const bool Edited{Info.mMin < Info.mMax ? ImGui::SliderFloat(Info.mName.c_str(), &Number, Info.mMin, Info.mMax, "%.3f") : ImGui::DragFloat(Info.mName.c_str(), &Number, 0.01f, 0.0f, 0.0f, "%.3f")};

                if (Edited && std::isfinite(Number)) {
                    Node->SetParameter(Info.mName, FormatFloat(Number));
                    mDirty = true;
                }

                ImGui::EndDisabled();

                break;
            }

            case EAnimGraphParameterType::Bool: {
                bool Flag{Value == "true"};

                if (ImGui::Checkbox(Info.mName.c_str(), &Flag)) {
                    Node->SetParameter(Info.mName, Flag ? "true" : "false");
                    mDirty = true;
                }

                break;
            }

            case EAnimGraphParameterType::AnimSequence:
            case EAnimGraphParameterType::BlendSpace: {
                const EAssetType AssetType{Info.mType == EAnimGraphParameterType::AnimSequence ? EAssetType::AnimSequence : EAssetType::BlendSpace};

                if (DrawAnimationAssetPicker(Info.mName.c_str(), Value, AssetType)) {
                    Node->SetParameter(Info.mName, Value);
                    mDirty = true;
                }

                break;
            }
        }

        ImGui::PopID();
    }

    ImGui::PopID();
}
