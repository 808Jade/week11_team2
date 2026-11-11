import Fs from 'node:fs';
import Path from 'node:path';
import { fileURLToPath as FileURLToPath } from 'node:url';

const Root = Path.resolve(Path.dirname(FileURLToPath(import.meta.url)), '..');
const Folder = 'Assets/LocomotionStage';
const Read = File => JSON.parse(Fs.readFileSync(Path.join(Root, File), 'utf8'));
const Save = (File, Value) => {
    Fs.mkdirSync(Path.dirname(Path.join(Root, File)), { recursive: true });
    Fs.writeFileSync(Path.join(Root, File), typeof Value === 'string' ? Value : JSON.stringify(Value, null, 4) + '\n');
};
const Clone = Value => structuredClone(Value);
const Library = Read(`${Folder}/Library.json`);
const ClipName = Entry => Entry.Object.match(/\|([^|]+)\[\d+\]$/)?.[1];
const ClipMaps = Object.fromEntries(['InPlace', 'RootMotion'].map(Kind => [Kind, Object.fromEntries(Library[Kind].filter(Entry => Entry.Type === 6).map(Entry => [ClipName(Entry), Entry]))]));
const Clips = Object.keys(ClipMaps.InPlace).sort();
if (Clips.length !== 43 || Object.keys(ClipMaps.RootMotion).length !== 43) throw Error('Expected all 43 clips in both libraries.');
const MeshKey = Library.InPlace.find(Entry => Entry.Type === 2).Key;

function VisitGraphs(Graph, Callback) {
    Callback(Graph);
    for (const Machine of Graph.StateMachines) for (const State of Machine.States) VisitGraphs(State.Graph, Callback);
}

function EmptyGraph(Variables) {
    return { FormatVersion: 1, Links: [], Nodes: [], OutputX: 350, OutputY: 0, StateMachines: [], VariableGetters: [], Variables: Clone(Variables) };
}

function Rule(Type, Name = '', Value = 0) {
    return { Nodes: [{ Id: 0, Type, Name, Value, Bool: false, A: -1, B: -1, X: 0, Y: 0 }], Result: 0 };
}

function NotRule(Name) {
    return { Nodes: [{ Id: 0, Type: 3, Name, Value: 0, Bool: false, A: -1, B: -1, X: 0, Y: 0 }, { Id: 1, Type: 12, Name: '', Value: 0, Bool: false, A: 0, B: -1, X: 230, Y: 0 }], Result: 1 };
}

function SequenceGraph(Variables, Name, Key, Loop) {
    const Graph = EmptyGraph(Variables);
    Graph.Nodes.push({ Id: 0, Name, Type: 'SequencePlayer', X: 0, Y: 0, Parameters: [{ Name: 'Sequence', Value: Key }, { Name: 'Loop', Value: String(Loop) }, { Name: 'PlayRate', Value: '1' }] });
    Graph.Links.push({ From: 0, To: -1, Pin: 0 });
    return Graph;
}

