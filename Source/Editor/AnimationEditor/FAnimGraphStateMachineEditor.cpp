#include "EnginePCH.h"
#include "FAnimGraphEditor.h"
#include "FAnimGraphNodeBuilder.h"
#include "Animation/AnimGraph.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace ed = ax::NodeEditor;

namespace {
    constexpr ImVec4 BoolColor{0.82f, 0.24f, 0.28f, 1.0f};
    constexpr ImVec4 StateColor{0.45f, 0.72f, 1.0f, 1.0f};

    std::uintptr_t StateNodeKey(int32 Id) {
        return Id == FAnimStateMachineData::AnyStateId ? 16 : (static_cast<std::uintptr_t>(Id) + 2) * 16;
    }

    int32 StateNodeId(std::uintptr_t Key) {
        return Key >= 32 ? static_cast<int32>(Key / 16 - 2) : Key >= 16 ? FAnimStateMachineData::AnyStateId : -3;
    }

    bool EditString(const char* Label, FString& Value) {
        std::array<char, 256> Buffer{};
        std::copy_n(Value.begin(), std::min(Value.size(), Buffer.size() - 1), Buffer.begin());

        if (ImGui::InputText(Label, Buffer.data(), Buffer.size())) {
            Value = Buffer.data();

            return true;
        }

        return false;
    }
}

void FAnimGraphEditor::ClearGraphContexts() {
    for (const auto& [Key, Context] : mGraphContexts) {
        ed::DestroyEditor(Context);
    }

    mGraphContexts.clear();
    mEditor = nullptr;
}

void FAnimGraphEditor::ActivateView(const FGraphView& View) {
    // 기존 캔버스로 돌아올 때는 저장된 UI 상태를 재사용하고 새 뷰에만 에셋 좌표를 적용한다.
    mView = View;
    mEditing = View.mGraph;
    mSelectedNode = View.mSelection;
    mContextNode = 0;
    const FString Key{View.mKey};
    auto Found{mGraphContexts.find(Key)};
    const bool Created{Found == mGraphContexts.end()};

    if (Created) {
        ed::Config Config{};
        Config.SettingsFile = nullptr;
        Found = mGraphContexts.emplace(Key, ed::CreateEditor(&Config)).first;
    }

    mEditor = Found->second;
    mApplyPositions = Created;
    mFitRequested = Created;
}

void FAnimGraphEditor::Navigate(UAnimGraph* Graph, EGraphView Mode, int32 Machine, int32 Transition, const FString& Label) {
    // 현재 선택을 경로에 남겨 상태 머신, 상태 포즈, 전환 규칙 사이를 되돌아갈 수 있게 한다.
    mView.mSelection = mSelectedNode;
    mNavigation.push_back(mView);
    FGraphView View{};
    View.mGraph = Graph;
    View.mMode = Mode;
    View.mMachine = Machine;
    View.mTransition = Transition;
    View.mLabel = Label;
    View.mKey = std::format("{}/{}:{}:{}", mView.mKey, static_cast<int>(Mode), Machine, Transition);
    ActivateView(View);
}

