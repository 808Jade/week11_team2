import bpy
import json
import math
import pathlib
import re
import shutil
import sys
from mathutils import Vector

Root = pathlib.Path(__file__).resolve().parent.parent
Output = Root / "Assets/DevilDuel/Environment"
Sources = json.loads((Output / "Sources.json").read_text(encoding="utf-8"))
Meshes = []
PreviousMeshes = json.loads((Output / "Meshes.json").read_text(encoding="utf-8")) if (Output / "Meshes.json").is_file() else []
SelectedModels = {
    "modular_urban_apartments_facade": {"door_centered_large_01", "cornice_standard_standard_01", "crown_standard_standard_01", "wall_door_centered_large_01", "wall_window_centered_large_01", "wall_standard_standard_01", "window_centered_large_01"},
    "rollershutter_door": {"rollershutter_door", "rollershutter_door_graffiti"},
    "modular_industrial_pipes_01": {"modular_industrial_pipes_01_pipe02", "modular_industrial_pipes_01_pipe04"},
    "modular_electric_cables": {"cable_straight_long", "cable_box_3way"},
    "exterior_aircon_unit": {"exterior_aircon_unit", "exterior_aircon_unit_rusted"},
    "street_lamp_01": {"street_lamp_01"},
    "metal_trash_can": {"metal_trash_can_handle_left", "metal_trash_can_lid", "metal_trash_can", "metal_trash_can_handle_right"},
    "trashbag": {"trashbag"},
    "plastic_crate_01": {"plastic_crate_01"}
}

def SafeName(Name):
    return re.sub(r"[^A-Za-z0-9_.-]", "_", Name)

def FindImage(Socket, Visited=None):
    Visited = set() if Visited is None else Visited
    for Link in Socket.links:
        Node = Link.from_node
        if Node in Visited:
            continue
        Visited.add(Node)
        if Node.type == "TEX_IMAGE" and Node.image:
            return Node.image
        for Input in Node.inputs:
            Image = FindImage(Input, Visited)
            if Image:
                return Image
    return None

def CopyImage(Image):
    Source = pathlib.Path(bpy.path.abspath(Image.filepath))
    Destination = Output / "Textures" / Source.name
    Destination.parent.mkdir(parents=True, exist_ok=True)
    if Source.resolve() != Destination.resolve():
        shutil.copy2(Source, Destination)
    return Destination.name

def ExportMesh(Object, AssetId):
    Name = SafeName(Object.name)
    Directory = Output / "Models"
    Directory.mkdir(parents=True, exist_ok=True)
    FileName = AssetId + "__" + Name
    Evaluated = Object.evaluated_get(bpy.context.evaluated_depsgraph_get())
    Mesh = Evaluated.to_mesh()
    Mesh.calc_loop_triangles()
    Rotation = Object.matrix_world.to_3x3()
    NormalMatrix = Rotation.inverted().transposed()
    Positions = [Rotation @ Vertex.co for Vertex in Mesh.vertices]
    Minimum = [min(Position[Axis] for Position in Positions) for Axis in range(3)]
    Maximum = [max(Position[Axis] for Position in Positions) for Axis in range(3)]
    Lines = ["mtllib " + FileName + ".mtl", "o " + Name]
    MaterialLines = []
    MaterialInfo = []
    for Index, Slot in enumerate(Object.material_slots):
        Material = Slot.material
        MaterialName = "Material_" + str(Index)
        Diffuse = None
        Normal = None
        Color = [0.3, 0.35, 0.4]
        if Material and Material.use_nodes:
            Principled = next((Node for Node in Material.node_tree.nodes if Node.type == "BSDF_PRINCIPLED"), None)
            if Principled:
                Color = list(Principled.inputs["Base Color"].default_value[:3])
                DiffuseImage = FindImage(Principled.inputs["Base Color"])
                NormalImage = FindImage(Principled.inputs["Normal"])
                Diffuse = CopyImage(DiffuseImage) if DiffuseImage else None
                Normal = CopyImage(NormalImage) if NormalImage else None
        MaterialLines.extend(["newmtl " + MaterialName, "Kd " + " ".join(str(Value) for Value in (Color if not Diffuse else [1.0, 1.0, 1.0])), "d 1", "Ns 48"])
        if Diffuse:
            MaterialLines.append("map_Kd ../Textures/" + Diffuse)
        if Normal:
            MaterialLines.append("map_bump ../Textures/" + Normal)
        MaterialInfo.append({"Name": Material.name if Material else MaterialName, "Diffuse": Diffuse, "Normal": Normal, "Color": Color})
    UVs = Mesh.uv_layers.active.data if Mesh.uv_layers.active else None
    for Loop in Mesh.loops:
        Position = Positions[Loop.vertex_index]
        Lines.append(f"v {Position.y:.7f} {Position.z:.7f} {-Position.x:.7f}")
    for Loop in Mesh.loops:
        UV = UVs[Loop.index].uv if UVs else (0.0, 0.0)
        Lines.append(f"vt {UV[0]:.7f} {UV[1]:.7f}")
    for Loop in Mesh.loops:
        Normal = (NormalMatrix @ Mesh.corner_normals[Loop.index].vector).normalized()
        Lines.append(f"vn {Normal.y:.7f} {Normal.z:.7f} {-Normal.x:.7f}")
    PreviousMaterial = -1
    for Triangle in Mesh.loop_triangles:
        if Triangle.material_index != PreviousMaterial:
            PreviousMaterial = Triangle.material_index
            Lines.append("usemtl Material_" + str(PreviousMaterial))
        Order = (0, 2, 1) if Rotation.determinant() >= 0.0 else (0, 1, 2)
        Indices = [Triangle.loops[Index] + 1 for Index in Order]
        Lines.append("f " + " ".join(f"{Index}/{Index}/{Index}" for Index in Indices))
    (Directory / (FileName + ".obj")).write_text("\n".join(Lines) + "\n", encoding="utf-8")
    (Directory / (FileName + ".mtl")).write_text("\n".join(MaterialLines) + "\n", encoding="utf-8")
    (Directory / (FileName + ".obj.bin")).unlink(missing_ok=True)
    Result = {"Source": AssetId, "Name": Name, "Path": "DevilDuel/Environment/Models/" + FileName + ".obj", "Min": Minimum, "Max": Maximum, "Triangles": len(Mesh.loop_triangles), "Materials": MaterialInfo}
    Evaluated.to_mesh_clear()
    return Result

