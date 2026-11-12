import * as Fs from 'node:fs/promises';
import * as Path from 'node:path';
import { fileURLToPath as FileURLToPath } from 'node:url';

const Root = Path.resolve(Path.dirname(FileURLToPath(import.meta.url)), '..');
const ScenePath = Path.join(Root, 'Scenes/DevilDuel.scene');
const Scene = JSON.parse(await Fs.readFile(ScenePath, 'utf8'));
const Meshes = JSON.parse(await Fs.readFile(Path.join(Root, 'Assets/DevilDuel/Environment/Meshes.json'), 'utf8'));
const Actors = Scene.World.PersistentLevel.Actors.filter(Actor => ['Duel Player', 'Duel Rival', 'Duel Camera', 'Duel Director'].includes(Actor.Name));
let Id = 10000;
const Vector = (X = 0, Y = 0, Z = 0) => ({ X, Y, Z });
const Rotation = (Yaw = 0, Pitch = 0, Roll = 0) => ({ Pitch, Yaw, Roll });
const Transform = (Location = Vector(), Yaw = 0, Scale = Vector(1, 1, 1), Pitch = 0, Roll = 0) => ({ Location, Rotation: Rotation(Yaw, Pitch, Roll), Scale });

function Component(Class, Name, Parent, Properties) {
    return { Class, Name, Id: Id++, Outer: 0, Parent, Properties: { bAutoActivate: true, bVisible: true, bHiddenInGame: false, ...Properties } };
}

function Actor(Class, Name, Components) {
    const Result = { Class, Name, Components, RootComponent: Components[0].Id, Properties: { ScriptPath: '', bActorEnableCollision: false } };
    Actors.push(Result);
    return Result;
}

function Material(Source, Index = 0, Tint = [1, 1, 1]) {
    const Name = Source.Name.toLowerCase();
    const IsNeon = Name.includes('neon');
    const IsSky = Name.includes('night_sky');
    const IsBulb = Name.includes('bulb');
    const IsGlass = Name.includes('glass');
    const IsRoad = Name.includes('asphalt');
    const Color = IsNeon ? Source.Color.map(Value => Value * 1.7) : IsSky ? [0.34, 0.38, 0.50] : Source.Diffuse ? Tint : Source.Color;
    const Emission = IsBulb ? [0.8, 0.55, 0.24] : IsGlass ? Index % 3 === 0 ? [0.06, 0.03, 0.015] : [0.015, 0.025, 0.035] : [0, 0, 0];
    return { Base: 'DefaultMaterial', Instance: { BaseColor: { X: Color[0], Y: Color[1], Z: Color[2], W: 1 }, BlendState: 'Opaque', FlipNormalY: false, PSOType: 0, Properties: {}, SamplerState: 'LinearWrap', ShadingModel: IsNeon || IsSky || IsBulb ? 'Unlit' : 'BlinnPhong', EmissiveColor: Vector(...Emission), Shininess: IsRoad ? 120 : 36, SpecularColor: IsRoad ? Vector(.65, .70, .78) : Vector(.16, .17, .19), Textures: [Source.Diffuse ? 'Assets/DevilDuel/Environment/Textures/' + Source.Diffuse : 'WhiteTexture', Source.Normal ? 'Assets/DevilDuel/Environment/Textures/' + Source.Normal : 'FlatNormalTexture'], UVScrollSpeed: [0, 0] } };
}

const StreetRoot = Component('USceneComponent', 'DefaultSceneRoot', 0, { Transform: Transform() });
const StreetParts = [StreetRoot];
let PartIndex = 0;

function Model(Source, Name, Location, Yaw = 0, Scale = Vector(1, 1, 1), Pitch = 0, Roll = 0, Tint = [1, 1, 1]) {
    const Mesh = Meshes.find(Item => Item.Source === Source && Item.Name === Name);
    if (!Mesh) {
        throw new Error('Missing alley mesh: ' + Source + '/' + Name);
    }
    const IsSky = Name === 'Night_Sky';
    const IsNeon = Name.startsWith('Neon_');
    const Result = Component('UStaticMeshComponent', Name + '_' + PartIndex++, StreetRoot.Id, { StaticMesh: Mesh.Path, Transform: Transform(Location, Yaw, Scale, Pitch, Roll), bCastShadow: !IsSky && !IsNeon, bReceiveShadow: !IsSky && !IsNeon, bGenerateOverlapEvents: false, bBlockComponent: false, bReceivesDecals: !IsSky });
    Result.OverrideMaterials = [Material(Mesh.Materials[0], PartIndex, Tint), ...Mesh.Materials.map((Source, Index) => Material(Source, Index + PartIndex, Tint))];
    StreetParts.push(Result);
    return Result;
}

function Urban(Name, Location, Yaw = 0, Tint = [1, 1, 1]) {
    return Model('modular_urban_apartments_facade', Name, Location, Yaw, Vector(1, 1, 1), 0, 0, Tint);
}

Model('Authored', 'Street_Surface', Vector(0, 0, 0));
Model('Authored', 'Sidewalk', Vector(0, -7.65, .15));
Model('Authored', 'Sidewalk', Vector(0, 14.65, .15), 180);
Model('Authored', 'Night_Sky', Vector(0, 0, 0));