for (const Kind of ['InPlace', 'RootMotion']) {
    for (const Name of ['BS_Locomotion', 'BS_Crouch', 'BS_AimPitch', 'BS_Swim', 'BS_AllClips']) {
        const Space = Read(`Assets/StateMachineLab/${Name}.blendspace`);
        for (const Sample of Space.Samples) Sample.Animation = ClipMaps[Kind][Path.basename(Sample.Animation, '.fbxbin')].Key;
        if (Name === 'BS_Locomotion' && Kind === 'RootMotion') Space.Samples.forEach(Sample => {
            const Entry = Object.values(ClipMaps[Kind]).find(Clip => Clip.Key === Sample.Animation);
            Sample.X = Math.hypot(...Entry.RootDelta) / Entry.Length;
        });
        if (Name === 'BS_Locomotion' && Kind === 'RootMotion') Space.Axes[0].Max = 9;
        Save(`${Folder}/Graphs/${Kind}_${Name}.blendspace`, Space);
    }

    const Graph = Read('Assets/StateMachineLab/ThirdPerson.animgraph');
    Graph.Variables.push({ Name: 'IsSeated', Default: 0, BoolDefault: false, Source: 0, Type: 1 });
    // Speed는 스크립트가 매 프레임 넣는다(InPlace는 실제 이동 속도, RootMotion은 입력 속도).
    // OwnerSpeed로 두면 그래프가 컴포넌트 위치 변화로 속도를 다시 계산해 덮어쓰는데, 컴포넌트가 액터 스크립트보다 먼저
    // Tick하므로 "지난 프레임 이동량 / 이번 프레임 DeltaTime"이 되어 프레임 시간이 흔들릴 때마다 Blend Space가 떨린다.
    Graph.Variables.find(Variable => Variable.Name === 'Speed').Source = 0;
    VisitGraphs(Graph, Child => {
        Child.Variables = Clone(Graph.Variables);
        for (const Node of Child.Nodes) for (const Parameter of Node.Parameters) {
            if (Parameter.Name === 'Sequence') Parameter.Value = ClipMaps[Kind][Path.basename(Parameter.Value, '.fbxbin')].Key;
            if (Parameter.Name === 'BlendSpace') Parameter.Value = `LocomotionStage/Graphs/${Kind}_${Path.basename(Parameter.Value)}`;
        }
    });
    // 착지 클립(약 1.3초)이 끝날 때까지 기다리지 않는다. 움직이고 있으면 0.12초 뒤, 서 있으면 끝나기 0.3초 전에 Grounded로 섞는다.
    // (Speed > 0.3 && StateTime >= 0.12) || TimeRemaining(Land) <= 0.3
    VisitGraphs(Graph, Child => {
        for (const Machine of Child.StateMachines) {
            const Ids = Object.fromEntries(Machine.States.map(State => [State.Name, State.Id]));
            if (Ids.Land === undefined || Ids.Grounded === undefined) continue;
            for (const Transition of Machine.Transitions) if (Transition.From === Ids.Land && Transition.To === Ids.Grounded) {
                const N = (Id, Type, Name, Value, A, B, X, Y) => ({ Id, Type, Name, Value, Bool: false, A, B, X, Y });
                Transition.Duration = 0.2;
                Transition.Rule = { Nodes: [
                    N(0, 2, 'Speed', 0, -1, -1, 0, 0), N(1, 0, '', 0.3, -1, -1, 0, 90), N(2, 4, '', 0, 0, 1, 230, 40),
                    N(3, 13, '', 0, -1, -1, 0, 180), N(4, 0, '', 0.12, -1, -1, 0, 270), N(5, 5, '', 0, 3, 4, 230, 220),
                    N(6, 10, '', 0, 2, 5, 460, 130), N(7, 14, 'Land', 0, -1, -1, 0, 360), N(8, 0, '', 0.3, -1, -1, 0, 450),
                    N(9, 7, '', 0, 7, 8, 230, 400), N(10, 11, '', 0, 6, 9, 690, 260)
                ], Result: 10 };
            }
        }
    });
    Graph.Nodes[0].Name = 'Locomotion Stage';
    const Top = Graph.StateMachines[0];
    Top.States.forEach((State, Index) => { State.X = (Index % 3) * 370; State.Y = Math.floor(Index / 3) * 260; });
    const Seated = EmptyGraph(Graph.Variables);
    Seated.Nodes.push({ Id: 0, Type: 'StateMachine', Name: 'Seated Lifecycle', Parameters: [], X: 0, Y: 0 });
    Seated.Links.push({ From: 0, To: -1, Pin: 0 });
    Seated.StateMachines.push({ Node: 0, Entry: 0, AnyStateX: -300, AnyStateY: -170, States: [
        { Id: 0, Name: 'Sit Enter', X: 0, Y: 0, ResetOnEntry: true, Graph: SequenceGraph(Graph.Variables, 'Seat Enter', ClipMaps[Kind].Sitting_Enter.Key, false) },
        { Id: 1, Name: 'Seated Idle', X: 370, Y: 0, ResetOnEntry: true, Graph: SequenceGraph(Graph.Variables, 'Seat Idle', ClipMaps[Kind].Sitting_Idle_Loop.Key, true) },
        { Id: 2, Name: 'Sit Exit', X: 740, Y: 0, ResetOnEntry: true, Graph: SequenceGraph(Graph.Variables, 'Seat Exit', ClipMaps[Kind].Sitting_Exit.Key, false) }
    ], Transitions: [
        { Id: 0, From: 0, To: 1, Duration: 0.12, Priority: 0, Rule: Rule(15, 'Seat Enter') },
        { Id: 1, From: 1, To: 2, Duration: 0.12, Priority: 0, Rule: NotRule('IsSeated') }
    ] });
    Top.States.push({ Id: 6, Name: 'Seated', X: 0, Y: 520, ResetOnEntry: true, Graph: Seated });
    Top.Transitions.push({ Id: 30, From: 0, To: 6, Duration: 0.18, Priority: 4, Rule: Rule(3, 'IsSeated') });
    Top.Transitions.push({ Id: 31, From: 6, To: 0, Duration: 0.18, Priority: 0, Rule: Rule(15, 'Seat Exit') });
    Save(`${Folder}/Graphs/AG_${Kind}.animgraph`, Graph);

    for (const Name of Clips) {
        const Entry = ClipMaps[Kind][Name];
        const Events = [];
        const Add = (Event, Phase, Duration = 0, Track = 0) => Events.push({ Name: Event, Time: +(Entry.Length * Phase).toFixed(5), Duration: +(Entry.Length * Duration).toFixed(5), Track });
        if (/^(Walk|Jog|Sprint|Crouch_Fwd)/.test(Name)) { Add('Foot_L', 0.12); Add('Foot_R', 0.62); }
        if (Name === 'Swim_Fwd_Loop') { Add('SwimStroke', 0.18); Add('SwimStroke', 0.68); }
        if (/^(Punch|Sword_Attack|Pistol_Shoot|Spell_Simple_Shoot)/.test(Name)) { Add('AttackWindow', 0.24, 0.24, 1); Add('Impact', 0.34); }
        if (Name === 'Roll') Add('DodgeWindow', 0.08, 0.64, 1);
        if (Name === 'Jump_Land') Add('Land', 0.12);
        if (/^(Interact|PickUp_Table)$/.test(Name)) Add('Interact', 0.46);
        if (Name === 'Pistol_Reload') { Add('MagazineOut', 0.28); Add('MagazineIn', 0.7); }
        if (Events.length) Save('Assets/' + Entry.Key.replace(/\.fbxbin$/, '.notifies.json'), { Version: 1, Notifies: Events });
    }
}