for Source in Sources:
    if Source["Kind"] == "Font":
        FontDirectory = Output / "Fonts"
        FontDirectory.mkdir(parents=True, exist_ok=True)
        shutil.copy2(Root / Source["Input"], FontDirectory / "Pretendard-Bold.otf")
        shutil.copy2((Root / Source["Input"]).parent / "OFL.txt", FontDirectory / "OFL.txt")
        continue
    if Source["Kind"] == "Sky":
        shutil.copy2(Root / Source["Input"], Output / "Textures" / "NightCity.jpg")
        continue
    if Source["Kind"] == "Texture":
        for Name, File in Source["Input"].items():
            Destination = Output / "Textures" / pathlib.Path(File).name
            Destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(Root / File, Destination)
        continue
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(Root / Source["Input"]))
    for Object in list(bpy.context.scene.objects):
        if Object.type == "MESH" and Object.name in SelectedModels[Source["Id"]]:
            Meshes.append(ExportMesh(Object, Source["Id"]))
    print("Exported", Source["Id"])

(Output / "Meshes.json").write_text(json.dumps(Meshes, indent=4) + "\n", encoding="utf-8")
print("Exported", len(Meshes), "real environment meshes")

def Material(Name, Color, Texture=None):
    Result = bpy.data.materials.new(Name)
    Result.use_nodes = True
    Shader = Result.node_tree.nodes.get("Principled BSDF")
    Shader.inputs["Base Color"].default_value = (*Color, 1.0)
    if Texture:
        ImageNode = Result.node_tree.nodes.new("ShaderNodeTexImage")
        ImageNode.image = bpy.data.images.load(str(Output / "Textures" / Texture), check_existing=True)
        Result.node_tree.links.new(ImageNode.outputs["Color"], Shader.inputs["Base Color"])
        NormalPath = Output / "Textures" / Texture.replace("_Diffuse", "_Normal")
        if NormalPath.is_file() and "_Diffuse" in Texture:
            NormalTexture = Result.node_tree.nodes.new("ShaderNodeTexImage")
            NormalTexture.image = bpy.data.images.load(str(NormalPath), check_existing=True)
            NormalTexture.image.colorspace_settings.name = "Non-Color"
            NormalMap = Result.node_tree.nodes.new("ShaderNodeNormalMap")
            Result.node_tree.links.new(NormalTexture.outputs["Color"], NormalMap.inputs["Color"])
            Result.node_tree.links.new(NormalMap.outputs["Normal"], Shader.inputs["Normal"])
    return Result

def ExportAuthored(Object, Name):
    Object.name = Name
    Meshes.append(ExportMesh(Object, "Authored"))

def CurveTube(Name, Points, Radius, Surface):
    Curve = bpy.data.curves.new(Name, "CURVE")
    Curve.dimensions = "3D"
    Curve.bevel_depth = Radius
    Curve.bevel_resolution = 3
    Spline = Curve.splines.new("POLY")
    Spline.points.add(len(Points) - 1)
    for Point, Position in zip(Spline.points, Points):
        Point.co = (*Position, 1.0)
    Object = bpy.data.objects.new(Name, Curve)
    bpy.context.collection.objects.link(Object)
    Object.data.materials.append(Surface)
    return Object

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
Asphalt = Material("Wet_Asphalt", (1.0, 1.0, 1.0), "worn_asphalt_Diffuse.jpg")
bpy.ops.mesh.primitive_grid_add(x_subdivisions=24, y_subdivisions=16, size=2.0)
Road = bpy.context.object
for Vertex in Road.data.vertices:
    Vertex.co.x *= 26.0
    Vertex.co.y *= 17.0