for (const [Line, Yaw, Tint] of [[-8.6, 180, [.69, .75, .83]], [15.6, 0, [.72, .69, .64]]]) {
    for (let Bay = 0; Bay < 8; Bay++) {
        const X = Yaw === 180 ? -12 + Bay * 3 : -9 + Bay * 3;
        for (let Floor = 0; Floor < 4; Floor++) {
            const Door = Floor === 0 && Bay % 3 !== 1;
            const Wall = Door ? 'wall_door_centered_large_01' : Floor === 0 ? 'wall_standard_standard_01' : 'wall_window_centered_large_01';
            Urban(Wall, Vector(X, Line, Floor * 3 + .15), Yaw, Tint);
            if (Door) {
                Urban('door_centered_large_01', Vector(X, Line, .15), Yaw);
            } else if (Floor > 0) {
                Urban('window_centered_large_01', Vector(X, Line, Floor * 3 + .15), Yaw);
            }
            Urban('cornice_standard_standard_01', Vector(X, Line, Floor * 3 + 3.05), Yaw);
        }
        Urban('crown_standard_standard_01', Vector(X, Line, 12.15), Yaw);
        if (Bay % 3 === 1) {
            Model('rollershutter_door', Bay % 2 ? 'rollershutter_door_graffiti' : 'rollershutter_door', Vector(X + (Yaw === 180 ? 1.5 : -1.5), Line + (Yaw === 180 ? .12 : -.12), .15), Yaw, Vector(2.25, 1, 1.1));
        }
        if (Bay % 2 === 0) {
            Model('exterior_aircon_unit', Bay === 0 ? 'exterior_aircon_unit_rusted' : 'exterior_aircon_unit', Vector(X + (Yaw === 180 ? 2.0 : -2.0), Line + (Yaw === 180 ? .38 : -.38), 3.6 + (Bay % 3) * 3), Yaw);
        }
    }
}

for (const [X, Yaw] of [[-16, 90], [16, -90]]) {
    for (let Bay = 0; Bay < 8; Bay++) {
        const Y = Yaw === 90 ? -9 + Bay * 3 : -6 + Bay * 3;
        for (let Floor = 0; Floor < 3; Floor++) {
            Urban(Floor === 0 ? 'wall_standard_standard_01' : 'wall_window_centered_large_01', Vector(X, Y, Floor * 3 + .15), Yaw, [.70, .73, .78]);
            if (Floor > 0) {
                Urban('window_centered_large_01', Vector(X, Y, Floor * 3 + .15), Yaw);
            }
        }
        Urban('crown_standard_standard_01', Vector(X, Y, 9.15), Yaw);
    }
}

for (const [Name, X, Y, Z, Yaw] of [['Neon_Arcade', -7.5, -8.05, 3.7, 180], ['Neon_Ramen', 1.3, -8.05, 3.45, 180], ['Neon_Karaoke', 8.7, -8.0, 5.4, 180], ['Neon_Dragon', -15.4, 1.0, 4.0, 90], ['Neon_Open', 2.0, -8.05, 1.95, 180], ['Neon_Karaoke', 3.5, 15.05, 4.8, 0], ['Neon_Ramen', 15.45, 4.0, 3.5, -90]]) {
    Model('Authored', Name, Vector(X, Y, Z), Yaw);
    Model('Authored', Name + '_Frame', Vector(X, Y, Z), Yaw);
}

for (const X of [-10.5, -1.5, 10.5]) {
    for (let Segment = 0; Segment < 4; Segment++) {
        Model('modular_industrial_pipes_01', 'modular_industrial_pipes_01_pipe02', Vector(X, -8.20, .7 + Segment * 1.95), 180);
    }
    Model('modular_industrial_pipes_01', 'modular_industrial_pipes_01_pipe04', Vector(X, -8.20, 8.1), 180);
}

for (const X of [-6, 3, 9]) {
    Model('modular_electric_cables', 'cable_box_3way', Vector(X, -8.1, 2.5), 180, Vector(2, 2, 2));
    for (let Segment = 0; Segment < 5; Segment++) {
        Model('modular_electric_cables', 'cable_straight_long', Vector(X, -8.1, 2.8 + Segment * .6), 180);
    }
}
for (let Segment = 0; Segment < 6; Segment++) {
    Model('modular_electric_cables', 'cable_straight_long', Vector(-12 + Segment * 3.65, -8.0, 6.7), 0, Vector(1.8, 1.8, 6), 90);
}

for (const [X, Y] of [[-9.8, -6.2], [10.8, -6.2], [-11.0, 10.3], [11.0, 10.3]]) {
    Model('street_lamp_01', 'street_lamp_01', Vector(X, Y, .15));
}