const LuaString = Value => JSON.stringify(Value);
Save('Scripts/locomotion_stage_library.lua', 'return {\n    Clips = {\n' + Clips.map(Name => `        ${LuaString(Name)},`).join('\n') + '\n    },\n' + ['InPlace', 'RootMotion'].map(Kind => `    ${Kind} = {\n` + Clips.map(Name => `        [${LuaString(Name)}] = ${LuaString(ClipMaps[Kind][Name].Key)},`).join('\n') + '\n    },').join('\n') + '\n}\n');

const Colors = { Navy: [0.022, 0.033, 0.055], Floor: [0.11, 0.14, 0.18], Pale: [0.69, 0.76, 0.79], Cyan: [0.03, 0.68, 0.65], Lime: [0.48, 0.83, 0.07], Orange: [0.91, 0.26, 0.055], Violet: [0.34, 0.22, 0.77], Water: [0.025, 0.24, 0.34] };
const Actors = [];
let NextId = 1;
const Vector = ([X = 0, Y = 0, Z = 0]) => ({ X, Y, Z });
const Rotation = ([Pitch = 0, Yaw = 0, Roll = 0]) => ({ Pitch, Yaw, Roll });
const Transform = (Location = [0, 0, 0], Scale = [1, 1, 1], Angles = [0, 0, 0]) => ({ Location: Vector(Location), Rotation: Rotation(Angles), Scale: Vector(Scale) });
const MaterialTemplate = Clone(Read('Scenes/Demo_StateMachineLab.scene').World.PersistentLevel.Actors.find(Actor => Actor.Name === 'State Machine Player').Components.find(Component => Component.Class === 'USkeletalMeshComponent').OverrideMaterials[0].Instance);

function Material(Color, Glow = 0) {
    const Instance = Clone(MaterialTemplate);
    Instance.BaseColor = { X: Color[0], Y: Color[1], Z: Color[2], W: 1 };
    Instance.SpecularColor = { X: 0.09, Y: 0.09, Z: 0.09 };
    Instance.Shininess = 24;
    if (Glow) Instance.EmissiveColor = Vector(Color.map(Value => Value * Glow));
    return { Base: 'DefaultMaterial', Instance };
}

