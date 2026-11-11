#include "EnginePCH.h"
#include "Animation/AnimAssetArchive.h"
#include "../Source/Editor/AnimationEditor/FAnimGraphEditor.h"
#include "../Source/Editor/AnimationEditor/FAnimGraphPanel.h"
#include "Animation/AnimGraph.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameEngine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Render/GeometryGenerator.h"
#include "Render/RenderCommand.h"
#include "Render/RenderDevice.h"
#include "Render/RenderResourceManager.h"
#include "Serialization/JsonArchive.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <stdexcept>

namespace {
    void Check(bool Passed, const FString& Message) {
        if (!Passed) {
            throw std::runtime_error{Message};
        }
        std::printf("PASS %s\n", Message.c_str());
    }

    void DrawFrame(FAnimGraphEditor& Editor) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_Always);
        Editor.Draw();
        ImGui::Render();
    }

    bool FindText(const char* Text, ImRect& Bounds, const ImRect* Region = nullptr) {
        // 실제 ImGui 출력의 글리프 UV로 텍스트 위치를 찾아 표시 여부와 클릭 위치를 확인한다.
        bool Found{};
        ImFontBaked* Font{ImGui::GetIO().Fonts->Fonts[0]->GetFontBaked(13.0f)};
        for (const ImDrawList* List : ImGui::GetDrawData()->CmdLists) {
            for (int Index{}; Index < List->VtxBuffer.Size; ++Index) {
                int Offset{Index};
                ImRect Candidate{};
                bool Matches{true};
                for (const char* Character{Text}; *Character != '\0'; ++Character) {
                    const ImFontGlyph* Glyph{Font->FindGlyph(static_cast<ImWchar>(*Character))};
                    if (!Glyph->Visible) {
                        continue;
                    }
                    if (Offset + 3 >= List->VtxBuffer.Size || List->VtxBuffer[Offset].uv.x != Glyph->U0 || List->VtxBuffer[Offset].uv.y != Glyph->V0 || List->VtxBuffer[Offset + 2].uv.x != Glyph->U1 || List->VtxBuffer[Offset + 2].uv.y != Glyph->V1) {
                        Matches = false;
                        break;
                    }
                    if (Offset == Index) {
                        Candidate = {List->VtxBuffer[Offset].pos, List->VtxBuffer[Offset + 2].pos};
                    } else {
                        Candidate.Add(List->VtxBuffer[Offset].pos);
                        Candidate.Add(List->VtxBuffer[Offset + 2].pos);
                    }
                    Offset += 4;
                }
                if (Matches && (Region == nullptr || Region->Contains(Candidate.GetCenter()))) {
                    Bounds = Candidate;
                    Found = true;
                }
            }
        }
        return Found;
    }

    bool HasWire(float X) {
        // 그리기 결과의 색상과 삼각형 범위로 Bool 연결선이 실제로 생성됐는지 확인한다.
        const ImU32 Rgb{ImGui::ColorConvertFloat4ToU32({0.82f, 0.24f, 0.28f, 1.0f}) & 0x00FFFFFF};
        for (const ImDrawList* List : ImGui::GetDrawData()->CmdLists) {
            for (const ImDrawCmd& Command : List->CmdBuffer) {
                if (X < Command.ClipRect.x || X > Command.ClipRect.z) {
                    continue;
                }
                for (unsigned int Index{Command.IdxOffset}; Index + 2 < Command.IdxOffset + Command.ElemCount; Index += 3) {
                    const ImDrawVert& A{List->VtxBuffer[Command.VtxOffset + List->IdxBuffer[Index]]};
                    const ImDrawVert& B{List->VtxBuffer[Command.VtxOffset + List->IdxBuffer[Index + 1]]};
                    const ImDrawVert& C{List->VtxBuffer[Command.VtxOffset + List->IdxBuffer[Index + 2]]};
                    if ((A.col & 0x00FFFFFF) == Rgb && (B.col & 0x00FFFFFF) == Rgb && (C.col & 0x00FFFFFF) == Rgb && (A.col & 0xFF000000) != 0 && std::min({A.pos.x, B.pos.x, C.pos.x}) < X && std::max({A.pos.x, B.pos.x, C.pos.x}) > X) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    void DragEditor(FAnimGraphEditor& Editor, const ImVec2& From, const ImVec2& To) {
        ImGuiIO& Io{ImGui::GetIO()};
        Io.AddMousePosEvent(From.x, From.y);
        DrawFrame(Editor);
        Io.AddMouseButtonEvent(0, true);
        DrawFrame(Editor);
        Io.AddMousePosEvent((From.x + To.x) * 0.5f, (From.y + To.y) * 0.5f);
        DrawFrame(Editor);
        Io.AddMousePosEvent(To.x, To.y);
        DrawFrame(Editor);
        Io.AddMouseButtonEvent(0, false);
        DrawFrame(Editor);
        DrawFrame(Editor);
    }

    void TestAnyStateEditor() {
        UAnimGraph Graph{};
        const int32 Node{Graph.AddNode("StateMachine", "Shared Transition Test", 0.0f, 0.0f)};
        Graph.Connect(Node, UAnimGraph::OutputNodeId, 0);
        auto* Data{Graph.FindStateMachine(Node)};
        Data->AddState("First", 200.0f, 0.0f, Graph);
        const int32 Hit{Data->AddState("Hit", 200.0f, 180.0f, Graph)};
        const FString Path{"Assets/__AnyStateEditorTest_" + std::to_string(GetCurrentProcessId()) + ".animgraph"};
        FString Error{};
        Check(UAssetManager::SaveAnimGraph(Path, Graph, Error), "Create an isolated Any State editor fixture");
        FAnimGraphEditor Editor{};
        Check(Editor.Open(Path, Error) && Editor.OpenStateMachine(Node), "Open the Any State editor fixture");

        for (uint32 Frame{}; Frame < 45; ++Frame) {
            DrawFrame(Editor);
        }

        ImRect AnyState{};
        ImRect Target{};
        Check(FindText("Any State", AnyState) && FindText("Hit", Target), "The machine editor always draws Any State beside ordinary states");
        const float Scale{(Target.Min.x - AnyState.Min.x) / 380.0f};
        const ImRect AnyRegion{{AnyState.Min.x - 20.0f * Scale, AnyState.Min.y}, {AnyState.Min.x + 160.0f * Scale, AnyState.Min.y + 85.0f * Scale}};
        const ImRect TargetRegion{{Target.Min.x - 20.0f * Scale, Target.Min.y}, {Target.Min.x + 180.0f * Scale, Target.Min.y + 85.0f * Scale}};
        ImRect Out{};
        ImRect In{};
        Check(FindText("Out", Out, &AnyRegion) && FindText("In", In, &TargetRegion), "Any State exposes an output pin and the target exposes an input pin");
        const ImVec2 SourcePin{Out.Max.x + 14.0f * Scale, Out.GetCenter().y + Scale};
        const ImVec2 TargetPin{In.Min.x - 14.0f * Scale, In.GetCenter().y + Scale};
        DragEditor(Editor, SourcePin, TargetPin);
        ImGuiIO& Io{ImGui::GetIO()};
        Io.AddKeyEvent(ImGuiMod_Ctrl, true);
        Io.AddKeyEvent(ImGuiKey_S, true);
        DrawFrame(Editor);
        Io.AddKeyEvent(ImGuiKey_S, false);
        Io.AddKeyEvent(ImGuiMod_Ctrl, false);
        DrawFrame(Editor);
        FAnimAssetArchive Load{true};
        UAnimGraph Restored{};
        Check(Load.Load(Path), "Read the graph after Ctrl+S");
        Restored.Serialize(Load);
        const auto* RestoredData{Restored.FindStateMachine(Node)};
        Check(!Load.HasError() && RestoredData != nullptr && RestoredData->GetTransitions().size() == 1 && RestoredData->GetTransitions().front().mFromState == FAnimStateMachineData::AnyStateId && RestoredData->GetTransitions().front().mToState == Hit, "Dragging Any State Out to Hit In creates and saves a shared transition");
        Check(Editor.OpenTransitionRule(RestoredData->GetTransitions().front().mId), "Open the new Any State transition rule");

        for (uint32 Frame{}; Frame < 5; ++Frame) {
            DrawFrame(Editor);
        }

        ImRect Bounds{};
        Check(FindText("Any State -> Hit", Bounds) && FindText("Can Enter Transition", Bounds), "Any State rules use the ordinary rule editor and a named breadcrumb");
        std::filesystem::remove(Path);
    }

    void TestEditor() {
        // 중첩 그래프 및 경로 탐색과 노드 저장 순서에 독립적인 전환 규칙 연결선 표시를 확인한다.
        FAnimGraphEditor Editor{};
        FString Error{};
        Check(Editor.Open("Assets/StateMachineLab/ThirdPerson.animgraph", Error), "Open the Week11 graph editor");
        DrawFrame(Editor);
        Check(Editor.OpenStateMachine(0) && Editor.OpenState(0) && Editor.OpenStateMachine(0), "Navigate into the nested Movement machine");
        DrawFrame(Editor);
        Check(Editor.OpenTransitionRule(0), "Open a nested transition rule");
        DrawFrame(Editor);
        Check(Editor.NavigateTo(0), "Breadcrumb navigation skips multiple levels");
        DrawFrame(Editor);
        Check(Editor.OpenStateMachine(0), "Reopen the top-level machine");
        const auto* Graph{UAssetManager::GetAssetByPath<UAnimGraph>("StateMachineLab/ThirdPerson.animgraph")};
        const auto& Transitions{Graph->FindStateMachine(0)->GetTransitions()};
        const auto Found{std::find_if(Transitions.begin(), Transitions.end(), [](const FAnimTransitionData& Transition) {
            return Transition.mFromState == 3 && Transition.mToState == 0;
        })};
        Check(Found != Transitions.end() && Editor.OpenTransitionRule(Found->mId), "Open Dance to Movement");
        for (uint32 Frame{}; Frame < 45; ++Frame) {
            DrawFrame(Editor);
        }
        ImRect Getter{};
        ImRect Not{};
        Check(FindText("Get Bool", Getter) && FindText("Not", Not), "Draw Bool and Not rule nodes");
        const float Scale{(Not.Min.x - Getter.Min.x) / 250.0f};
        const float WireX{(Getter.Min.x + 159.0f * Scale + Not.Min.x - 23.0f * Scale) * 0.5f};
        Check(HasWire(WireX), "Render the IsDancing to Not wire independently of node storage order");
        ImRect Bounds{};
        Check(!FindText("< Back", Bounds) && !FindText("Save", Bounds), "Use breadcrumbs and Ctrl+S without Back or Save buttons");
    }

    void DrawDebugFrame(FAnimGraphPanel& Panel, USkeletalMeshComponent* Mesh, bool Playing) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20.0f, 20.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1240.0f, 680.0f}, ImGuiCond_Always);
        bool Open{true};
        Panel.Draw(Mesh, Playing, Open);
        ImGui::Render();
    }

    void SettleDebugView(FAnimGraphPanel& Panel, USkeletalMeshComponent* Mesh) {
        for (uint32 Frame{}; Frame < 5; ++Frame) {
            DrawDebugFrame(Panel, Mesh, true);
        }
    }

    void ClickDebugText(FAnimGraphPanel& Panel, USkeletalMeshComponent* Mesh, const char* Text, bool DoubleClick) {
        ImRect Bounds{};
        Check(FindText(Text, Bounds), FString{"Find debug navigation target: "} + Text);
        ImGuiIO& Io{ImGui::GetIO()};
        const ImVec2 Position{Bounds.GetCenter()};
        Io.AddMousePosEvent(Position.x, Position.y);
        DrawDebugFrame(Panel, Mesh, true);

        for (uint32 Click{}; Click < (DoubleClick ? 2u : 1u); ++Click) {
            Io.AddMouseButtonEvent(0, true);
            DrawDebugFrame(Panel, Mesh, true);
            Io.AddMouseButtonEvent(0, false);
            DrawDebugFrame(Panel, Mesh, true);
        }

        SettleDebugView(Panel, Mesh);
    }

    void TestRuntimeDebugPanel() {
        // PIE 변수 조작, 중첩 실행 뷰와 에셋 교체 후 이전 디버그 참조의 초기화를 확인한다.
        FGeometryGenerator::CreateDefaultMeshDatas();
        auto& Assets{UAssetManager::Get()};
        Assets.CreateDefaultTextures();
        Assets.CreateDefaultMaterial();
        Assets.CreateDefaultMeshes();
        Assets.CreateBillboardMaterial();
        Assets.CreateEditorArrowMaterial();
        UWorld* World{UWorld::CreateWorld(EWorldType::Editor, false)};
        Check(FJsonArchive::LoadWorld(World, "Scenes/Demo_StateMachineLab.scene"), "Load a character for runtime debug display");
        USkeletalMeshComponent* Mesh{};
        for (auto* Actor : World->GetPersistentLevel()->GetActors()) {
            if (Actor->GetName() == "State Machine Player") {
                for (auto* Component : Actor->GetComponents()) {
                    if (auto* Candidate{Cast<USkeletalMeshComponent>(Component)}) {
                        Mesh = Candidate;
                    }
                }
            }
        }
        Check(Mesh != nullptr && Mesh->SetAnimGraphBool("IsCrouching", true), "Set native graph Bool input for debug rendering");
        auto* Graph{Cast<UAnimGraphInstance>(Mesh->GetAnimInstance())};
        for (uint32 Frame{}; Frame < 30; ++Frame) {
            Mesh->TickComponent(1.0f / 60.0f);
        }
        {
            FAnimGraphPanel Panel{};
            for (uint32 Frame{}; Frame < 45; ++Frame) {
                DrawDebugFrame(Panel, Mesh, true);
            }
            ImRect Bounds{};
            Check(FindText("IsInAir", Bounds) && FindText("IsCrouching", Bounds) && FindText("IsDancing", Bounds), "Runtime debug panel displays Bool graph variables like Macaw");
            FindText("IsInAir", Bounds);
            ImGuiIO& Io{ImGui::GetIO()};
            const ImVec2 Position{Bounds.GetCenter()};
            Io.AddMousePosEvent(Position.x, Position.y);
            DrawDebugFrame(Panel, Mesh, true);
            Io.AddMouseButtonEvent(0, true);
            DrawDebugFrame(Panel, Mesh, true);
            Io.AddMouseButtonEvent(0, false);
            DrawDebugFrame(Panel, Mesh, true);
            Check(Graph->GetBoolVariable("IsInAir"), "Runtime Bool checkbox edits the selected character instance");
            for (uint32 Frame{}; Frame < 2; ++Frame) {
                DrawDebugFrame(Panel, Mesh, false);
            }
            FindText("IsInAir", Bounds);
            const ImVec2 DisabledPosition{Bounds.GetCenter()};
            Io.AddMousePosEvent(DisabledPosition.x, DisabledPosition.y);
            DrawDebugFrame(Panel, Mesh, false);
            Io.AddMouseButtonEvent(0, true);
            DrawDebugFrame(Panel, Mesh, false);
            Io.AddMouseButtonEvent(0, false);
            DrawDebugFrame(Panel, Mesh, false);
            Check(Graph->GetBoolVariable("IsInAir"), "Preview Bool variables stay read-only outside Play");

            SettleDebugView(Panel, Mesh);
            Check(FindText("Character State Machine", Bounds) && !FindText("Ground Locomotion", Bounds) && !FindText("Dance Loop", Bounds), "The runtime root graph collapses state pose graphs instead of flattening them");
            ClickDebugText(Panel, Mesh, "Character State Machine", true);
            Check(FindText("Entry", Bounds) && FindText("Movement", Bounds) && !FindText("Ground Locomotion", Bounds), "Double-clicking the runtime machine opens its state diagram");
            ClickDebugText(Panel, Mesh, "Movement", true);
            Check(FindText("Movement State Machine", Bounds) && !FindText("Ground Locomotion", Bounds), "Double-clicking a state opens only that state's pose graph");
            ClickDebugText(Panel, Mesh, "Movement State Machine", true);
            Check(FindText("Grounded", Bounds) && FindText("Crouch", Bounds), "Nested runtime state machines remain navigable");
            Check(Panel.OpenState(1), "Open the live Crouch pose graph");
            SettleDebugView(Panel, Mesh);
            Check(FindText("Crouch Locomotion", Bounds) && !FindText("Dance Loop", Bounds), "A state view shows its own saved pose layout and live player");
            ClickDebugText(Panel, Mesh, "Anim Graph", false);
            Check(FindText("Character State Machine", Bounds) && !FindText("Crouch Locomotion", Bounds), "The root breadcrumb returns across multiple runtime levels");
            Check(Panel.OpenStateMachine(0), "Reopen the runtime root machine");
            SettleDebugView(Panel, Mesh);
            const auto* Source{Graph->GetSourceGraph()};
            const auto& Transitions{Source->FindStateMachine(0)->GetTransitions()};
            const auto DanceReturn{std::find_if(Transitions.begin(), Transitions.end(), [](const auto& Transition) {
                return Transition.mFromState == 3 && Transition.mToState == 0;
            })};
            Check(DanceReturn != Transitions.end() && Panel.OpenTransitionRule(DanceReturn->mId), "Open a runtime transition condition graph");
            SettleDebugView(Panel, Mesh);
            ImRect Getter{};
            ImRect Not{};
            Check(FindText("Get Bool", Getter) && FindText("Not", Not) && FindText("Can Enter Transition", Bounds), "The runtime rule view draws the complete Bool to Not to Result tree");
            const float Scale{(Not.Min.x - Getter.Min.x) / 250.0f};
            const float WireX{(Getter.Min.x + 159.0f * Scale + Not.Min.x - 23.0f * Scale) * 0.5f};
            Check(HasWire(WireX), "Runtime transition wires render even when consumers precede producers");
            Check(FindText("Source state inactive", Bounds), "Runtime rules distinguish live values from transition eligibility");
            Graph->SetBoolVariable("IsDancing", true);
            DrawDebugFrame(Panel, Mesh, true);
            Check(FindText("true", Bounds) && FindText("false", Bounds), "Runtime rule nodes display live input and negated result values");
            FString Error{};
            const FString GraphPath{Mesh->GetAnimGraph()};
            Check(Mesh->SetAnimGraph(GraphPath, Error), "Rebuild the inspected runtime graph");
            Mesh->TickComponent(1.0f / 60.0f);
            SettleDebugView(Panel, Mesh);
            Check(FindText("Character State Machine", Bounds) && !FindText("Can Enter Transition", Bounds), "Rebuilding a graph safely resets stale runtime navigation");
            Graph = Cast<UAnimGraphInstance>(Mesh->GetAnimInstance());
            Check(Panel.OpenStateMachine(0) && Panel.OpenTransitionRule(DanceReturn->mId), "Inspect a rule before replacing its source asset data");
            SettleDebugView(Panel, Mesh);
            const uint64 Generation{Graph->GetGraphGeneration()};
            UAnimGraph Replacement{};
            auto* Asset{UAssetManager::GetAssetByPath<UAnimGraph>("StateMachineLab/ThirdPerson.animgraph")};
            Replacement.CopyFrom(*Asset);
            Asset->CopyFrom(Replacement);
            DrawDebugFrame(Panel, Mesh, true);
            Check(!Panel.OpenStateMachine(0), "Changed source data cannot expose stale runtime node pointers");
            World->SetWorldType(EWorldType::PIE);
            Mesh->TickComponent(1.0f / 60.0f);
            World->SetWorldType(EWorldType::Editor);
            SettleDebugView(Panel, Mesh);
            Check(Graph->GetGraphGeneration() > Generation && FindText("Character State Machine", Bounds) && !FindText("Can Enter Transition", Bounds), "Hot-reloading asset data resets nested debug navigation without replacing the instance");

            auto* SharedData{Replacement.FindStateMachine(0)};
            const int32 SharedTransition{SharedData->AddTransition(FAnimStateMachineData::AnyStateId, 2)};
            auto& SharedRule{SharedData->FindTransition(SharedTransition)->mRule};
            const int32 Time{SharedRule.AddNode(EAnimRuleNodeType::StateTime, 0.0f, 0.0f)};
            const int32 Limit{SharedRule.AddNode(EAnimRuleNodeType::Float, 0.0f, 120.0f)};
            const int32 Compare{SharedRule.AddNode(EAnimRuleNodeType::Greater, 220.0f, 0.0f)};
            SharedRule.FindNode(Limit)->mValue = 1000.0f;
            SharedRule.Connect(Time, Compare, 0);
            SharedRule.Connect(Limit, Compare, 1);
            SharedRule.SetResult(Compare);
            Asset->CopyFrom(Replacement);
            World->SetWorldType(EWorldType::PIE);
            Mesh->TickComponent(1.0f / 60.0f);
            World->SetWorldType(EWorldType::Editor);
            SettleDebugView(Panel, Mesh);
            Check(Panel.OpenStateMachine(0), "Open a live machine containing an Any State edge");
            SettleDebugView(Panel, Mesh);
            Check(FindText("Any State", Bounds) && Panel.OpenTransitionRule(SharedTransition), "The runtime debugger draws Any State and opens its rule");
            SettleDebugView(Panel, Mesh);
            const auto* Runtime{dynamic_cast<FAnimNode_StateMachine*>(Graph->FindRuntimeNode(*Asset, 0))};
            const FString Elapsed{std::format("{:.3f}", Runtime->FindState(Runtime->GetCurrentState())->mElapsed)};
            Check(FindText("Any State ->", Bounds) && FindText("Source state active", Bounds) && FindText(Elapsed.c_str(), Bounds), "Any State debug rules use the current state's live playback time and eligibility");
        }
        UGameEngine Engine{};
        Engine.DestroyWorld(World);
    }
}

int main() {
    try {
        FRenderDevice Device{};
        RenderCommand::Init(&Device);
        FRenderResourceManager::Init();
        ImGui::CreateContext();
        ImGuiIO& Io{ImGui::GetIO()};
        Io.IniFilename = nullptr;
        Io.LogFilename = nullptr;
        Io.DisplaySize = {1280.0f, 720.0f};
        Io.DeltaTime = 1.0f / 60.0f;
        unsigned char* Pixels{};
        int Width{};
        int Height{};
        Io.Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);
        TestEditor();
        TestAnyStateEditor();
        TestRuntimeDebugPanel();
        ImGui::DestroyContext();
        std::printf("PASS: Week11 nested graph editor, breadcrumbs and transition rule wiring.\n");
        return 0;
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "FAIL: %s\n", Error.what());
        return 1;
    }
}