const TrashSource = JSON.parse(await Fs.readFile(Path.join(Root, 'Build/DevilDuelSources/metal_trash_can/metal_trash_can.gltf'), 'utf8'));
const TrashBase = TrashSource.nodes.find(Node => Node.name === 'metal_trash_can').translation ?? [0, 0, 0];
for (const [X, Y, Yaw] of [[-5.2, -6.8, 13], [6.0, -6.7, -18], [10.0, 12.0, 35]]) {
    for (const Name of ['metal_trash_can', 'metal_trash_can_lid', 'metal_trash_can_handle_left', 'metal_trash_can_handle_right']) {
        const Node = TrashSource.nodes.find(Node => Node.name === Name);
        const Position = Node.translation ?? [0, 0, 0];
        Model('metal_trash_can', Name, Vector(X + Position[0] - TrashBase[0], Y - Position[2] + TrashBase[2], .15 + Position[1] - TrashBase[1]), Yaw);
    }
    for (let Bag = 0; Bag < 3; Bag++) {
        Model('trashbag', 'trashbag', Vector(X - .7 + Bag * .38, Y + .6, .13), Bag * 121 + Yaw, Vector(1, 1, 1 + Bag * .15));
    }
    for (let Box = 0; Box < 3; Box++) {
        Model('plastic_crate_01', 'plastic_crate_01', Vector(X + .9, Y, .16 + Box * .35), Yaw, Vector(1.3, 1.3, 1.3));
    }
}

Actor('AActor', 'Neon Backstreet', StreetParts);

function PackedColor(R, G, B) {
    return (R | G << 8 | B << 16 | 255 << 24) >>> 0;
}

function Light(Name, Position, Color, Intensity, Radius, Directional = false, Yaw = 0, Pitch = 0) {
    const Root = Component('USceneComponent', 'DefaultSceneRoot', 0, { Transform: Transform(Position) });
    const Lamp = Component(Directional ? 'UDirectionalLightComponent' : 'UPointLightComponent', 'AlleyLight', Root.Id, { Transform: Transform(Vector(), Yaw, Vector(1, 1, 1), Pitch), LightColor: PackedColor(...Color), Intensity, AttenuationRadius: Radius, LightFalloffExponent: 2, bCastShadows: Directional, ShadowBias: 1.0, ShadowSlopeBias: 1.0, DynamicShadowCascades: 2, DynamicShadowDistanceMovableLight: 42, AngularRadiusDegrees: 2 });
    Actor('AActor', Name, [Root, Lamp]);
}

Light('Moonlight', Vector(0, 0, 15), [110, 150, 225], .42, 0, true, -35, 55);
Light('Street Fill', Vector(0, 4, 7), [120, 155, 220], 1.1, 22);
for (const [Name, X, Y, Z, Color, Intensity, Radius] of [['Arcade Cyan', -6.2, -6.8, 3.5, [25, 180, 255], 6.2, 17], ['Ramen Amber', 1.0, -6.9, 3.1, [255, 130, 45], 4.2, 14], ['Karaoke Magenta', 8.0, -6.6, 4.2, [255, 30, 130], 6.0, 18], ['Left Violet', -12.0, 1.5, 4.5, [145, 45, 255], 5.0, 16], ['Front Rose', 3.3, 12.0, 4.0, [255, 50, 145], 4.0, 18]]) {
    Light(Name, Vector(X, Y, Z), Color, Intensity, Radius);
}
for (const [X, Y] of [[-9.8, -6.2], [10.8, -6.2], [-11, 10.3], [11, 10.3]]) {
    Light('Streetlamp ' + X + ' ' + Y, Vector(X, Y, 3.5), [255, 190, 115], 2.5, 8);
}
const Ambient = Component('USkyLightComponent', 'AlleyAmbient', 0, { Transform: Transform(), LightColor: PackedColor(115, 140, 190), Intensity: .28, bCastShadows: false });
Actor('AActor', 'Night Ambient', [Ambient]);
const Fog = Component('UExponentialHeightFogComponent', 'AlleyMist', 0, { Transform: Transform(Vector(0, 0, -.3)), FogDensity: .008, FogHeightFalloff: .2, FogColor: { R: .025, G: .035, B: .07, A: 1 }, StartDistance: 9, FogMaxOpacity: .65 });
Actor('AActor', 'Street Haze', [Fog]);

for (const Name of ['Player Marker', 'Rival Marker']) {
    Actor('AActor', Name, [Component('USceneComponent', 'DefaultSceneRoot', 0, { Transform: Transform(), bHiddenInGame: true })]);
}
for (const Fighter of Actors.filter(Actor => Actor.Class === 'ASkeletalMeshActor')) {
    const Mesh = Fighter.Components.find(Component => Component.Class === 'USkeletalMeshComponent');
    Mesh.Properties.bEnableRootMotion = true;
    delete Mesh.Properties.bApplyRootMotionToActor;
    Mesh.Properties.mApplyRootMotionToActor = false;
    Mesh.Properties.AnimToPlay = 'DevilDuel/Motion/Bouncing Fight Idle.fbxbin';
}
Scene.World.PersistentLevel.Actors = Actors;
await Fs.writeFile(ScenePath, JSON.stringify(Scene, null, 4) + '\n');
process.stdout.write(`Built the neon backstreet from ${StreetParts.length - 1} mesh instances and ${Actors.length} actors\n`);