function Component(Class, Name, Properties, Parent = 0, Extra = {}) {
    return { Class, Name, Id: NextId++, Outer: 0, Parent, Properties: { bAutoActivate: true, bVisible: true, bHiddenInGame: false, ...Properties }, ...Extra };
}

function Actor(Name, Class, Components, Script = '', RootId = Components[0].Id) {
    const Value = { Class, Name, RootComponent: RootId, Properties: { ScriptPath: Script, bActorEnableCollision: false }, Components };
    Actors.push(Value);
    return Value;
}

function Box(Name, Location, Scale, Color = Colors.Floor, Solid = false, Angles = [0, 0, 0], Mesh = 'Cube') {
    const Body = Component('UStaticMeshComponent', 'UStaticMeshComponent', { StaticMesh: Mesh, Transform: Transform(Location, Scale, Angles), bCastShadow: true, bReceiveShadow: true, bBlockComponent: Solid, bGenerateOverlapEvents: false }, 0, { OverrideMaterials: [Material(Color)] });
    const Value = Actor(Name, 'AStaticMeshActor', [Body]);
    Value.Properties.bActorEnableCollision = Solid;
    return Value;
}

function Text(Name, Value, Location, Size = 0.25, Angles = [0, 0, 0]) {
    return Actor(Name, 'ATextRenderActor', [Component('UTextRenderComponent', 'TextRenderComponent', { Text: Value, Font: 'Assets/Fonts/Pretendard.json', TextSize: Size, Transform: Transform(Location, [1, 1, 1], Angles), bBlockComponent: false, bCastShadow: false, bGenerateOverlapEvents: false })]);
}

function Character(Name, Location, Color, Script, Kind = 'InPlace', Yaw = 0) {
    const SceneRoot = Component('USceneComponent', 'DefaultSceneRoot', { Transform: Transform(Location, [1, 1, 1], [0, Yaw, 0]) });
    const Body = Component('USkeletalMeshComponent', 'CharacterMesh', { SkeletalMesh: MeshKey, Transform: Transform([0, 0, 0], [1, 1, 1], [0, 90, 0]), bBlockComponent: false, bGenerateOverlapEvents: false, bCastShadow: true, bReceiveShadow: true, bEnableRootMotion: Kind === 'RootMotion' }, SceneRoot.Id, { AnimGraph: `LocomotionStage/Graphs/AG_${Kind}.animgraph`, BonePose: [], OverrideMaterials: [Material(Color), Material(Color.map(Value => Value * 0.18))] });
    return Actor(Name, 'AActor', [SceneRoot, Body], Script);
}

for (const [Name, Location, Angles, Intensity, Color] of [
    ['Stage Key', [0, 0, 12], [48, -45, 180], 1.5, 4294967295],
    ['Stage Fill', [0, 0, 8], [28, 130, 180], 0.4, 4294967295]
]) Actor(Name, 'ADirectionalLightActor', [Component('UDirectionalLightComponent', 'UDirectionalLightComponent', { Transform: Transform(Location, [1, 1, 1], Angles), Intensity, LightColor: Color, bCastShadows: Name === 'Stage Key', AngularRadiusDegrees: 2, DynamicShadowCascades: 4, DynamicShadowDistanceMovableLight: 100, ShadowBias: 0.5, ShadowSlopeBias: 0.7 })]);

const CameraRoot = Component('USceneComponent', 'DefaultSceneRoot', { Transform: Transform([-9, 0, 0]) });
Actor('Stage Camera', 'APawn', [CameraRoot, Component('UCameraComponent', 'CameraComponent', { Transform: Transform([-5.4, 0, 3], [1, 1, 1], [-18, 0, 0]), FieldOfView: 62 }, CameraRoot.Id)]);

Box('Foundation West', [-6.5, 0, -0.35], [23, 40, 0.7], Colors.Floor, true);
Box('Foundation East', [23.5, 0, -0.35], [21, 40, 0.7], Colors.Floor, true);
Box('Foundation South', [9, -5, -0.35], [8, 30, 0.7], Colors.Floor, true);
Box('Foundation North', [9, 18.5, -0.35], [8, 3, 0.7], Colors.Floor, true);
Box('Pool Basin', [9, 13.5, -1.35], [8, 7, 0.7], Colors.Water, true);
Box('Pool Surface', [9, 13.5, -0.2], [7.9, 6.9, 0.06], Colors.Water);
Box('Arena Plinth', [8, 0, -0.85], [53, 41, 0.25], Colors.Navy);