for Loop in Road.data.uv_layers.active.data:
    Loop.uv.x *= 13.0
    Loop.uv.y *= 8.5
Road.data.materials.append(Asphalt)
ExportAuthored(Road, "Street_Surface")
Concrete = Material("Pavement", (1.0, 1.0, 1.0), "concrete_floor_worn_001_Diffuse.jpg")
Vertices = [(-26.0, -1.0, -0.15), (26.0, -1.0, -0.15), (26.0, -1.0, 0.0), (-26.0, -1.0, 0.0), (-26.0, 0.9, 0.0), (26.0, 0.9, 0.0), (26.0, 1.0, -0.10), (-26.0, 1.0, -0.10)]
Faces = [(0, 1, 2, 3), (3, 2, 5, 4), (4, 5, 6, 7)]
Mesh = bpy.data.meshes.new("Pavement")
Mesh.from_pydata(Vertices, [], Faces)
Mesh.uv_layers.new()
for Polygon in Mesh.polygons:
    for Index in Polygon.loop_indices:
        Position = Mesh.vertices[Mesh.loops[Index].vertex_index].co
        Mesh.uv_layers.active.data[Index].uv = (Position.x / 2.0, Position.y / 2.0)
Sidewalk = bpy.data.objects.new("Pavement", Mesh)
bpy.context.collection.objects.link(Sidewalk)
Sidewalk.data.materials.append(Concrete)
ExportAuthored(Sidewalk, "Sidewalk")

FontPath = Output / "Fonts/Pretendard-Bold.otf"
Font = bpy.data.fonts.load(str(FontPath))
for Name, Label, Color, Width in [("Neon_Karaoke", "노래방", (1.0, 0.025, 0.25), 3.4), ("Neon_Ramen", "심야식당", (1.0, 0.45, 0.035), 4.0), ("Neon_Arcade", "ARCADE", (0.02, 0.8, 1.0), 4.5), ("Neon_Dragon", "홍련거리", (0.7, 0.03, 1.0), 3.6), ("Neon_Open", "OPEN 24H", (0.04, 1.0, 0.6), 2.3)]:
    Curve = bpy.data.curves.new(Name, "FONT")
    Curve.body = Label
    Curve.font = Font
    Curve.align_x = "CENTER"
    Curve.align_y = "CENTER"
    Curve.size = 1.0
    Curve.extrude = 0.006
    Curve.bevel_depth = 0.009
    Curve.bevel_resolution = 3
    Object = bpy.data.objects.new(Name, Curve)
    bpy.context.collection.objects.link(Object)
    Object.rotation_euler[0] = math.pi / 2.0
    bpy.context.view_layer.update()
    Factor = Width / max(Object.dimensions.x, 0.001)
    Object.scale = (-Factor, Factor, Factor)
    Object.data.materials.append(Material("Neon_" + Name, Color))
    ExportAuthored(Object, Name)
    Points = []
    for Index in range(65):
        Angle = Index * math.tau / 64.0
        X = math.copysign(abs(math.cos(Angle)) ** 0.22, math.cos(Angle)) * (Width * 0.5 + 0.18)
        Z = math.copysign(abs(math.sin(Angle)) ** 0.22, math.sin(Angle)) * 0.68
        Points.append((X, 0.025, Z))
    Frame = CurveTube(Name + "_Frame", Points, 0.015, Material("Neon_Frame", Color))
    ExportAuthored(Frame, Name + "_Frame")

SkyMaterial = Material("Night_Sky", (1.0, 1.0, 1.0), "NightCity.jpg")
bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=100.0)
Sky = bpy.context.object
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.flip_normals()
bpy.ops.object.mode_set(mode="OBJECT")
Sky.data.materials.append(SkyMaterial)
ExportAuthored(Sky, "Night_Sky")
(Output / "Meshes.json").write_text(json.dumps(Meshes, indent=4) + "\n", encoding="utf-8")
CurrentPaths = {Mesh["Path"] for Mesh in Meshes}
for Previous in PreviousMeshes:
    if Previous["Path"] not in CurrentPaths:
        PreviousPath = (Root / "Assets" / Previous["Path"]).resolve()
        if PreviousPath.parent != (Output / "Models").resolve():
            raise ValueError("Invalid generated mesh cleanup path")
        for File in [PreviousPath, PreviousPath.with_suffix(".mtl"), PreviousPath.with_suffix(".obj.bin")]:
            File.unlink(missing_ok=True)