void FAnimGraphEditor::DrawNavigation() {
    int32 Destination{-1};
    const float Height{ImGui::GetFrameHeight() + ImGui::GetStyle().ScrollbarSize + 4.0f};

    if (ImGui::BeginChild("GraphNavigation", {0.0f, Height}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (std::size_t Index{}; Index <= mNavigation.size(); ++Index) {
            const bool Current{Index == mNavigation.size()};
            const FGraphView& View{Current ? mView : mNavigation[Index]};
            const FString Label{GetViewLabel(View, Index > 0 ? &mNavigation[Index - 1] : nullptr)};

            if (Index > 0) {
                ImGui::SameLine();
                ImGui::TextDisabled(">");
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(Index));
            ImGui::BeginDisabled(Current);

            if (ImGui::Button(Label.c_str())) {
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

FString FAnimGraphEditor::GetViewLabel(const FGraphView& View, const FGraphView* Parent) const {
    if (View.mGraph != nullptr && View.mMode == EGraphView::Machine) {
        if (const auto* Node{View.mGraph->FindNode(View.mMachine)}) {
            return Node->mName.empty() ? FString{"State Machine"} : Node->mName;
        }
    } else if (View.mGraph != nullptr && View.mMode == EGraphView::Rule) {
        const auto* Machine{View.mGraph->FindStateMachine(View.mMachine)};
        const auto* Transition{Machine != nullptr ? Machine->FindTransition(View.mTransition) : nullptr};
        const auto* From{Transition != nullptr ? Machine->FindState(Transition->mFromState) : nullptr};
        const auto* To{Transition != nullptr ? Machine->FindState(Transition->mToState) : nullptr};

        if (Transition != nullptr && To != nullptr && (From != nullptr || Transition->mFromState == FAnimStateMachineData::AnyStateId)) {
            return std::format("{} -> {}", From != nullptr ? From->mName : "Any State", To->mName);
        }
    } else if (Parent != nullptr && Parent->mGraph != nullptr && Parent->mMode == EGraphView::Machine) {
        const auto* Machine{Parent->mGraph->FindStateMachine(Parent->mMachine)};

        if (const auto* State{Machine != nullptr ? Machine->FindState(View.mMachine) : nullptr}) {
            return State->mName;
        }
    }

    return View.mLabel;
}

bool FAnimGraphEditor::OpenStateMachine(int32 NodeId) {
    if (mEditing == nullptr || mView.mMode != EGraphView::Pose || mEditing->FindStateMachine(NodeId) == nullptr) {
        return false;
    }

    Navigate(mEditing, EGraphView::Machine, NodeId, -1, mEditing->FindNode(NodeId)->mName);

    return true;
}

bool FAnimGraphEditor::OpenState(int32 StateId) {
    auto* Machine{mEditing != nullptr && mView.mMode == EGraphView::Machine ? mEditing->FindStateMachine(mView.mMachine) : nullptr};
    auto* State{Machine != nullptr ? Machine->FindState(StateId) : nullptr};

    if (State == nullptr || State->mGraph == nullptr) {
        return false;
    }

    Navigate(State->mGraph.get(), EGraphView::Pose, StateId, -1, State->mName);

    return true;
}

bool FAnimGraphEditor::OpenTransitionRule(int32 TransitionId) {
    auto* Machine{mEditing != nullptr && mView.mMode == EGraphView::Machine ? mEditing->FindStateMachine(mView.mMachine) : nullptr};

    if (Machine == nullptr || Machine->FindTransition(TransitionId) == nullptr) {
        return false;
    }

    Navigate(mEditing, EGraphView::Rule, mView.mMachine, TransitionId, "Transition Rule");

    return true;
}

bool FAnimGraphEditor::NavigateBack() {
    return !mNavigation.empty() && NavigateTo(mNavigation.size() - 1);
}

bool FAnimGraphEditor::NavigateTo(std::size_t Depth) {
    if (Depth >= mNavigation.size()) {
        return false;
    }

    const FGraphView View{mNavigation[Depth]};
    mNavigation.resize(Depth);
    ActivateView(View);

    return true;
}

void FAnimGraphEditor::DrawStateMachine() {
    // Entry는 시작 상태를 지정하고, 상태 사이의 방향 연결은 전환 데이터를 생성한다.
    auto* Machine{mEditing->FindStateMachine(mView.mMachine)};

    if (Machine == nullptr) {
        return;
    }

    const auto AddState{[this, Machine]() {
        uint32 Number{static_cast<uint32>(Machine->GetStates().size())};
        FString Name{};

        do {
            Name = std::format("State {}", Number++);
        } while (std::any_of(Machine->GetStates().begin(), Machine->GetStates().end(), [&Name](const auto& State) {
            return State.mName == Name;
        }));

        const int32 Id{Machine->AddState(Name, mCreateX, mCreateY, *mRootEditing)};

        if (Id >= 0) {
            ed::SetNodePosition(ed::NodeId{StateNodeKey(Id)}, {mCreateX, mCreateY});
            mSelectedNode = Id;
            mDirty = true;
        }
    }};

    ed::SetCurrentEditor(mEditor);

    if (ImGui::Button("+ State")) {
        mCreateX = static_cast<float>(Machine->GetStates().size()) * 220.0f;
        mCreateY = 0.0f;
        AddState();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Double-click a state or transition to open it");
    FAnimGraphNodeBuilder::ApplyStyle();
    ed::Begin("StateMachineCanvas");

    if (mApplyPositions) {
        ed::SetNodePosition(ed::NodeId{1}, {-180.0f, 0.0f});
        ed::SetNodePosition(ed::NodeId{StateNodeKey(FAnimStateMachineData::AnyStateId)}, {Machine->GetAnyStateX(), Machine->GetAnyStateY()});

        for (const auto& State : Machine->GetStates()) {
            ed::SetNodePosition(ed::NodeId{StateNodeKey(State.mId)}, {State.mX, State.mY});
        }
    }

    FAnimGraphNodeBuilder Entry{};
    Entry.FitHeader("Entry", "Entry");
    Entry.FitPins("", "State");
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
        const std::uintptr_t Key{StateNodeKey(State.mId)};
        FAnimGraphNodeBuilder Builder{};
        Builder.FitHeader(State.mName.c_str(), "State");
        Builder.FitPins("In", "Out");
        Builder.Begin(ed::NodeId{Key}, 170.0f);
        Builder.Header(State.mName.c_str(), "State");
        Builder.InputPin(ed::PinId{Key + 1}, "In", true, StateColor);
        Builder.OutputPin(ed::PinId{Key + 2}, "Out", true, StateColor, true);
        Builder.End();
    }

    if (Machine->GetEntryState() >= 0) {
        ed::Link(ed::LinkId{1}, ed::PinId{2}, ed::PinId{StateNodeKey(Machine->GetEntryState()) + 1}, StateColor, 2.5f);
    }

    for (const auto& Transition : Machine->GetTransitions()) {
        ed::Link(ed::LinkId{static_cast<std::uintptr_t>(Transition.mId) + 2}, ed::PinId{StateNodeKey(Transition.mFromState) + 2}, ed::PinId{StateNodeKey(Transition.mToState) + 1}, StateColor, 2.0f);
    }

    if (ed::BeginCreate()) {
        ed::PinId A{};
        ed::PinId B{};

        if (ed::QueryNewLink(&A, &B) && A && B) {
            if (A.Get() % 16 != 2) {
                std::swap(A, B);
            }

            // 핀의 하위 번호로 방향을 확인하고 Entry 연결과 일반 전환을 구분한다.
            const bool Valid{A.Get() % 16 == 2 && B.Get() % 16 == 1 && Machine->FindState(StateNodeId(B.Get())) != nullptr && (A.Get() == 2 || A.Get() == AnyStateKey + 2 || Machine->FindState(StateNodeId(A.Get())) != nullptr)};

            if (!Valid) {
                ed::RejectNewItem(BoolColor, 2.0f);
            } else if (ed::AcceptNewItem(StateColor, 2.0f)) {
                mDirty |= A.Get() == 2 ? Machine->SetEntryState(StateNodeId(B.Get())) : Machine->AddTransition(StateNodeId(A.Get()), StateNodeId(B.Get())) >= 0;
            }
        }
    }

    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::NodeId Node{};
        ed::LinkId Link{};

        while (ed::QueryDeletedNode(&Node)) {
            if (Node.Get() == 1 || Node.Get() == AnyStateKey) {
                ed::RejectDeletedItem();
            } else if (ed::AcceptDeletedItem()) {
                mDirty |= Machine->RemoveState(StateNodeId(Node.Get()));
                mSelectedNode = -2;
            }
        }

        while (ed::QueryDeletedLink(&Link)) {
            if (Link.Get() == 1) {
                ed::RejectDeletedItem();
            } else if (ed::AcceptDeletedItem()) {
                mDirty |= Machine->RemoveTransition(static_cast<int32>(Link.Get() - 2));
                mView.mTransition = -1;
            }
        }
    }

    ed::EndDelete();
    std::array<ed::NodeId, 1> SelectedNodes{};
    std::array<ed::LinkId, 1> SelectedLinks{};

    if (ed::GetSelectedNodes(SelectedNodes.data(), 1) == 1) {
        mSelectedNode = StateNodeId(SelectedNodes[0].Get());
        mView.mTransition = -1;
    } else if (ed::GetSelectedLinks(SelectedLinks.data(), 1) == 1 && SelectedLinks[0].Get() >= 2) {
        mSelectedNode = -2;
        mView.mTransition = static_cast<int32>(SelectedLinks[0].Get() - 2);
    }

    const int32 DoubleClickedState{StateNodeId(ed::GetDoubleClickedNode().Get())};
    const std::uintptr_t OpenLink{ed::GetDoubleClickedLink().Get()};
    const ImVec2 Mouse{ImGui::GetMousePos()};
    ed::NodeId ContextNode{};
    ed::LinkId ContextLink{};
    int32 OpenStateId{-1};
    int32 OpenTransitionId{-1};
    ed::Suspend();

    if (ed::ShowNodeContextMenu(&ContextNode)) {
        mContextNode = ContextNode.Get();
        ed::SelectNode(ContextNode);
        ImGui::OpenPopup("AnimStateMenu");
    } else if (ed::ShowLinkContextMenu(&ContextLink) && ContextLink.Get() >= 2) {
        mContextNode = ContextLink.Get();
        ed::SelectLink(ContextLink);
        ImGui::OpenPopup("AnimTransitionMenu");
    } else if (ed::ShowBackgroundContextMenu()) {
        mCreateX = Mouse.x;
        mCreateY = Mouse.y;
        ImGui::OpenPopup("CreateState");
    }

    if (ImGui::BeginPopup("CreateState")) {
        if (ImGui::MenuItem("Add State")) {
            AddState();
        }

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints({360.0f, 0.0f}, {500.0f, ImGui::GetIO().DisplaySize.y});

    if (ImGui::BeginPopup("AnimStateMenu")) {
        mSelectedNode = StateNodeId(mContextNode);
        mView.mTransition = -1;
        ImGui::PushItemWidth(220.0f);
        DrawMachineSelection();
        ImGui::PopItemWidth();

        if (Machine->FindState(mSelectedNode) != nullptr) {
            ImGui::Separator();

            if (ImGui::MenuItem("Open State Graph")) {
                OpenStateId = mSelectedNode;
            }

            if (ImGui::MenuItem("Delete State", "Delete")) {
                mDirty |= Machine->RemoveState(mSelectedNode);
                mSelectedNode = -2;
            }
        } else if (mSelectedNode == FAnimStateMachineData::AnyStateId) {
            ImGui::TextDisabled("Drag Out to a state to add a shared transition.");
        } else {
            ImGui::TextDisabled("Connect Entry to the initial state.");
        }

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints({360.0f, 0.0f}, {500.0f, ImGui::GetIO().DisplaySize.y});

    if (ImGui::BeginPopup("AnimTransitionMenu")) {
        mSelectedNode = -2;
        mView.mTransition = static_cast<int32>(mContextNode - 2);
        ImGui::PushItemWidth(220.0f);
        DrawMachineSelection();
        ImGui::PopItemWidth();
        ImGui::Separator();

        if (ImGui::MenuItem("Open Transition Rule")) {
            OpenTransitionId = mView.mTransition;
        }

        if (ImGui::MenuItem("Delete Transition", "Delete")) {
            mDirty |= Machine->RemoveTransition(mView.mTransition);
            mView.mTransition = -1;
        }

        ImGui::EndPopup();
    }

    ed::Resume();

    if (!mApplyPositions) {
        const ImVec2 AnyStatePosition{ed::GetNodePosition(ed::NodeId{AnyStateKey})};

        if (std::abs(AnyStatePosition.x - Machine->GetAnyStateX()) > 0.5f || std::abs(AnyStatePosition.y - Machine->GetAnyStateY()) > 0.5f) {
            mDirty |= Machine->SetAnyStatePosition(AnyStatePosition.x, AnyStatePosition.y);
        }

        for (const auto& State : Machine->GetStates()) {
            const ImVec2 Position{ed::GetNodePosition(ed::NodeId{StateNodeKey(State.mId)})};

            if (std::isfinite(Position.x) && std::isfinite(Position.y) && (std::abs(Position.x - State.mX) > 0.5f || std::abs(Position.y - State.mY) > 0.5f)) {
                auto* Data{Machine->FindState(State.mId)};
                Data->mX = Position.x;
                Data->mY = Position.y;
                mDirty = true;
            }
        }
    }

    if (mFitRequested) {
        ed::NavigateToContent();
        mFitRequested = false;
    }

    mApplyPositions = false;
    ed::End();
    ed::SetCurrentEditor(nullptr);

    const int32 StateId{OpenStateId >= 0 ? OpenStateId : DoubleClickedState};
    const int32 TransitionId{OpenTransitionId >= 0 ? OpenTransitionId : (OpenLink >= 2 ? static_cast<int32>(OpenLink - 2) : -1)};

    if (auto* State{Machine->FindState(StateId)}; State != nullptr && State->mGraph != nullptr) {
        OpenState(StateId);
    } else if (Machine->FindTransition(TransitionId) != nullptr) {
        OpenTransitionRule(TransitionId);
    }
}

void FAnimGraphEditor::DrawMachineSelection() {
    auto* Machine{mEditing->FindStateMachine(mView.mMachine)};

    if (Machine == nullptr) {
        return;
    }

    if (auto* State{Machine->FindState(mSelectedNode)}) {
        ImGui::SeparatorText("State");
        FString Name{State->mName};

        if (EditString("Name", Name) && !Name.empty() && std::none_of(Machine->GetStates().begin(), Machine->GetStates().end(), [State, &Name](const auto& Other) {
            return Other.mId != State->mId && Other.mName == Name;
        })) {
            State->mName = Name;
            mDirty = true;
        }

        mDirty |= ImGui::Checkbox("Reset on Entry", &State->mResetOnEntry);

        if (ImGui::Button("Set as Entry")) {
            mDirty |= Machine->SetEntryState(State->mId);
        }
    } else if (auto* Transition{Machine->FindTransition(mView.mTransition)}) {
        ImGui::SeparatorText("Transition");
        const auto* From{Machine->FindState(Transition->mFromState)};
        const auto* To{Machine->FindState(Transition->mToState)};
        ImGui::Text("%s -> %s", Transition->mFromState == FAnimStateMachineData::AnyStateId ? "Any State" : From != nullptr ? From->mName.c_str() : "?", To != nullptr ? To->mName.c_str() : "?");
        mDirty |= ImGui::DragFloat("Blend Duration", &Transition->mDuration, 0.01f, 0.0f, 10.0f, "%.3f s", ImGuiSliderFlags_AlwaysClamp);
        mDirty |= ImGui::InputInt("Priority", &Transition->mPriority);
        ImGui::TextDisabled("Lower priority values are checked first");
    }
}

void FAnimGraphEditor::DrawTransitionRule() {
    // 규칙 노드는 Float와 Bool 입력을 구분하며 Result에는 Bool 출력만 연결할 수 있다.
    auto* Machine{mEditing->FindStateMachine(mView.mMachine)};
    auto* Transition{Machine != nullptr ? Machine->FindTransition(mView.mTransition) : nullptr};

    if (Transition == nullptr) {
        return;
    }

    auto& Graph{Transition->mRule};
    ed::SetCurrentEditor(mEditor);
    FAnimGraphNodeBuilder::ApplyStyle();
    const ImVec2 Origin{ImGui::GetCursorScreenPos()};
    const ImVec2 Size{ImGui::GetContentRegionAvail()};
    ed::Begin("TransitionRuleCanvas");

    if (mApplyPositions) {
        ed::SetNodePosition(ed::NodeId{1}, {450.0f, 0.0f});

        for (const auto& Node : Graph.GetNodes()) {
            ed::SetNodePosition(ed::NodeId{StateNodeKey(Node.mId)}, {Node.mX, Node.mY});
        }
    }

    FAnimGraphNodeBuilder Result{};
    Result.FitHeader("Result", "Can Enter Transition");
    Result.FitPins("Can Enter");
    Result.Begin(ed::NodeId{1}, 180.0f);
    Result.Header("Result", "Can Enter Transition");
    Result.InputPin(ed::PinId{1}, "Can Enter", Graph.GetResult() >= 0, BoolColor);
    Result.End();

    for (const auto& Node : Graph.GetNodes()) {
        const std::uintptr_t Key{StateNodeKey(Node.mId)};
        const char* Title{FAnimTransitionGraph::GetTitle(Node.mType)};
        const bool BoolOutput{FAnimTransitionGraph::IsBool(Node.mType)};
        const int32 InputCount{FAnimTransitionGraph::GetInputCount(Node.mType)};
        const ImVec4 InputColor{FAnimTransitionGraph::IsBoolInput(Node.mType) ? BoolColor : FAnimGraphNodeBuilder::FloatColor()};
        FAnimGraphNodeBuilder Builder{};
        Builder.FitHeader(Title, Title);
        Builder.FitPins(InputCount > 0 ? "A" : "", BoolOutput ? "Bool" : "Float");
        Builder.FitText(Node.mName.c_str());
        Builder.Begin(ed::NodeId{Key}, 160.0f);
        Builder.Header(Title, Title);

        if (InputCount > 0) {
            Builder.InputPin(ed::PinId{Key + 1}, "A", Node.mInputA >= 0, InputColor);
        }

        Builder.OutputPin(ed::PinId{Key + 3}, BoolOutput ? "Bool" : "Float", true, BoolOutput ? BoolColor : FAnimGraphNodeBuilder::FloatColor(), InputCount > 0);

        if (InputCount > 1) {
            Builder.InputPin(ed::PinId{Key + 2}, "B", Node.mInputB >= 0, InputColor);
        }

        Builder.BeginBody();

        if (Node.mType == EAnimRuleNodeType::Float || Node.mType == EAnimRuleNodeType::Bool) {
            const FString Value{Node.mType == EAnimRuleNodeType::Bool ? FString{Node.mBoolValue ? "true" : "false"} : std::format("{:.3f}", Node.mValue)};
            Builder.DrawText(Value.c_str(), BoolOutput ? BoolColor : FAnimGraphNodeBuilder::FloatColor());
        } else if (!Node.mName.empty()) {
            Builder.DrawText(Node.mName.c_str(), StateColor);
        }

        Builder.End();
    }

    for (const auto& Node : Graph.GetNodes()) {
        const std::uintptr_t Key{StateNodeKey(Node.mId)};
        const ImVec4 InputColor{FAnimTransitionGraph::IsBoolInput(Node.mType) ? BoolColor : FAnimGraphNodeBuilder::FloatColor()};
        const int32 InputCount{FAnimTransitionGraph::GetInputCount(Node.mType)};

        for (int32 Pin{}; Pin < InputCount; ++Pin) {
            const int32 Input{Pin == 0 ? Node.mInputA : Node.mInputB};

            if (Input >= 0) {
                ed::Link(ed::LinkId{Key + Pin + 1}, ed::PinId{StateNodeKey(Input) + 3}, ed::PinId{Key + Pin + 1}, InputColor, 2.0f);
            }
        }
    }

    if (Graph.GetResult() >= 0) {
        ed::Link(ed::LinkId{1}, ed::PinId{StateNodeKey(Graph.GetResult()) + 3}, ed::PinId{1}, BoolColor, 2.0f);
    }

    if (ed::BeginCreate()) {
        ed::PinId A{};
        ed::PinId B{};

        if (ed::QueryNewLink(&A, &B) && A && B) {
            if (A.Get() % 16 != 3) {
                std::swap(A, B);
            }

            const int32 From{StateNodeId(A.Get())};
            const int32 To{StateNodeId(B.Get())};
            const int32 Pin{static_cast<int32>(B.Get() % 16) - 1};
            // 드래그 중의 연결 유효성은 복사본으로 검사하고 수락한 경우에만 원본을 수정한다.
            auto Candidate{Graph};
            const bool Valid{A.Get() % 16 == 3 && (B.Get() == 1 ? Candidate.SetResult(From) : Candidate.Connect(From, To, Pin))};

            if (!Valid) {
                ed::RejectNewItem(BoolColor, 2.0f);
            } else if (ed::AcceptNewItem(StateColor, 2.0f)) {
                mDirty |= B.Get() == 1 ? Graph.SetResult(From) : Graph.Connect(From, To, Pin);
            }
        }
    }

    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::NodeId Node{};
        ed::LinkId Link{};

        while (ed::QueryDeletedNode(&Node)) {
            if (Node.Get() == 1) {
                ed::RejectDeletedItem();
            } else if (ed::AcceptDeletedItem()) {
                mDirty |= Graph.RemoveNode(StateNodeId(Node.Get()));
            }
        }

        while (ed::QueryDeletedLink(&Link)) {
            if (ed::AcceptDeletedItem()) {
                mDirty |= Link.Get() == 1 ? Graph.SetResult(-1) : Graph.Disconnect(StateNodeId(Link.Get()), static_cast<int32>(Link.Get() % 16) - 1);
            }
        }
    }

    ed::EndDelete();
    std::array<ed::NodeId, 1> Selected{};
    mSelectedNode = ed::GetSelectedNodes(Selected.data(), 1) == 1 ? StateNodeId(Selected[0].Get()) : -2;
    const ImVec2 Mouse{ImGui::GetMousePos()};
    ed::NodeId ContextNode{};
    ed::Suspend();

    if (ed::ShowNodeContextMenu(&ContextNode)) {
        mContextNode = ContextNode.Get();
        ed::SelectNode(ContextNode);
        ImGui::OpenPopup("AnimRuleNodeMenu");
    } else if (ed::ShowBackgroundContextMenu()) {
        mCreateX = Mouse.x;
        mCreateY = Mouse.y;
        ImGui::OpenPopup("CreateRuleNode");
    }

    if (ImGui::BeginPopup("CreateRuleNode")) {
        for (uint32 Index{}; Index < static_cast<uint32>(EAnimRuleNodeType::Count); ++Index) {
            const auto Type{static_cast<EAnimRuleNodeType>(Index)};

            if (ImGui::MenuItem(FAnimTransitionGraph::GetTitle(Type))) {
                const int32 Id{Graph.AddNode(Type, mCreateX, mCreateY)};

                if (Id >= 0) {
                    ed::SetNodePosition(ed::NodeId{StateNodeKey(Id)}, Mouse);
                    mDirty = true;
                }
            }
        }

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints({360.0f, 0.0f}, {500.0f, ImGui::GetIO().DisplaySize.y});

    if (ImGui::BeginPopup("AnimRuleNodeMenu")) {
        mSelectedNode = StateNodeId(mContextNode);
        ImGui::PushItemWidth(220.0f);
        DrawRuleSelection();
        ImGui::PopItemWidth();
        ImGui::Separator();

        if (ImGui::MenuItem("Delete Node", "Delete", false, Graph.FindNode(mSelectedNode) != nullptr)) {
            mDirty |= Graph.RemoveNode(mSelectedNode);
            mSelectedNode = -2;
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginDragDropTargetCustom(ImRect{Origin, {Origin.x + Size.x, Origin.y + Size.y}}, ImGui::GetID("RuleVariableDrop"))) {
        if (const ImGuiPayload* Payload{ImGui::AcceptDragDropPayload("MACAW_ANIM_GRAPH_VARIABLE")}; Payload != nullptr && Payload->Data != nullptr && Payload->DataSize > 1 && static_cast<const char*>(Payload->Data)[Payload->DataSize - 1] == '\0') {
            const FString Name{static_cast<const char*>(Payload->Data)};
            const auto& Variables{mRootEditing->GetVariables()};
            const auto Found{std::find_if(Variables.begin(), Variables.end(), [&Name](const auto& Variable) {
                return Variable.mName == Name;
            })};

            if (Found != Variables.end()) {
                const int32 Id{Graph.AddNode(Found->mType == EAnimGraphVariableType::Bool ? EAnimRuleNodeType::GetBool : EAnimRuleNodeType::GetFloat, Mouse.x, Mouse.y)};

                if (auto* Node{Graph.FindNode(Id)}) {
                    Node->mName = Name;
                    ed::SetNodePosition(ed::NodeId{StateNodeKey(Id)}, Mouse);
                    mDirty = true;
                }
            }
        }

        ImGui::EndDragDropTarget();
    }

    ed::Resume();

    if (!mApplyPositions) {
        for (const auto& Node : Graph.GetNodes()) {
            const ImVec2 Position{ed::GetNodePosition(ed::NodeId{StateNodeKey(Node.mId)})};

            if (std::isfinite(Position.x) && std::isfinite(Position.y) && (std::abs(Position.x - Node.mX) > 0.5f || std::abs(Position.y - Node.mY) > 0.5f)) {
                auto* Data{Graph.FindNode(Node.mId)};
                Data->mX = Position.x;
                Data->mY = Position.y;
                mDirty = true;
            }
        }
    }

    if (mFitRequested) {
        ed::NavigateToContent();
        mFitRequested = false;
    }

    mApplyPositions = false;
    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void FAnimGraphEditor::DrawRuleSelection() {
    // 상수 값, 조회할 변수와 플레이어 이름 등 선택한 규칙 노드에 필요한 속성만 표시한다.
    auto* Machine{mEditing->FindStateMachine(mView.mMachine)};
    auto* Transition{Machine != nullptr ? Machine->FindTransition(mView.mTransition) : nullptr};
    auto* Node{Transition != nullptr ? Transition->mRule.FindNode(mSelectedNode) : nullptr};

    if (Node == nullptr) {
        ImGui::TextWrapped("Connect a Bool output to Can Enter Transition. An unconnected result is false.");

        return;
    }

    ImGui::SeparatorText(FAnimTransitionGraph::GetTitle(Node->mType));

    if (Node->mType == EAnimRuleNodeType::Float) {
        mDirty |= ImGui::DragFloat("Value", &Node->mValue, 0.01f);
    } else if (Node->mType == EAnimRuleNodeType::Bool) {
        mDirty |= ImGui::Checkbox("Value", &Node->mBoolValue);
    } else if (Node->mType == EAnimRuleNodeType::GetFloat || Node->mType == EAnimRuleNodeType::GetBool) {
        if (ImGui::BeginCombo("Variable", Node->mName.empty() ? "Select Variable" : Node->mName.c_str())) {
            for (const auto& Variable : mRootEditing->GetVariables()) {
                if ((Variable.mType == EAnimGraphVariableType::Bool) == (Node->mType == EAnimRuleNodeType::GetBool) && ImGui::Selectable(Variable.mName.c_str(), Variable.mName == Node->mName)) {
                    Node->mName = Variable.mName;
                    mDirty = true;
                }
            }

            ImGui::EndCombo();
        }
    } else if (Node->mType == EAnimRuleNodeType::TimeRemaining || Node->mType == EAnimRuleNodeType::Finished) {
        mDirty |= EditString("Player Name", Node->mName);
        ImGui::TextWrapped("Leave empty when the source state contains a single animation player. Time Remaining uses seconds at the current play rate.");
    }
}