for (let X = -16; X <= 32; X += 4) if (X < 5 || X > 13) Box(`Grid X ${X}`, [X, 0, 0.006], [0.022, 39, 0.012], [0.17, 0.21, 0.25]);
for (let Y = -18; Y <= 18; Y += 4) Box(`Grid Y ${Y}`, [7, Y, 0.008], [50, 0.022, 0.012], [0.17, 0.21, 0.25]);
for (const Y of [-19.8, 19.8]) Box(`Perimeter Y ${Y}`, [8, Y, 0.13], [52, 0.12, 0.26], Colors.Cyan);
for (const X of [-17.8, 33.8]) Box(`Perimeter X ${X}`, [X, 0, 0.13], [0.12, 40, 0.26], Colors.Cyan);

Box('Stage Backdrop', [29, 0, 3.1], [0.35, 37, 6.2], Colors.Navy);
Box('Backdrop Stripe', [28.78, 0, 0.45], [0.04, 37, 0.16], Colors.Cyan);
Text('Stage Wordmark', 'LOCOMOTION  /  STAGE', [28.73, 0, 4.5], 1.1);
Text('Stage Subtitle', '01  FLOW     02  TRAVERSE     03  COMBAT     04  ROOT MOTION     05  CLIP LAB', [28.7, 0, 3.35], 0.23);
Text('Stage Credits', 'UNIVERSAL ANIMATION LIBRARY  /  QUATERNIUS  /  43 MOTIONS', [28.7, 0, 2.65], 0.27);

for (const [Index, Title, X, Y, Color] of [[1, 'FLOW', -8, 0, Colors.Cyan], [2, 'TRAVERSE', -2, -12, Colors.Cyan], [3, 'COMBAT', 14, 6, Colors.Orange], [4, 'ROOT MOTION', -8, -6, Colors.Lime], [5, 'CLIP LAB', -8, 12, Colors.Violet]]) {
    Box(`Zone ${Index} Edge`, [X - 2.5, Y, 0.018], [0.12, 5.5, 0.035], Color);
    const SignY = Y + (Index === 1 ? 4.5 : -4.8);
    Box(`Zone ${Index} Sign`, [X + 1.5, SignY, 0.75], [0.12, 3.2, 1.5], Colors.Navy);
    Text(`Zone ${Index} Title`, `${String(Index).padStart(2, '0')}  /  ${Title}`, [X + 1.42, SignY, 1.0], 0.26);
    Text(`Zone ${Index} Hint`, `PRESS ${Index} TO ENTER`, [X + 1.41, SignY, 0.5], 0.14);
}

for (const Y of [-8.1, -4.1]) Box(`Root Track ${Y}`, [10, Y, 0.025], [38, 0.065, 0.045], Colors.Lime);
for (let X = -10; X <= 28; X += 2) Box(`Root Meter ${X}`, [X, -8.3, 0.03], [0.055, 0.4, 0.05], Colors.Lime);
Text('Root Track Note', 'DISTANCE IS EXTRACTED FROM THE ROOT TRACK', [22, -8.8, 0.65], 0.18);

Box('Traverse Ramp', [4, -12, 0.54], [6.1, 4.3, 0.25], Colors.Pale, true, [-11.3, 0, 0]);
Box('Traverse Takeoff', [8.1, -12, 0.6], [2.3, 4.3, 1.2], Colors.Pale, true);
Box('Traverse Landing', [13.4, -12, 0.6], [3.4, 4.3, 1.2], Colors.Pale, true);
for (let Step = 0; Step < 4; ++Step) Box(`Traverse Step ${Step}`, [15.8 + Step * 1.25, -12, (1.2 - Step * 0.3) / 2], [1.25, 4.3, 1.2 - Step * 0.3], Colors.Pale, true);
for (const X of [7.4, 9.1, 11.9, 14.9]) Box(`Jump Safety Stripe ${X}`, [X, -12, 1.215], [0.12, 4.1, 0.035], Colors.Cyan);
Text('Traverse Instructions', 'RAMP  /  SHIFT + SPACE TO CLEAR THE GAP  /  STEPS', [22, -14.3, 0.8], 0.21);

for (const Y of [9.5, 14.5]) Box(`Tunnel Wall ${Y}`, [0.5, Y, 0.75], [6, 0.25, 1.5], Colors.Pale, true);
Box('Crouch Tunnel Roof', [0.5, 12, 1.5], [6, 5.25, 0.22], Colors.Pale, true);
Box('Crouch Entry Stripe', [-2.6, 12, 1.4], [0.08, 5.1, 0.16], Colors.Cyan);
Text('Crouch Warning', 'C  /  LOW CLEARANCE', [-2.72, 10.1, 1.75], 0.2);
for (const Y of [10, 17]) Box(`Pool Rim ${Y}`, [9, Y, 0.06], [8.1, 0.12, 0.12], Colors.Cyan);
Text('Pool Sign', 'SWIM  /  ENTER THE BASIN', [13.7, 10.2, 0.7], 0.19);

Box('Combat Floor', [17, 5, 0.018], [10, 9, 0.035], [0.17, 0.12, 0.105]);
for (const Y of [0.6, 9.4]) Box(`Combat Stripe ${Y}`, [17, Y, 0.045], [10, 0.08, 0.05], Colors.Orange);
Box('Interaction Bench Seat', [17, 9, 0.48], [3, 0.65, 0.15], Colors.Pale, true);
for (const X of [16, 18]) Box(`Bench Leg ${X}`, [X, 9, 0.23], [0.12, 0.5, 0.46], Colors.Navy, true);
Text('Combat Prompt', 'LMB  PUNCH     Q  ROLL     X  SWORD     K  SPELL', [23, 0.8, 1.1], 0.19);
Text('Interaction Prompt', 'E  INTERACT     R  RELOAD     U  SIT / STAND', [23, 0.8, 0.65], 0.19);

Character('Stage Player', [-9, 0, 0.03], [0.68, 0.9, 0.9], 'Scripts/locomotion_stage.lua');
for (const [Name, X, Y, Color, Kind] of [['Flow Demonstrator', 21.5, -17, Colors.Cyan, 'InPlace'], ['Action Demonstrator', 23, 5, Colors.Orange, 'InPlace'], ['Aim Demonstrator', 23, 12.5, Colors.Violet, 'InPlace'], ['Root Demonstrator', 7, -6, Colors.Lime, 'RootMotion']]) {
    Character(Name, [X, Y, 0.03], Color, 'Scripts/locomotion_stage_demo.lua', Kind, 180);
    Box(`${Name} Plinth`, [X, Y, 0.005], [3.4, 3.4, 0.01], Colors.Navy);
    Text(`${Name} Label`, Name.replace(' Demonstrator', '').toUpperCase(), [X + 1.4, Y - 1.6, 2.8], 0.28);
}

// 구역마다 위에서 내려 비추는 Spot과 포인트 강조광. FColor는 { R, G, B, A } 바이트를 리틀 엔디언 uint32로 저장한다.
const LightColor = ([R, G, B]) => ((255 << 24) | (B << 16) | (G << 8) | R) >>> 0;
function Light(Kind, Name, Location, Rgb, Intensity, AttenuationRadius, bCastShadows, Cone) {
    const Properties = { Transform: Transform(Location, [1, 1, 1], Kind === 'Spot' ? [89, 0, 0] : [0, 0, 0]), Intensity, LightColor: LightColor(Rgb), bCastShadows, AttenuationRadius, LightFalloffExponent: 1, SourceRadius: 0.02, ShadowResolutionScale: 1, ShadowBias: 0.5, ShadowSlopeBias: 0.5, ShadowSharpen: 0 };
    if (Cone) [Properties.InnerConeAngle, Properties.OuterConeAngle] = Cone;
    Actor(`Light / ${Name}`, `A${Kind}LightActor`, [Component(`U${Kind}LightComponent`, `U${Kind}LightComponent`, Properties)]);
}
for (const [Name, Location, Rgb, Intensity, Radius, Shadows, Cone] of [
    ['Flow Spot', [-9, 0, 7.5], [255, 236, 214], 160, 14, true, [30, 44]],
    ['Traverse Spot Ramp', [6, -12, 7.5], [220, 232, 255], 150, 14, false, [28, 42]],
    ['Traverse Spot Steps', [15.5, -12, 7.5], [220, 232, 255], 150, 14, true, [28, 42]],
    ['Combat Spot', [17, 5, 8], [255, 214, 176], 200, 16, true, [32, 46]],
    ['Root Track Spot', [8, -6, 7], [190, 235, 255], 140, 13, false, [30, 44]],
    ['Clip Lab Spot', [-8, 12, 7], [245, 245, 255], 150, 13, true, [30, 44]]
]) Light('Spot', Name, Location, Rgb, Intensity, Radius, Shadows, Cone);
for (const [Name, Location, Rgb, Intensity, Radius] of [
    ['Pool Glow', [9, 13.5, 0.6], [80, 200, 255], 3, 7],
    ['Crouch Tunnel Lamp', [0.5, 12, 1.05], [255, 170, 90], 2, 5],
    ['Backdrop Wash North', [26.5, 9, 3.5], [255, 90, 200], 4, 10],
    ['Backdrop Wash South', [26.5, -9, 3.5], [90, 140, 255], 4, 10],
    ['Flow Demonstrator Lamp', [21.5, -17, 3], [150, 255, 180], 2.5, 7],
    ['Action Demonstrator Lamp', [23, 5, 3], [255, 200, 120], 2.5, 7],
    ['Aim Demonstrator Lamp', [23, 12.5, 3], [190, 150, 255], 2.5, 7],
    ['Bench Lamp', [17, 9.8, 1.6], [255, 220, 170], 1.5, 4]
]) Light('Point', Name, Location, Rgb, Intensity, Radius, false);

Save('Scenes/Demo_LocomotionStage.scene', { Version: 5, World: { Properties: {}, PersistentLevel: { Properties: {}, Actors } } });

const Hud = { Version: 2, DesignSize: [1920, 1080], ParentClass: 'UUserWidget', Root: { Class: 'UPanelWidget', Properties: { WidgetName: 'Root', Anchors: { X: 0, Y: 0, Z: 1, W: 1 }, Offsets: { X: 0, Y: 0, Z: 0, W: 0 }, AlignmentX: 0, AlignmentY: 0, RenderOpacity: 1, Visibility: 3, ZOrder: 0, bAutoSize: false }, Children: [] } };

function Widget(Class, Name, X, Y, Width, Height, Extra, Anchors = { X: 0, Y: 0, Z: 0, W: 0 }) {
    Hud.Root.Children.push({ Class, Properties: { WidgetName: Name, Anchors, Offsets: { X, Y, Z: Width, W: Height }, AlignmentX: 0, AlignmentY: 0, RenderOpacity: 1, Visibility: 3, ZOrder: Class === 'UImage' ? 0 : 2, bAutoSize: false, ...Extra } });
}

const RGBA = (Color, A = 1) => ({ R: Color[0], G: Color[1], B: Color[2], A });
const Label = (Name, Text, X, Y, Width, Size, Color = [0.91, 0.95, 0.96], Anchors) => Widget('UTextBlock', Name, X, Y, Width, Size * 1.5, { Text, FontSize: Size, Justification: 0, FontPath: 'Assets/Fonts/Pretendard.json', ColorAndOpacity: RGBA(Color) }, Anchors);
Widget('UImage', 'Top Backdrop', 24, 22, 610, 118, { ColorAndOpacity: RGBA(Colors.Navy, 0.93), TexturePath: '' });
Widget('UImage', 'Top Accent', 24, 22, 5, 118, { ColorAndOpacity: RGBA(Colors.Cyan), TexturePath: '' });
Label('Brand', 'MOTION / FIELD', 48, 33, 540, 17, [0.3, 0.86, 0.83]);
Label('Title', 'Locomotion Stage', 48, 54, 555, 35);
Label('Subtitle', '43 CLIPS   /   IN-PLACE + ROOT MOTION', 49, 103, 560, 15, [0.6, 0.69, 0.75]);
Widget('UImage', 'Telemetry Backdrop', -408, 22, 384, 193, { ColorAndOpacity: RGBA(Colors.Navy, 0.93), TexturePath: '' }, { X: 1, Y: 0, Z: 1, W: 0 });
for (const [Name, Text, Y, Size, Color] of [['State', 'MOVEMENT / GROUNDED', 37, 20, [0.3, 0.86, 0.83]], ['Speed', '0.00 m/s', 72, 30, [0.94, 0.97, 0.98]], ['Motion', 'IN-PLACE  /  NATIVE GRAPH', 116, 16, [0.64, 0.73, 0.8]], ['Notify', 'NOTIFY  /  READY', 145, 16, [0.94, 0.58, 0.26]], ['Window', 'ATTACK WINDOW  /  --', 174, 15, [0.94, 0.58, 0.26]]]) Label(Name, Text, -386, Y, 343, Size, Color, { X: 1, Y: 0, Z: 1, W: 0 });
Widget('UImage', 'Navigation Backdrop', 24, -140, 965, 116, { ColorAndOpacity: RGBA(Colors.Navy, 0.92), TexturePath: '' }, { X: 0, Y: 1, Z: 0, W: 1 });
Label('Zone', '01 / FLOW', 48, -132, 900, 24, [0.3, 0.86, 0.83], { X: 0, Y: 1, Z: 0, W: 1 });
Label('Controls', 'WASD MOVE   SHIFT SPRINT   Z WALK   SPACE JUMP   C CROUCH   RMB/T AIM', 48, -92, 915, 17, undefined, { X: 0, Y: 1, Z: 0, W: 1 });
Label('Navigation', '1 FLOW   2 TRAVERSE   3 COMBAT   4 ROOT MOTION   5 CLIP LAB   I HELP', 48, -59, 915, 15, [0.61, 0.71, 0.76], { X: 0, Y: 1, Z: 0, W: 1 });
Widget('UImage', 'Hint Backdrop', 650, 22, 760, 43, { ColorAndOpacity: RGBA(Colors.Navy, 0.9), TexturePath: '' });
Label('Hint', 'EXPLORE THE STAGE / 1-5 QUICK TRAVEL', 670, 32, 720, 16, [0.71, 0.8, 0.84]);
Widget('UImage', 'ClipBackdrop', 24, 150, 1150, 52, { ColorAndOpacity: RGBA(Colors.Navy, 0.93), TexturePath: '', Visibility: 1 });
Label('Clip', '', 48, 155, 1150, 22, [0.8, 0.7, 1]);
Widget('UImage', 'HelpBackdrop', 24, 190, 1040, 410, { ColorAndOpacity: RGBA(Colors.Navy, 0.96), TexturePath: '', Visibility: 1 });
Label('Help', '', 48, 208, 1000, 23, [0.3, 0.86, 0.83]);
const HelpRows = [
    ['LOCOMOTION', 48, 254, 16], ['WASD Move / Shift Sprint / Z Walk', 48, 288, 18],
    ['Space Jump / C Crouch / T or RMB Aim', 48, 318, 18], ['Mouse or arrows Orbit / V Swim', 48, 348, 18],
    ['Q Roll / E Interact / R Reload', 48, 388, 18], ['LMB Punch or Fire / X Sword / K Spell', 48, 418, 18],
    ['H Hit / U Sit / F Dance / Y Death', 48, 448, 18], ['Backspace Reset / B Guided Demo', 48, 488, 18],
    ['CLIP LAB', 560, 254, 16], ['5 or Tab Open / Close', 560, 288, 18],
    ['Q / E Previous / Next clip', 560, 318, 18], ['P Pause / N Reverse / O Speed', 560, 348, 18],
    ['J / L Seek -10% / +10%', 560, 388, 18], ['M In-place / Root Motion', 560, 418, 18],
    ['1-5 Quick travel between stage zones', 560, 488, 18]
];
HelpRows.forEach(([Text, X, Y, Size], Index) => {
    Label('HelpLine' + (Index + 1), Text, X, Y, 470, Size, [0.84, 0.89, 0.93]);
    Hud.Root.Children.at(-1).Properties.Visibility = 1;
});
Save(`${Folder}/StageHUD.widget`, Hud);
Fs.copyFileSync(Path.join(Root, 'Assets/StateMachineLab/License.txt'), Path.join(Root, Folder, 'License.txt'));
console.log(`Generated Locomotion Stage v5: ${Actors.length} actors, ${Clips.length} clips per library, two nested graphs, HUD and notify tracks.`);
