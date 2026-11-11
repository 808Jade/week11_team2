#include "EnginePCH.h"
#include "FbxImporter.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "HumanoidRetarget.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifndef WITH_FBX_SDK
#define WITH_FBX_SDK 0
#endif

#if WITH_FBX_SDK
#include <fbxsdk.h>

namespace
{
FString Lower(FString S) { for (char& C : S) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C))); return S; }

FString NodePath(FbxNode* Node)
{
	if (!Node || !Node->GetParent()) return "/";
	FString Name = Node->GetName();
	int Occurrence = 0;
	for (int I = 0; I < Node->GetParent()->GetChildCount(); ++I)
	{
		FbxNode* Sibling = Node->GetParent()->GetChild(I);
		if (Sibling == Node) break;
		if (Name == Sibling->GetName()) ++Occurrence;
	}
	return NodePath(Node->GetParent()) + Name + "[" + std::to_string(Occurrence) + "]/";
}

bool HasAttribute(FbxNode* Node, FbxNodeAttribute::EType Type)
{
	for (int I = 0; Node && I < Node->GetNodeAttributeCount(); ++I)
		if (Node->GetNodeAttributeByIndex(I)->GetAttributeType() == Type) return true;
	return false;
}

bool IsCompensation(FbxCluster* Cluster)
{
	return Cluster && !std::strcmp(Cluster->GetUserDataID(), "Maya_ClusterHint") &&
		!std::strcmp(Cluster->GetUserData(), "CompensationCluster");
}

// UE FFbxScene::Internal_GetRootSkeleton의 상향 경계와 Blender Armature 예외를 따른다.
FbxNode* FindRoot(FbxScene* Scene, FbxNode* Link, bool bBlender)
{
	FbxNode* Root = Link;
	while (Root && Root->GetParent() && Root->GetParent() != Scene->GetRootNode())
	{
		FbxNode* Parent = Root->GetParent();
		FbxNodeAttribute* Attribute = Parent->GetNodeAttribute();
		if (!Attribute) break;
		const auto Type = Attribute->GetAttributeType();
		const bool bArmature = bBlender && Lower(Parent->GetName()) == "armature" &&
			(!Parent->GetParent() || Parent->GetParent() == Scene->GetRootNode());
		if (Type != FbxNodeAttribute::eSkeleton && Type != FbxNodeAttribute::eMesh &&
			(Type != FbxNodeAttribute::eNull || bArmature)) break;
		if (Type == FbxNodeAttribute::eMesh && static_cast<FbxMesh*>(Attribute)->GetDeformerCount(FbxDeformer::eSkin)) break;
		Root = Parent;
	}
	return Root;
}

bool CollisionName(FString Name)
{
	Name = Lower(Name);
	// 후속 충돌 임포트는 이 분류에서 별도 데이터 경로로 연결한다.
	for (const char* Prefix : { "ucx_", "ubx_", "usp_", "ucp_", "mcdcX_" })
		if (Name.starts_with(Lower(Prefix))) return true;
	return false;
}

FbxAMatrix Geometry(FbxNode* Node)
{
	FbxAMatrix Out;
	Out.SetT(Node->GetGeometricTranslation(FbxNode::eSourcePivot));
	Out.SetR(Node->GetGeometricRotation(FbxNode::eSourcePivot));
	Out.SetS(Node->GetGeometricScaling(FbxNode::eSourcePivot));
	return Out;
}

FVector Reflect(const FbxVector4& V) { return FVector(static_cast<float>(V[0]), static_cast<float>(-V[1]), static_cast<float>(V[2])); }

FMatrix EngineMatrix(const FbxAMatrix& M, double UnitScale, bool bEngineInput = true)
{
	const FbxVector4 O = M.MultT(FbxVector4(0, 0, 0));
	const FVector X = Reflect(M.MultT(FbxVector4(1, 0, 0)) - O);
	const FVector Y = Reflect(M.MultT(FbxVector4(0, 1, 0)) - O) * (bEngineInput ? -1.f : 1.f);
	const FVector Z = Reflect(M.MultT(FbxVector4(0, 0, 1)) - O);
	const FVector T = Reflect(O) * static_cast<float>(UnitScale);
	return FMatrix(X.X, X.Y, X.Z, 0, Y.X, Y.Y, Y.Z, 0, Z.X, Z.Y, Z.Z, 0, T.X, T.Y, T.Z, 1);
}

bool FiniteMatrix(const FMatrix& M)
{
	for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C) if (!std::isfinite(M.M[R][C])) return false;
	return std::abs(M.Determinant()) > 1.e-12f;
}

struct FBoneTRS
{
	FVector Translation;
	FQuat Rotation;
	FVector Scale;
};

FBoneTRS ConvertBoneTRS(const FbxAMatrix& Local, double UnitScale)
{
	const FbxQuaternion Q = Local.GetQ();
	const FbxVector4 S = Local.GetS();
	return { Reflect(Local.GetT()) * static_cast<float>(UnitScale),
		FQuat(static_cast<float>(Q[0]), static_cast<float>(-Q[1]), static_cast<float>(Q[2]), static_cast<float>(-Q[3])).Normalized(),
		FVector(static_cast<float>(S[0]), static_cast<float>(S[1]), static_cast<float>(S[2])) };
}

FName UnqualifiedBoneName(const FString& Name)
{
	const auto Separator = Name.find_last_of(':');
	return FName(Separator == FString::npos ? Name : Name.substr(Separator + 1));
}

FName NormalizedBoneName(const FString& Name)
{
	FString Result = Name;
	for (char& C : Result)
	{
		if (C >= 'A' && C <= 'Z') C = static_cast<char>(C - 'A' + 'a');
		else if (C == ':') C = '_';
	}
	const auto Separator = Result.find_last_of('_');
	if (Separator != FString::npos && Separator > 0 && Separator + 1 < Result.size()
		&& std::all_of(Result.begin() + Separator + 1, Result.end(), [](char C) { return C >= '0' && C <= '9'; }))
	{
		Result.resize(Separator);
	}
	return FName(Result);
}

// 일부 FBX(예: Takes 섹션이 없는 Sketchfab 변환본)는 파일에 CurveNode→Layer 연결이 있어도
// SDK로 읽은 뒤 레이어가 비고, 커브노드는 뼈 속성에만 연결된 채 남는다.
// 그대로 두면 IsAnimated(Layer)가 모두 false라 애니메이션이 없는 것으로 판단되므로,
// 대상을 알 수 있는 스택과 레이어가 하나뿐일 때만 다시 넣는다.
int32 RepairDetachedCurveNodes(FbxScene* Scene)
{
	if (Scene->GetSrcObjectCount<FbxAnimStack>() != 1) return 0;
	FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(0);
	if (Stack->GetMemberCount<FbxAnimLayer>() != 1) return 0;
	FbxAnimLayer* Layer = Stack->GetMember<FbxAnimLayer>(0);
	if (Layer->GetMemberCount<FbxAnimCurveNode>() > 0) return 0;

	int32 Repaired = 0;
	for (int I = 0; I < Scene->GetSrcObjectCount<FbxAnimCurveNode>(); ++I)
	{
		FbxAnimCurveNode* CurveNode = Scene->GetSrcObject<FbxAnimCurveNode>(I);
		if (CurveNode->GetDstPropertyCount() == 0) continue;
		Layer->AddMember(CurveNode);
		++Repaired;
	}
	return Repaired;
}

// 기존 Week11 에셋의 이름은 보존하고, 다른 DCC namespace에서 온 본은 고유한 짧은 이름으로도 찾는다.
class FTargetBoneNames
{
public:
	explicit FTargetBoneNames(const FReferenceSkeleton& Skeleton)
	{
		for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		{
			const auto& Name = Skeleton.RefBoneInfo[Index].Name;
			Add(Exact, FName(Name), Index);
			Add(Unqualified, UnqualifiedBoneName(Name), Index);
			Add(Normalized, NormalizedBoneName(Name), Index);
		}
	}

	int32 Find(const FString& Name) const
	{
		if (const auto It = Exact.find(FName(Name)); It != Exact.end()) return It->second;
		if (const auto It = Unqualified.find(UnqualifiedBoneName(Name)); It != Unqualified.end()) return It->second;
		// 정규화 후 겹치는 이름은 Add()가 INDEX_NONE으로 막아 두므로 어느 뼈인지 모호하면 매칭하지 않는다.
		if (const auto It = Normalized.find(NormalizedBoneName(Name)); It != Normalized.end()) return It->second;
		return INDEX_NONE;
	}

private:
	using FIndices = std::unordered_map<FName, int32, FNameHash>;
	static void Add(FIndices& Indices, FName Name, int32 Index)
	{
		const auto [It, bAdded] = Indices.emplace(Name, Index);
		if (!bAdded) It->second = INDEX_NONE;
	}
	FIndices Exact, Unqualified, Normalized;
};

bool Near(const FbxAMatrix& A, const FbxAMatrix& B)
{
	for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C)
		if (std::abs(A.Get(R, C) - B.Get(R, C)) > 1.e-4 * (1 + std::abs(A.Get(R, C)))) return false;
	return true;
}

FbxFileTexture* FileTexture(FbxSurfaceMaterial* Material, const char* PropertyName)
{
	const FbxProperty Property = Material ? Material->FindProperty(PropertyName) : FbxProperty();
	return Property.IsValid() ? Property.GetSrcObject<FbxFileTexture>(0) : nullptr;
}

// FBX UV에 적용하는 2D affine 변환. 엔진의 V 반전은 이 변환 이후에 수행한다.
struct FUVTransform
{
	double M[2][3]{ {1, 0, 0}, {0, 1, 0} };
	bool bValid = true;
	FbxVector2 Apply(const FbxVector2& UV) const
	{
		return { M[0][0] * UV[0] + M[0][1] * UV[1] + M[0][2], M[1][0] * UV[0] + M[1][1] * UV[1] + M[1][2] };
	}
};

FUVTransform UVTransform(FbxFileTexture* Texture)
{
	FUVTransform Out;
	if (!Texture) return Out;
	FbxAMatrix Scale, Rotation, Translation, ScalePivot, InvScalePivot, RotationPivot, InvRotationPivot;
	Scale.SetS(FbxVector4(Texture->GetScaleU(), Texture->GetScaleV(), 1));
	Rotation.SetR(FbxVector4(Texture->GetRotationU(), Texture->GetRotationV(), Texture->GetRotationW()));
	Translation.SetT(FbxVector4(Texture->GetTranslationU(), Texture->GetTranslationV(), 0));
	const FbxDouble3 SP = Texture->ScalingPivot.Get(), RP = Texture->RotationPivot.Get();
	ScalePivot.SetT(FbxVector4(SP[0], SP[1], SP[2])); InvScalePivot.SetT(FbxVector4(-SP[0], -SP[1], -SP[2]));
	RotationPivot.SetT(FbxVector4(RP[0], RP[1], RP[2])); InvRotationPivot.SetT(FbxVector4(-RP[0], -RP[1], -RP[2]));
	const FbxAMatrix Matrix = Translation * RotationPivot * Rotation * InvRotationPivot * ScalePivot * Scale * InvScalePivot;
	FbxVector4 O = Matrix.MultT(FbxVector4(0, 0, 0)), U = Matrix.MultT(FbxVector4(1, 0, 0)), V = Matrix.MultT(FbxVector4(0, 1, 0));
	if (Texture->GetSwapUV()) { std::swap(O[0], O[1]); std::swap(U[0], U[1]); std::swap(V[0], V[1]); }
	for (int I = 0; I < 2; ++I)
	{
		Out.M[I][0] = U[I] - O[I]; Out.M[I][1] = V[I] - O[I]; Out.M[I][2] = O[I];
		for (double Value : Out.M[I]) Out.bValid &= std::isfinite(Value);
	}
	Out.bValid &= Texture->GetMappingType() == FbxTexture::eUV;
	return Out;
}

bool Near(const FUVTransform& A, const FUVTransform& B)
{
	for (int R = 0; R < 2; ++R) for (int C = 0; C < 3; ++C)
		if (std::abs(A.M[R][C] - B.M[R][C]) > 1.e-6 * (1 + std::max(std::abs(A.M[R][C]), std::abs(B.M[R][C])))) return false;
	return true;
}

struct FSurfaceUV
{
	FString Set;
	FUVTransform Transform;
	FbxGeometryElementTangent* Tangents = nullptr;
	FbxGeometryElementBinormal* Binormals = nullptr;
};

template<class T> bool CornerVector(T* Element, FbxMesh* Mesh, int Polygon, int Corner, FbxVector4& Out)
{
	if (!Element) return false;
	int Index;
	switch (Element->GetMappingMode())
	{
	case FbxLayerElement::eByControlPoint: Index = Mesh->GetPolygonVertex(Polygon, Corner); break;
	case FbxLayerElement::eByPolygonVertex: Index = Mesh->GetPolygonVertexIndex(Polygon) + Corner; break;
	case FbxLayerElement::eByPolygon: Index = Polygon; break;
	case FbxLayerElement::eAllSame: Index = 0; break;
	default: return false;
	}
	if (Element->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
	{
		if (Index < 0 || Index >= Element->GetIndexArray().GetCount()) return false;
		Index = Element->GetIndexArray().GetAt(Index);
	}
	if (Index < 0 || Index >= Element->GetDirectArray().GetCount()) return false;
	Out = Element->GetDirectArray().GetAt(Index);
	return std::isfinite(Out[0]) && std::isfinite(Out[1]) && std::isfinite(Out[2]);
}

bool ImportTangent(const FSurfaceUV& UV, FbxMesh* Mesh, int Polygon, int Corner, const FMatrix& GeometryTransform, FVertex& Vertex)
{
	FbxVector4 T, B;
	if (!CornerVector(UV.Tangents, Mesh, Polygon, Corner, T)) return false;
	if (!CornerVector(UV.Binormals, Mesh, Polygon, Corner, B))
	{
		FbxVector4 N;
		if (!Mesh->GetPolygonVertexNormal(Polygon, Corner, N) || !std::isfinite(T[3]) || std::abs(std::abs(T[3]) - 1) > 1.e-4) return false;
		B = N.CrossProduct(T) * T[3];
	}
	auto Direction = [&](const FbxVector4& V) { return GeometryTransform.TransformVector(FVector(static_cast<float>(V[0]), static_cast<float>(V[1]), static_cast<float>(V[2]))); };
	const FVector GT = Direction(T), GB = Direction(B);
	// 최종 UV의 Jacobian 역행렬로 원본 T/B를 옮긴다. 두 번째 행의 부호는 1-V 변환이다.
	const auto& M = UV.Transform.M;
	const double J00 = M[0][0], J01 = M[0][1], J10 = -M[1][0], J11 = -M[1][1];
	const double Det = J00 * J11 - J01 * J10;
	if (!std::isfinite(Det) || std::abs(Det) < 1.e-12) return false;
	FVector Tangent = GT * static_cast<float>(J11 / Det) + GB * static_cast<float>(-J10 / Det);
	const FVector Bitangent = GT * static_cast<float>(-J01 / Det) + GB * static_cast<float>(J00 / Det);
	Tangent = (Tangent - Vertex.Normal * Vertex.Normal.Dot(Tangent)).GetSafeNormal();
	const float Orientation = Vertex.Normal.Cross(Tangent).Dot(Bitangent);
	if (!std::isfinite(Orientation) || std::abs(Orientation) < 1.e-8f || Tangent.SizeSquared() < 1.e-8f) return false;
	Vertex.Tangent = FVector4(Tangent, Orientation < 0 ? -1.f : 1.f);
	return true;
}

struct FMeshItem
{
	FbxNode* Node = nullptr;
	FbxMesh* Mesh = nullptr;
	FbxNode* Rig = nullptr;
	FbxNode* LODGroupNode = nullptr;
	FString UnitKey;
	FString Label;
	int LOD = -1;
};

struct FRig
{
	FbxNode* Root = nullptr;
	std::set<FbxNode*> Needed;
	bool bHasTransformAnimation = false;
	std::vector<FbxNode*> Bones;
	std::unordered_map<FbxNode*, uint16> Indices;
	FReferenceSkeleton Reference;
	FbxAMatrix ParentBind;
	FString MeshSkeletonKey;
	bool bHasMesh = false;

	void IncludeBoneAndParents(FbxNode* Node)
	{
		for (; Node; Node = Node->GetParent())
			if (!Needed.insert(Node).second || Node == Root) break;
	}
};

struct FOutputGroup
{
	FString Identity;
	FString Label;
	FbxNode* Root = nullptr;
	std::vector<FMeshItem*> Items;
};

struct FCornerKey
{
	int CP;
	int Surface;
	FVertex Vertex;
	bool operator==(const FCornerKey& Other) const { return CP == Other.CP && Surface == Other.Surface && !std::memcmp(&Vertex, &Other.Vertex, sizeof(Vertex)); }
};
struct FCornerHash
{
	size_t operator()(const FCornerKey& Key) const
	{
		uint64 Hash = static_cast<uint32>(Key.CP) ^ (static_cast<uint64>(Key.Surface) << 32);
		for (size_t I = 0; I < sizeof(FVertex); ++I) Hash = (Hash ^ reinterpret_cast<const uint8*>(&Key.Vertex)[I]) * 1099511628211ull;
		return Hash;
	}
};

class FImport
{
public:
	FbxScene* Scene;
	FFbxImportOptions Options;
	FString SourceKey;
	fs::path Source;
	fs::path Destination;
	double UnitScale = 1;
	bool bBlender = false;
	TArray<FFbxAssetData>& Output;
	TArray<FString>& Warnings;
	FString& Error;
	std::vector<FMeshItem> Meshes;
	std::map<FString, FRig> Rigs;
	std::unordered_map<FbxNode*, FbxAMatrix> ClusterBones;
	std::unordered_map<FbxMesh*, FbxAMatrix> ClusterMeshes;
	std::unordered_map<FbxNode*, FbxAMatrix> BindCache;
	// BindPose도 Cluster도 없어서 Bind()가 0초의 평가값을 돌려준 노드. (스킨 없는 애니메이션 전용 FBX의 본)
	std::unordered_set<FbxNode*> EvaluatedBinds;
	std::unordered_map<FbxSurfaceMaterial*, FString> Materials;
	std::unordered_set<FbxSurfaceMaterial*> SkippedOpacityTextures;
	std::unordered_map<FString, FString> Textures;
	std::unordered_map<FString, FString> FileOwners;
	std::unordered_set<FString> Warned;
	std::vector<FbxPose*> Poses;
	std::map<FString, FFbxAssetData> SelectedSkeletons;

	FImport(FbxScene* S, const FFbxImportOptions& O, const FString& K, const fs::path& P, const fs::path& D,
		TArray<FFbxAssetData>& Out, TArray<FString>& W, FString& E)
		: Scene(S), Options(O), SourceKey(K), Source(P), Destination(D), Output(Out), Warnings(W), Error(E) {}

	void Warn(const FString& Message) { if (Warned.insert(Message).second) Warnings.Add(Message); }
	FString MakeKey(const char* Prefix, const FString& Label, const FString& Identity);
	FFbxAssetData Data(EFbxAssetType Type, const FString& Key, const FString& Identity)
	{
		FFbxAssetData Out; Out.Type = Type; Out.Key = Key; Out.SourcePath = SourceKey; Out.SourceObject = Identity; Out.Options = Options; return Out;
	}
	void Gather(FbxNode* Node, bool bParentVisible, FbxNode* LODGroup = nullptr, int LOD = -1);
	void FindRigs();
	void PreparePoses();
	bool PoseMatrix(FbxPose* Pose, FbxNode* Node, FbxAMatrix& Out);
	FbxAMatrix Bind(FbxNode* Node);
	FbxAMatrix MeshBind(const FMeshItem& Item);
	bool BuildRig(FRig& Rig);
	bool LoadSelectedSkeleton(const FString& Key);
	bool ChooseMeshSkeleton(FRig& Rig);
	bool CookAnimations(FRig& Rig);
	bool CookRetargetedAnimations(FRig& Rig, const FString& SkeletonKey, const FReferenceSkeleton& Target, double Rate);
	FString Material(FbxSurfaceMaterial* SourceMaterial);
	FString Texture(FbxProperty Property);
	FString OpacityTexture(FbxSurfaceMaterial* Material, const FString& DiffuseKey, const FString& OpacityKey, const FString& Identity, ESamplerState Sampler);
	FSurfaceUV SurfaceUV(FbxSurfaceMaterial* SourceMaterial, FbxMesh* Mesh, const FString& Context);
	bool CookMesh(const FOutputGroup& Group);
	bool AppendMesh(const FMeshItem& Item, FFbxAssetData& Asset, int LOD, std::map<uint32, std::vector<uint32>>& Sections);
	void LODThresholds(const FOutputGroup& Group, FFbxAssetData& Asset);
	bool Run();
};

FString FImport::MakeKey(const char* Prefix, const FString& Label, const FString& Identity)
{
	const FString TypePrefix = FString(Prefix) + "_";
	auto StripPrefix = [&](FString Name)
	{
		if (Lower(Name).starts_with(Lower(TypePrefix))) Name.erase(0, TypePrefix.size());
		else if (TypePrefix == "SK_" && Lower(Name).starts_with("skm_")) Name.erase(0, 4);
		return FbxPath::FileName(Name);
	};
	FString SourceStem = FbxPath::ToString(Source.stem());
	const FString LowerStem = Lower(SourceStem);
	for (const FString MeshPrefix : { "skm_", "sk_", "sm_" })
	{
		if (LowerStem.starts_with(MeshPrefix))
		{
			SourceStem.erase(0, MeshPrefix.size());
			break;
		}
	}
	const FString SourceName = StripPrefix(std::move(SourceStem));
	const FString ObjectName = StripPrefix(Label);
	const FString Base = TypePrefix + SourceName + (Lower(SourceName) == Lower(ObjectName) ? "" : "_" + ObjectName);
	FString Candidate = FbxPath::ToString(Destination / FbxPath::FromString(Base + ".fbxbin"));
	FString Key;
	if (!FbxPath::GetAssetKey(Candidate, Key, Error, false)) return {};
	bool Conflict = false;
	if (auto It = FileOwners.find(Lower(Key)); It != FileOwners.end()) Conflict = It->second != Identity;
	if (fs::exists(FbxPath::FromString(Candidate)))
	{
		FString PreviousSource, PreviousObject;
		// 읽을 수 없는 파일은 덮어쓰지 않고 별도 안정 이름을 선택한다.
		if (!FFbxAssetBake::ReadSourceIdentity(Candidate, PreviousSource, PreviousObject)) Conflict = true;
		else Conflict |= Lower(PreviousSource) != Lower(SourceKey) || PreviousObject != Identity;
	}
	if (Conflict)
	{
		std::ostringstream Hash; Hash << std::hex << FbxPath::StableHash(SourceKey + "/" + Identity);
		Candidate = FbxPath::ToString(Destination / FbxPath::FromString(Base + "_" + Hash.str() + ".fbxbin"));
		if (!FbxPath::GetAssetKey(Candidate, Key, Error, false)) return {};
		if (fs::exists(FbxPath::FromString(Candidate)))
		{
			FString PreviousSource, PreviousObject;
			if (!FFbxAssetBake::ReadSourceIdentity(Candidate, PreviousSource, PreviousObject) ||
				Lower(PreviousSource) != Lower(SourceKey) || PreviousObject != Identity) { Error = "Output identity collision: " + Key; return {}; }
		}
	}
	FileOwners[Lower(Key)] = Identity;
	return Key;
}

void FImport::Gather(FbxNode* Node, bool ParentVisible, FbxNode* Group, int LOD)
{
	const bool Visible = Node->Visibility.Get() > 0 && (!Node->VisibilityInheritance.Get() || ParentVisible);
	for (int A = 0; A < Node->GetNodeAttributeCount(); ++A)
	{
		auto* Attribute = Node->GetNodeAttributeByIndex(A);
		if (Attribute->GetAttributeType() != FbxNodeAttribute::eMesh || !Visible || CollisionName(Node->GetName())) continue;
		auto* Mesh = static_cast<FbxMesh*>(Attribute);
		FMeshItem Item; Item.Node = Node; Item.Mesh = Mesh; Item.LODGroupNode = Group; Item.LOD = LOD;
		Item.Label = Group ? Group->GetName() : Node->GetName();
		Item.UnitKey = Group ? NodePath(Group) : NodePath(Node) + "Mesh[" + std::to_string(A) + "]";
		if (!Group)
		{
			std::smatch Match;
			const FString Name = Node->GetName();
			if (std::regex_match(Name, Match, std::regex("(.+)_LOD([0-9]+)", std::regex::icase)))
			{
				Item.Label = Match[1].str();
				Item.UnitKey = NodePath(Node->GetParent()) + "NamedLOD/" + Item.Label;
				try { Item.LOD = std::stoi(Match[2].str()); } catch (...) { Item.LOD = 4; }
			}
		}
		if (Item.LOD < 4) Meshes.push_back(std::move(Item));
		else Warn("LOD4 and later geometry is omitted: " + Item.UnitKey);
	}
	const bool NewGroup = !Group && HasAttribute(Node, FbxNodeAttribute::eLODGroup);
	for (int I = 0; I < Node->GetChildCount(); ++I)
		Gather(Node->GetChild(I), Visible, NewGroup ? Node : Group, NewGroup ? I : LOD);
}

void FImport::FindRigs()
{
	std::set<FbxNode*> Links;
	// Skeleton 속성이 없는 본(예: NodeAttribute 없이 LimbNode만 있는 Sketchfab 변환본)은
	// 선택한 Skeleton의 루트 이름으로 찾는다. 표기 차이("mixamorig:Hips" vs "mixamorig_Hips_01")도 허용한다.
	auto MatchesSelectedRoot = [](FbxNode* Node, const FString& RootName)
	{
		if (Node->GetNodeAttribute() && !HasAttribute(Node, FbxNodeAttribute::eNull)) return false;
		const FString NodeName = Node->GetName();
		return UnqualifiedBoneName(NodeName) == UnqualifiedBoneName(RootName)
			|| NormalizedBoneName(NodeName) == NormalizedBoneName(RootName);
	};
	const auto SelectedMesh = Options.bImportOnlyAnimations
		? SelectedSkeletons.end() : SelectedSkeletons.find(Options.SkeletalMeshSkeletonKey);
	const FString MeshRootName = SelectedMesh == SelectedSkeletons.end()
		? FString() : SelectedMesh->second.RefSkeleton.RefBoneInfo[0].Name;
	for (int N = 0; N < Scene->GetNodeCount(); ++N)
	{
		FbxNode* Node = Scene->GetNode(N);
		if (HasAttribute(Node, FbxNodeAttribute::eSkeleton) ||
			(SelectedMesh != SelectedSkeletons.end() && MatchesSelectedRoot(Node, MeshRootName)))
			Links.insert(Node);
	}
	for (auto& Item : Meshes)
	{
		for (int D = 0; D < Item.Mesh->GetDeformerCount(FbxDeformer::eSkin); ++D)
		{
			auto* Skin = static_cast<FbxSkin*>(Item.Mesh->GetDeformer(D, FbxDeformer::eSkin));
			if (D) Warn("Additional Skin deformers are ignored: " + NodePath(Item.Node));
			if (Skin->GetSkinningType() != FbxSkin::eLinear && Skin->GetSkinningType() != FbxSkin::eRigid)
				Warn("DQ/Blend skinning is imported as Linear: " + NodePath(Item.Node));
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				if (!Cluster || !Cluster->GetLink() || IsCompensation(Cluster)) continue;
				FbxNode* Link = Cluster->GetLink(); Links.insert(Link);
				FbxAMatrix B, M; Cluster->GetTransformLinkMatrix(B); Cluster->GetTransformMatrix(M);
				if (auto It = ClusterBones.find(Link); It != ClusterBones.end())
				{ if (!Near(It->second, B)) Warn("Conflicting bind matrices; selected bind pose retained: " + NodePath(Link)); }
				else ClusterBones.emplace(Link, B);
				if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end())
				{ if (!Near(It->second, M)) Warn("Conflicting mesh bind matrices; first Cluster fallback retained: " + NodePath(Item.Node)); }
				else ClusterMeshes.emplace(Item.Mesh, M);
				if (D == 0)
				{
					FbxNode* Root = FindRoot(Scene, Link, bBlender);
					if (Item.Rig && Item.Rig != Root) { Error = "One mesh references incompatible rig roots: " + NodePath(Item.Node); return; }
					Item.Rig = Root;
				}
			}
		}
	}
	auto AddRig = [this](FbxNode* Link) -> FRig*
	{
		FbxNode* Root = FindRoot(Scene, Link, bBlender);
		if (!Root || Root == Scene->GetRootNode()) return nullptr;
		auto& Rig = Rigs[NodePath(Root)]; Rig.Root = Root;
		Rig.IncludeBoneAndParents(Link);
		return &Rig;
	};
	for (FbxNode* Link : Links) AddRig(Link);

	std::unordered_map<FbxNode*, FRig*> RigsByRoot;
	for (auto& [Path, Rig] : Rigs) RigsByRoot.emplace(Rig.Root, &Rig);
	auto FindContainingRig = [&RigsByRoot](FbxNode* Node) -> FRig*
	{
		for (; Node; Node = Node->GetParent())
			if (auto It = RigsByRoot.find(Node); It != RigsByRoot.end()) return It->second;
		return nullptr;
	};

	for (auto& Item : Meshes)
	{
		const bool bSkinned = Item.Mesh->GetDeformerCount(FbxDeformer::eSkin) != 0;
		if (Item.Rig || (!bSkinned && !Options.bConvertStaticsInBoneHierarchyToSkeletals)) continue;
		FRig* Rig = FindContainingRig(Item.Node);
		if (!Rig) continue;
		Item.Rig = Rig->Root;
		if (!bSkinned) Rig->IncludeBoneAndParents(Item.Node);
	}
	for (const auto& Item : Meshes)
		if (Item.Rig) Rigs.at(NodePath(Item.Rig)).bHasMesh = true;

	// Animation-only root hints must not change how meshes were classified above.
	if (const auto Selected = SelectedSkeletons.find(Options.AnimationSkeletonKey);
		(Options.bImportAnimations || Options.bImportOnlyAnimations) && Selected != SelectedSkeletons.end())
	{
		const FString& RootName = Selected->second.RefSkeleton.RefBoneInfo[0].Name;
		// Humanoid 리타깃이면 이름 규칙이 달라도 같은 역할의 본(예: Pelvis ↔ mixamorig_Hips_01)을 루트로 본다.
		const FString RootHumanoidKey = Options.bRetargetHumanoid ? FHumanoidRetargeter::HumanoidBoneKey(RootName) : FString();
		auto MatchesAnimationRoot = [&](FbxNode* Node)
		{
			if (MatchesSelectedRoot(Node, RootName)) return true;
			if (RootHumanoidKey.empty() || (Node->GetNodeAttribute() && !HasAttribute(Node, FbxNodeAttribute::eNull))) return false;
			return FHumanoidRetargeter::HumanoidBoneKey(Node->GetName()) == RootHumanoidKey;
		};
		for (int I = 0; I < Scene->GetNodeCount(); ++I)
		{
			FbxNode* Node = Scene->GetNode(I);
			if (MatchesAnimationRoot(Node) && !FindContainingRig(Node))
				if (FRig* Rig = AddRig(Node)) RigsByRoot.emplace(Rig->Root, Rig);
		}
	}

	// As in Interchange, transform animation contributes to an established joint hierarchy.
	// A curve on an unrelated scene object does not establish a new skeleton root.
	std::vector<FbxAnimLayer*> AnimationLayers;
	for (int I = 0; I < Scene->GetSrcObjectCount<FbxAnimStack>(); ++I)
	{
		FbxAnimStack* Stack = Scene->GetSrcObject<FbxAnimStack>(I);
		for (int L = 0; L < Stack->GetMemberCount<FbxAnimLayer>(); ++L)
			AnimationLayers.push_back(Stack->GetMember<FbxAnimLayer>(L));
	}
	if (!AnimationLayers.empty())
	{
		for (int I = 0; I < Scene->GetNodeCount(); ++I)
		{
			FbxNode* Node = Scene->GetNode(I);
			FRig* Rig = FindContainingRig(Node);
			if (!Rig) continue;
			if (!Rig->Needed.contains(Node))
			{
				const auto* Attribute = Node->GetNodeAttribute();
				const auto* Mesh = Node->GetMesh();
				if (Attribute && Attribute->GetAttributeType() != FbxNodeAttribute::eNull &&
					(!Options.bConvertStaticsInBoneHierarchyToSkeletals ||
					 !Mesh || Mesh->GetDeformerCount(FbxDeformer::eSkin) != 0)) continue;
			}
			const bool bAnimated = std::any_of(AnimationLayers.begin(), AnimationLayers.end(), [Node](FbxAnimLayer* Layer)
			{
				return Node->LclTranslation.IsAnimated(Layer) || Node->LclRotation.IsAnimated(Layer) ||
					Node->LclScaling.IsAnimated(Layer);
			});
			if (bAnimated)
			{
				Rig->IncludeBoneAndParents(Node);
				Rig->bHasTransformAnimation = true;
			}
		}
	}
}

void FImport::PreparePoses()
{
	if (!Scene->GetPoseCount()) Scene->GetFbxManager()->CreateMissingBindPoses(Scene);
	for (int I = 0; I < Scene->GetPoseCount(); ++I)
	{
		FbxPose* Pose = Scene->GetPose(I);
		if (Pose && Pose->IsBindPose()) Poses.push_back(Pose);
	}
	if (Poses.empty() && !Rigs.empty()) Warn("No usable BindPose; Cluster bind matrices and evaluated reference transforms are used");
}

bool FImport::PoseMatrix(FbxPose* Pose, FbxNode* Node, FbxAMatrix& Out)
{
	const int Index = Pose->Find(Node);
	if (Index < 0) return false;
	// BindPose는 노드별로 검증한다.
	FbxArray<FbxNode*> Ancestors, Deformers, DeformerAncestors, Wrong;
	bool Valid = Pose->IsValidBindPoseVerbose(Node, Ancestors, Deformers, DeformerAncestors, Wrong, 1.e-4);
	if (!Valid)
	{
		for (int A = 0; A < Ancestors.GetCount(); ++A)
			Pose->Add(Ancestors[A], Ancestors[A]->EvaluateGlobalTransform(FBXSDK_TIME_ZERO));
		Valid = Pose->IsValidBindPose(Node);
		if (!Valid)
		{
			FbxNode* Parent = Node->GetParent();
			for (; Parent; Parent = Parent->GetParent())
				if (auto* Attribute = Parent->GetNodeAttribute(); Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eNull) break;
			Valid = Parent && Pose->IsValidBindPose(Parent);
		}
	}
	if (!Valid) return false;
	const FbxMatrix& Matrix = Pose->GetMatrix(Index);
	for (int R = 0; R < 4; ++R) Out.SetRow(R, Matrix.GetRow(R));
	if (Pose->IsLocalMatrix(Index)) Out = Bind(Node->GetParent()) * Out;
	return true;
}

FbxAMatrix FImport::Bind(FbxNode* Node)
{
	if (!Node || Node == Scene->GetRootNode()) return FbxAMatrix();
	if (auto It = BindCache.find(Node); It != BindCache.end()) return It->second;
	FbxAMatrix Result;
	bool Found = false;
	for (FbxPose* Pose : Poses)
	{
		FbxAMatrix Candidate;
		if (!PoseMatrix(Pose, Node, Candidate)) continue;
		if (Found) { if (!Near(Result, Candidate)) Warn("Multiple BindPoses differ; first valid pose retained: " + NodePath(Node)); }
		else { Result = Candidate; Found = true; }
	}
	if (auto It = ClusterBones.find(Node); It != ClusterBones.end())
	{
		if (Found && !Near(Result, It->second)) Warn("BindPose and Cluster differ; BindPose retained: " + NodePath(Node));
		if (!Found) { Result = It->second; Found = true; }
	}
	if (!Found)
	{
		Result = Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO);
		EvaluatedBinds.insert(Node);
	}
	BindCache.emplace(Node, Result);
	return Result;
}

FbxAMatrix FImport::MeshBind(const FMeshItem& Item)
{
	for (FbxPose* Pose : Poses)
	{
		FbxAMatrix Result;
		if (PoseMatrix(Pose, Item.Node, Result))
		{
			if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end() && !Near(Result, It->second))
				Warn("Mesh BindPose and Cluster differ; BindPose retained: " + NodePath(Item.Node));
			return Result;
		}
	}
	if (auto It = ClusterMeshes.find(Item.Mesh); It != ClusterMeshes.end()) return It->second;
	return Bind(Item.Node);
}

bool FImport::BuildRig(FRig& Rig)
{
	Rig.ParentBind = Bind(Rig.Root->GetParent());
	const FbxAMatrix FrameInverse = Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig.ParentBind.Inverse();
	if (!FiniteMatrix(EngineMatrix(Rig.ParentBind, UnitScale))) { Error = "Singular rig parent bind transform"; return false; }
	std::function<bool(FbxNode*, int)> Visit = [&](FbxNode* Node, int Parent)
	{
		if (!Rig.Needed.contains(Node)) return true;
		if (Rig.Bones.size() >= 65536) { Error = "More than 65536 bones in a rig"; return false; }
		const uint16 Index = static_cast<uint16>(Rig.Bones.size());
		Rig.Bones.push_back(Node); Rig.Indices[Node] = Index;
		const FbxAMatrix Global = FrameInverse * Bind(Node);
		const FbxAMatrix Local = Parent < 0 ? Global : (FrameInverse * Bind(Rig.Bones[Parent])).Inverse() * Global;
		const FMatrix Converted = EngineMatrix(Local, UnitScale);
		if (!FiniteMatrix(Converted)) { Error = "Invalid bind transform: " + NodePath(Node); return false; }
		const FBoneTRS TRS = ConvertBoneTRS(Local, UnitScale);
		FTransform Transform(TRS.Rotation.ToFRotator(), TRS.Translation, TRS.Scale);
		const FMatrix Rebuilt = Transform.GetLocalMatrix();
		bool Difference = false;
		for (int R = 0; R < 4; ++R) for (int C = 0; C < 4; ++C)
			Difference |= std::abs(Rebuilt.M[R][C] - Converted.M[R][C]) > 1.e-3f * (1 + std::abs(Converted.M[R][C]));
		if (Difference) Warn("Bone transform is approximated as TRS: " + NodePath(Node));
		Rig.Reference.RefBoneInfo.Add({ Node->GetName(), Parent });
		Rig.Reference.RefBonePose.Add(Transform);
		for (int C = 0; C < Node->GetChildCount(); ++C) if (!Visit(Node->GetChild(C), Index)) return false;
		return true;
	};
	return Visit(Rig.Root, -1) && Rig.Reference.Validate(Error);
}

bool FImport::LoadSelectedSkeleton(const FString& Key)
{
	if (Key.empty() || SelectedSkeletons.contains(Key)) return true;
	FString Path;
	if (!FbxPath::ResolveKey(Key, Path, Error)) return false;
	if (!FFbxAssetBake::IsBakedPath(Path))
	{ Error = "Selected Skeleton must be a .fbxbin asset: " + Key; return false; }
	FFbxAssetData Skeleton;
	if (!FFbxAssetBake::ReadBaked(Path, Skeleton, Error)) return false;
	if (Skeleton.Type != EFbxAssetType::Skeleton)
	{ Error = "Selected asset is not a Skeleton: " + Key; return false; }
	Skeleton.Key = Key;
	SelectedSkeletons.emplace(Key, std::move(Skeleton));
	return true;
}

bool FImport::ChooseMeshSkeleton(FRig& Rig)
{
	if (!Options.SkeletalMeshSkeletonKey.empty())
	{
		auto& Target = SelectedSkeletons.at(Options.SkeletalMeshSkeletonKey).RefSkeleton;
		const FTargetBoneNames Names(Target);
		FReferenceSkeleton MeshReference = Rig.Reference;
		for (auto& Bone : MeshReference.RefBoneInfo)
		{
			const int32 Match = Names.Find(Bone.Name);
			if (Match != INDEX_NONE) Bone.Name = Target.RefBoneInfo[Match].Name;
		}
		FString Reason;
		if (!Target.MergeBonesFrom(MeshReference, Reason))
		{
			Warn("Skeletal mesh group skipped for " + Options.SkeletalMeshSkeletonKey +
				" [" + NodePath(Rig.Root) + "]: " + Reason);
			return true;
		}
		Rig.Reference = std::move(MeshReference);
		Rig.MeshSkeletonKey = Options.SkeletalMeshSkeletonKey;
		return true;
	}
	const FString Identity = "Skeleton:" + NodePath(Rig.Root);
	Rig.MeshSkeletonKey = MakeKey("SKEL", Rig.Root->GetName(), Identity);
	if (Rig.MeshSkeletonKey.empty()) return false;
	auto Asset = Data(EFbxAssetType::Skeleton, Rig.MeshSkeletonKey, Identity);
	Asset.RefSkeleton = Rig.Reference;
	Output.Add(std::move(Asset));
	return true;
}

struct FAnimatedTime
{
	double Start = std::numeric_limits<double>::infinity();
	double Stop = -std::numeric_limits<double>::infinity();
	bool IsValid() const { return std::isfinite(Start) && std::isfinite(Stop) && Start <= Stop; }
	void Include(const FAnimatedTime& Other)
	{
		Start = std::min(Start, Other.Start);
		Stop = std::max(Stop, Other.Stop);
	}
};

FAnimatedTime FindTransformKeyTimes(FbxNode* Node, const std::vector<FbxAnimLayer*>& Layers)
{
	FAnimatedTime Result;
	FbxProperty Properties[]{ Node->LclTranslation, Node->LclRotation, Node->LclScaling };
	for (FbxAnimLayer* Layer : Layers)
	{
		for (auto& Property : Properties)
		{
			auto* CurveNode = Property.GetCurveNode(Layer);
			if (!CurveNode) continue;
			for (unsigned Channel = 0; Channel < CurveNode->GetChannelsCount(); ++Channel)
			{
				for (int Index = 0; Index < CurveNode->GetCurveCount(Channel); ++Index)
				{
					auto* Curve = CurveNode->GetCurve(Channel, Index);
					if (!Curve || Curve->KeyGetCount() == 0) continue;
					Result.Include({ Curve->KeyGetTime(0).GetSecondDouble(),
						Curve->KeyGetTime(Curve->KeyGetCount() - 1).GetSecondDouble() });
				}
			}
		}
	}
	return Result;
}

bool BuildAnimationBoneMap(const FRig& Rig, const FReferenceSkeleton& Target,
	std::vector<int32>& SourceToTarget, int32& SourceRoot, FString& Reason)
{
	const FTargetBoneNames Names(Target);
	const auto& SourceBones = Rig.Reference.RefBoneInfo;
	for (int32 Index = 0; Index < SourceBones.Num(); ++Index)
	{
		if (Names.Find(SourceBones[Index].Name) != 0) continue;
		if (SourceRoot != INDEX_NONE) { Reason = "Multiple source bones match the Skeleton root"; return false; }
		SourceRoot = Index;
	}
	if (SourceRoot == INDEX_NONE)
	{
		Reason = "Source has no joint matching Skeleton root " + Target.RefBoneInfo[0].Name;
		return false;
	}

	SourceToTarget.resize(SourceBones.Num(), INDEX_NONE);
	SourceToTarget[SourceRoot] = 0;
	std::vector<bool> InRoot(SourceBones.Num(), false);
	InRoot[SourceRoot] = true;
	std::unordered_set<int32> UsedBones{ 0 };
	for (int32 Index = SourceRoot + 1; Index < SourceBones.Num(); ++Index)
	{
		const auto& Bone = SourceBones[Index];
		if (!InRoot[Bone.ParentIndex]) continue;
		InRoot[Index] = true;
		const int32 Match = Names.Find(Bone.Name);
		if (Match == INDEX_NONE) continue;
		if (!UsedBones.insert(Match).second ||
			Target.RefBoneInfo[Match].ParentIndex != SourceToTarget[Bone.ParentIndex])
		{
			Reason = "Different parent chain or ambiguous mapping for animation bone " + Bone.Name;
			return false;
		}
		SourceToTarget[Index] = Match;
	}
	return true;
}

bool GetAnimationSampleRange(FbxAnimStack& Stack, const FFbxImportOptions& Options, FAnimatedTime Animated,
	double Rate, FFbxAnimationData& Clip, FString& Reason)
{
	FAnimatedTime Range = Animated;
	if (Options.AnimationRange == EFbxAnimationRange::Timeline)
	{
		const FbxTimeSpan Span = Stack.GetLocalTimeSpan();
		const FAnimatedTime Timeline{ Span.GetStart().GetSecondDouble(), Span.GetStop().GetSecondDouble() };
		if (Timeline.IsValid() && (Timeline.Start < Timeline.Stop || Animated.Start == Animated.Stop))
			Range = Timeline;
	}
	else if (Options.AnimationRange == EFbxAnimationRange::SetRange)
	{
		Range = { Options.FirstAnimationFrame / Rate, Options.LastAnimationFrame / Rate };
	}

	double Frames = (Range.Stop - Range.Start) * Rate;
	if (!Range.IsValid() || !std::isfinite(Frames))
	{ Reason = "Invalid animation time range"; return false; }
	if (std::abs(Frames - std::round(Frames)) > 1.e-4)
	{
		auto Align = [Mode = Options.FrameAlignment](double Frame)
		{
			if (Mode == EFbxFrameAlignment::Floor) return std::floor(Frame);
			if (Mode == EFbxFrameAlignment::Ceiling) return std::ceil(Frame);
			return std::round(Frame);
		};
		if (Options.FrameAlignment == EFbxFrameAlignment::None)
		{ Reason = "Animation length is not on a sample boundary; set Frame Alignment"; return false; }
		const double First = Align(Range.Start * Rate);
		const double Last = Align(Range.Stop * Rate);
		Range = { First / Rate, Last / Rate };
		Frames = Last - First;
	}
	const double LastKey = std::round(Frames);
	const double TimeLimit = FBXSDK_TIME_INFINITE.GetSecondDouble();
	if (Range.Start <= -TimeLimit || Range.Start + LastKey / Rate >= TimeLimit)
	{ Reason = "Animation time exceeds the FBX time range"; return false; }
	if (!std::isfinite(LastKey) || LastKey < 0 || LastKey >= FFbxAnimationData::MaxKeys)
	{ Reason = "Animation sample count exceeds the .fbxbin limit"; return false; }
	Clip.StartTime = Range.Start;
	Clip.FrameRate = static_cast<float>(Rate);
	Clip.NumberOfKeys = static_cast<int32>(LastKey) + 1;
	return true;
}

bool FImport::CookAnimations(FRig& Rig)
{
	const FString& SkeletonKey = Options.AnimationSkeletonKey.empty()
		? Rig.MeshSkeletonKey : Options.AnimationSkeletonKey;
	if (SkeletonKey.empty())
	{
		Warn("Animation skipped because the rig has no imported mesh or selected Skeleton: " + NodePath(Rig.Root));
		return true;
	}
	const auto Selected = SelectedSkeletons.find(SkeletonKey);
	const auto& Target = Selected != SelectedSkeletons.end() ? Selected->second.RefSkeleton : Rig.Reference;
	const auto& Settings = Scene->GetGlobalSettings();
	double Rate = Options.CustomBoneAnimationSampleRate;
	if (Rate == 0)
	{
		const auto TimeMode = Settings.GetTimeMode();
		Rate = TimeMode == FbxTime::eCustom ? Settings.GetCustomFrameRate() : FbxTime::GetFrameRate(TimeMode);
		if (!std::isfinite(Rate) || Rate <= 0)
		{
			Rate = 30.0;
			Warn("FBX source has no valid frame rate; sampling animation at 30 Hz");
		}
	}

	// 다른 파일의 Skeleton에 붙일 때 Humanoid 리타깃을 켜면 이름·계층이 달라도 회전 변화량으로 옮긴다.
	if (Options.bRetargetHumanoid && Selected != SelectedSkeletons.end() && SkeletonKey != Rig.MeshSkeletonKey)
		return CookRetargetedAnimations(Rig, SkeletonKey, Target, Rate);

	std::vector<int32> SourceToTarget;
	int32 SourceRoot = INDEX_NONE;
	FString Reason;
	if (!BuildAnimationBoneMap(Rig, Target, SourceToTarget, SourceRoot, Reason))
	{
		Warn("Animation rig skipped for " + SkeletonKey + " [" + NodePath(Rig.Root) + "]: " + Reason +
			(Selected != SelectedSkeletons.end() ? " (enable Retarget (Humanoid) for a different skeleton layout)" : ""));
		return true;
	}
	const bool bBake = Selected != SelectedSkeletons.end() && SkeletonKey != Rig.MeshSkeletonKey
		? Selected->second.Options.bBakeSkeletalMeshes : Options.bBakeSkeletalMeshes;
	FbxNode* Root = Rig.Bones[SourceRoot];
	const FbxAMatrix RootParentBind = Bind(Root->GetParent());
	const FbxAMatrix FrameInverse = bBake ? FbxAMatrix() : RootParentBind.Inverse();
	// Skeleton 속성이 없는 루트(이름 힌트로 찾은 루트, 예: Sketchfab 변환본의 Hips)의 조상은 뼈대가 아닌 컨테이너다.
	// 그 변환(스케일 0.01, 축 회전 등)을 다른 파일의 Skeleton 루트 트랙에 구우면 메시가 축소·이동되므로 부모 기준 로컬만 쓴다.
	const bool bRootRelativeToParent = !Root->GetNodeAttribute() && Root->GetParent()
		&& SkeletonKey != Rig.MeshSkeletonKey;

	std::map<FString, int32> Occurrences;
	for (int StackIndex = 0; StackIndex < Scene->GetSrcObjectCount<FbxAnimStack>(); ++StackIndex)
	{
		auto* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		const FString StackName = Stack->GetName();
		const int32 Occurrence = Occurrences[StackName]++;
		std::vector<FbxAnimLayer*> Layers;
		for (int LayerIndex = 0; LayerIndex < Stack->GetMemberCount<FbxAnimLayer>(); ++LayerIndex)
		{
			auto* Layer = Stack->GetMember<FbxAnimLayer>(LayerIndex);
			if (Layer && !Layer->Mute.Get()) Layers.push_back(Layer);
		}
		if (std::any_of(Layers.begin(), Layers.end(), [](auto* Layer) { return Layer->Solo.Get(); }))
			std::erase_if(Layers, [](auto* Layer) { return !Layer->Solo.Get(); });

		std::vector<int32> TrackIndices;
		FAnimatedTime Animated;
		bool bSkippedUnmatched = false;
		for (int32 Index = 0; Index < static_cast<int32>(Rig.Bones.size()); ++Index)
		{
			const auto TrackTimes = FindTransformKeyTimes(Rig.Bones[Index], Layers);
			if (!TrackTimes.IsValid()) continue;
			if (SourceToTarget[Index] == INDEX_NONE) { bSkippedUnmatched = true; continue; }
			TrackIndices.push_back(Index);
			Animated.Include(TrackTimes);
		}
		const FString Context = StackName + " [" + NodePath(Rig.Root) + "]";
		if (bSkippedUnmatched)
			Warn("Animation tracks outside the matched Skeleton are ignored: " + Context);
		if (TrackIndices.empty())
		{
			Warn("Animation skipped " + Context + ": no keyed transform track on the matched bones");
			continue;
		}
		// Ancestor motion affects the root in the same imported frame as its bind pose.
		FAnimatedTime Ancestors;
		for (FbxNode* Node = Root->GetParent(); Node; Node = Node->GetParent())
			Ancestors.Include(FindTransformKeyTimes(Node, Layers));
		if (Ancestors.IsValid())
		{
			if (std::find(TrackIndices.begin(), TrackIndices.end(), SourceRoot) == TrackIndices.end())
				TrackIndices.insert(TrackIndices.begin(), SourceRoot);
			Animated.Include(Ancestors);
		}

		FFbxAnimationData Clip;
		if (!GetAnimationSampleRange(*Stack, Options, Animated, Rate, Clip, Reason))
		{
			Warn("Animation skipped " + Context + ": " + Reason);
			continue;
		}
		if (static_cast<uint64>(Clip.NumberOfKeys) * TrackIndices.size() * 3 > FFbxAnimationData::MaxChannelKeys)
		{
			Warn("Animation exceeds the channel key limit: " + Context);
			continue;
		}
		for (const int32 Index : TrackIndices)
		{
			auto& Track = Clip.BoneAnimationTracks.AddDefaulted_GetRef();
			Track.Name = Target.RefBoneInfo[SourceToTarget[Index]].Name;
		}
		Scene->SetCurrentAnimationStack(Stack);
		Scene->GetAnimationEvaluator()->Reset();
		bool bValid = true;
		for (int32 Key = 0; Key < Clip.NumberOfKeys && bValid; ++Key)
		{
			FbxTime Time;
			Time.SetSecondDouble(Clip.StartTime + Key / Rate);
			for (int32 Track = 0; Track < Clip.BoneAnimationTracks.Num(); ++Track)
			{
				const int32 SourceIndex = TrackIndices[Track];
				FbxNode* Node = Rig.Bones[SourceIndex];
				const FbxAMatrix Global = Node->EvaluateGlobalTransform(Time);
				const FbxAMatrix Local = SourceIndex == SourceRoot && !bRootRelativeToParent ? FrameInverse * Global
					: Node->GetParent()->EvaluateGlobalTransform(Time).Inverse() * Global;
				if (!FiniteMatrix(EngineMatrix(Local, UnitScale)))
				{
					Warn("Animation has an invalid or singular transform: " + Context + " " + NodePath(Node));
					bValid = false;
					break;
				}
				const FBoneTRS TRS = ConvertBoneTRS(Local, UnitScale);
				auto& Raw = Clip.BoneAnimationTracks[Track].InternalTrackData;
				Raw.PosKeys.Add(TRS.Translation);
				Raw.RotKeys.Add(TRS.Rotation);
				Raw.ScaleKeys.Add(TRS.Scale);
			}
		}
		if (!bValid) continue;

		const FString Identity = "Animation:" + NodePath(Rig.Root) + StackName + "[" + std::to_string(Occurrence) + "]";
		const FString Label = (StackName.empty() ? FString("Take") : StackName)
			+ (Rigs.size() > 1 ? "_" + FString(Rig.Root->GetName()) : "");
		const FString Key = MakeKey("AS", Label, Identity);
		if (Key.empty()) return false;
		auto Asset = Data(EFbxAssetType::AnimSequence, Key, Identity);
		Asset.SkeletonKey = SkeletonKey;
		Asset.Animation = std::move(Clip);
		Output.Add(std::move(Asset));
	}
	return true;
}

bool FImport::CookRetargetedAnimations(FRig& Rig, const FString& SkeletonKey, const FReferenceSkeleton& Target, double Rate)
{
	FbxNode* Root = Rig.Root;
	// BuildRig이 만든 소스 기준 자세와 같은 규칙으로 루트를 샘플링해야 변화량이 맞는다.
	// 속성 없는 루트(Sketchfab 컨테이너 아래 Hips 등)는 조상 컨테이너를 빼고 부모 기준으로 맞춘다.
	const bool bRootRelativeToParent = !Root->GetNodeAttribute() && Root->GetParent();
	const FbxAMatrix FrameInverse = Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig.ParentBind.Inverse();
	auto ToTransform = [this](const FbxAMatrix& Local)
	{
		const FBoneTRS TRS = ConvertBoneTRS(Local, UnitScale);
		return FTransform(TRS.Rotation.ToFRotator(), TRS.Translation, TRS.Scale);
	};

	FReferenceSkeleton SourceReference = Rig.Reference;
	if (bRootRelativeToParent)
		SourceReference.RefBonePose[0] = ToTransform(Bind(Root->GetParent()).Inverse() * Bind(Root));

	const int32 NumSourceBones = static_cast<int32>(Rig.Bones.size());

	// BindPose도 Cluster도 없는 본(스킨 없는 애니메이션 전용 FBX)은 Bind()가 0초의 애니메이션 포즈를 돌려준다.
	// 그 포즈를 기준 자세로 삼으면 첫 프레임의 기울기·돌아선 방향이 캐릭터 축(Align)에 섞여 애니메이션 전체가 그만큼 틀어진다
	// (첫 프레임에 몸을 숙이고 시작하는 클립일수록 크게). 그래서 노드의 기본값(애니메이션 커브를 뺀 값, 보통 T-포즈)을 기준으로 쓴다.
	FReferenceSkeleton RestReference;
	bool bUseRestReference = std::any_of(Rig.Bones.begin(), Rig.Bones.end(),
		[this](FbxNode* Bone) { return EvaluatedBinds.contains(Bone); });
	if (bUseRestReference)
	{
		auto Rest = [this](FbxNode* Node)
		{
			const FbxAMatrix Bound = Bind(Node);
			return EvaluatedBinds.contains(Node) ? Node->EvaluateGlobalTransform(FBXSDK_TIME_INFINITE) : Bound;
		};
		RestReference = SourceReference;
		for (int32 Index = 0; Index < NumSourceBones && bUseRestReference; ++Index)
		{
			FbxNode* Node = Rig.Bones[Index];
			const int32 Parent = SourceReference.RefBoneInfo[Index].ParentIndex;
			const FbxAMatrix Local = Parent >= 0 ? Rest(Rig.Bones[Parent]).Inverse() * Rest(Node)
				: bRootRelativeToParent ? Rest(Node->GetParent()).Inverse() * Rest(Node)
				: FrameInverse * Rest(Node);
			if (FiniteMatrix(EngineMatrix(Local, UnitScale))) RestReference.RefBonePose[Index] = ToTransform(Local);
			else bUseRestReference = false;
		}
	}

	FHumanoidRetargeter Retargeter;
	FString Reason;
	// 기본값으로 사람 형태를 못 찾는 파일(기본값이 비어 있는 등)은 예전처럼 0초 포즈를 기준으로 한다.
	if (bUseRestReference && !Retargeter.Initialize(RestReference, Target, Reason))
	{
		Warn("Rest pose is not usable as the retarget reference (" + Reason + "); the pose at time zero is used [" + NodePath(Root) + "]");
		bUseRestReference = false;
		Retargeter = FHumanoidRetargeter();
	}
	if (!bUseRestReference && !Retargeter.Initialize(SourceReference, Target, Reason))
	{
		Warn("Humanoid retarget skipped for " + SkeletonKey + " [" + NodePath(Root) + "]: " + Reason);
		return true;
	}
	const TArray<int32>& Driven = Retargeter.GetDrivenTargetBones();
	Warn("Humanoid retarget drives " + std::to_string(Driven.Num()) + " of " + std::to_string(Target.GetNum()) +
		" target bone(s) [" + NodePath(Root) + "]");

	std::map<FString, int32> Occurrences;
	for (int StackIndex = 0; StackIndex < Scene->GetSrcObjectCount<FbxAnimStack>(); ++StackIndex)
	{
		auto* Stack = Scene->GetSrcObject<FbxAnimStack>(StackIndex);
		const FString StackName = Stack->GetName();
		const int32 Occurrence = Occurrences[StackName]++;
		std::vector<FbxAnimLayer*> Layers;
		for (int LayerIndex = 0; LayerIndex < Stack->GetMemberCount<FbxAnimLayer>(); ++LayerIndex)
		{
			auto* Layer = Stack->GetMember<FbxAnimLayer>(LayerIndex);
			if (Layer && !Layer->Mute.Get()) Layers.push_back(Layer);
		}
		if (std::any_of(Layers.begin(), Layers.end(), [](auto* Layer) { return Layer->Solo.Get(); }))
			std::erase_if(Layers, [](auto* Layer) { return !Layer->Solo.Get(); });

		FAnimatedTime Animated;
		for (FbxNode* Bone : Rig.Bones) Animated.Include(FindTransformKeyTimes(Bone, Layers));
		if (!bRootRelativeToParent)
			for (FbxNode* Node = Root->GetParent(); Node; Node = Node->GetParent())
				Animated.Include(FindTransformKeyTimes(Node, Layers));
		const FString Context = StackName + " [" + NodePath(Root) + "]";
		if (!Animated.IsValid())
		{
			Warn("Animation skipped " + Context + ": no keyed transform track");
			continue;
		}

		FFbxAnimationData Clip;
		if (!GetAnimationSampleRange(*Stack, Options, Animated, Rate, Clip, Reason))
		{
			Warn("Animation skipped " + Context + ": " + Reason);
			continue;
		}
		if (static_cast<uint64>(Clip.NumberOfKeys) * Driven.Num() * 3 > FFbxAnimationData::MaxChannelKeys)
		{
			Warn("Animation exceeds the channel key limit: " + Context);
			continue;
		}
		for (const int32 TargetBone : Driven)
			Clip.BoneAnimationTracks.AddDefaulted_GetRef().Name = Target.RefBoneInfo[TargetBone].Name;

		Scene->SetCurrentAnimationStack(Stack);
		Scene->GetAnimationEvaluator()->Reset();
		std::vector<FbxAMatrix> Globals(NumSourceBones);
		TArray<FTransform> SourceLocal, TargetLocal;
		SourceLocal.SetNum(NumSourceBones);
		bool bValid = true;
		for (int32 Key = 0; Key < Clip.NumberOfKeys && bValid; ++Key)
		{
			FbxTime Time;
			Time.SetSecondDouble(Clip.StartTime + Key / Rate);
			for (int32 Index = 0; Index < NumSourceBones; ++Index)
			{
				FbxNode* Node = Rig.Bones[Index];
				Globals[Index] = Node->EvaluateGlobalTransform(Time);
				const int32 Parent = SourceReference.RefBoneInfo[Index].ParentIndex;
				const FbxAMatrix Local = Parent >= 0 ? Globals[Parent].Inverse() * Globals[Index]
					: bRootRelativeToParent ? Node->GetParent()->EvaluateGlobalTransform(Time).Inverse() * Globals[Index]
					: FrameInverse * Globals[Index];
				if (!FiniteMatrix(EngineMatrix(Local, UnitScale)))
				{
					Warn("Animation has an invalid or singular transform: " + Context + " " + NodePath(Node));
					bValid = false;
					break;
				}
				SourceLocal[Index] = ToTransform(Local);
			}
			if (!bValid) break;

			if (Key == 0) Retargeter.SetSourceHipsAnchor(SourceLocal); // 첫 프레임에서 타깃 기준 자세 위치로 시작한다.
			Retargeter.Retarget(SourceLocal, TargetLocal);
			for (int32 Track = 0; Track < Driven.Num(); ++Track)
			{
				const FTransform& Local = TargetLocal[Driven[Track]];
				auto& Raw = Clip.BoneAnimationTracks[Track].InternalTrackData;
				Raw.PosKeys.Add(Local.Location);
				Raw.RotKeys.Add(Local.Rotation.Quaternion());
				Raw.ScaleKeys.Add(Local.Scale);
			}
		}
		if (!bValid) continue;

		const FString Identity = "RetargetAnimation:" + SkeletonKey + NodePath(Root) + StackName + "[" + std::to_string(Occurrence) + "]";
		// 리타깃 사본은 Skeleton마다 생기므로 이름에 대상 Skeleton(이 들어 있는 폴더 이름)을 붙인다. 예: AS_Hype_boy_Take 001__Ronaldo
		const FString TargetName = FbxPath::ToString(FbxPath::FromString(SkeletonKey).parent_path().filename());
		const FString Label = (StackName.empty() ? FString("Take") : StackName)
			+ (Rigs.size() > 1 ? "_" + FString(Root->GetName()) : "")
			+ (TargetName.empty() ? FString() : "__" + TargetName);
		const FString Key = MakeKey("AS", Label, Identity);
		if (Key.empty()) return false;
		auto Asset = Data(EFbxAssetType::AnimSequence, Key, Identity);
		Asset.SkeletonKey = SkeletonKey;
		Asset.Animation = std::move(Clip);
		Output.Add(std::move(Asset));
	}
	return true;
}

FSurfaceUV FImport::SurfaceUV(FbxSurfaceMaterial* Material, FbxMesh* Mesh, const FString& Context)
{
	FSurfaceUV Out;
	FbxStringList Names; Mesh->GetUVSetNames(Names);
	const FString First = Names.GetCount() ? Names[0].Buffer() : "";
	FbxFileTexture* Inputs[]{ FileTexture(Material, FbxSurfaceMaterial::sDiffuse), FileTexture(Material, FbxSurfaceMaterial::sNormalMap),
		SkippedOpacityTextures.contains(Material) ? nullptr : FileTexture(Material, FbxSurfaceMaterial::sTransparentColor) };
	FString Sets[3]; bool Missing = false;
	for (int I = 0; I < 3; ++I) if (Inputs[I])
	{
		const FString Requested = Inputs[I]->UVSet.Get().Buffer();
		for (int N = 0; N < Names.GetCount(); ++N) if (Requested == Names[N].Buffer()) { Sets[I] = Requested; break; }
		if (Sets[I].empty() && (Requested.empty() || Requested == "default")) Sets[I] = First;
		if (Sets[I].empty())
		{
			Missing = true;
			Warn("UV set not found: " + Context + ", texture=" + Inputs[I]->GetName() + ", requested=" + Requested);
		}
	}
	Out.Set = First;
	for (const auto& Set : Sets) if (!Set.empty()) { Out.Set = Set; break; }
	bool DifferentSets = false;
	for (int I = 0; I < 3; ++I) DifferentSets |= !Missing && Inputs[I] && Sets[I] != Out.Set;
	if (DifferentSets)
		Warn("Different texture UV sets: " + Context + ", diffuse=" + Sets[0] + ", normal=" + Sets[1] + ", opacity=" + Sets[2] + "; selected=" + Out.Set + ", UV transforms ignored");
	else if (Missing)
		Warn("UV fallback: " + Context + "; selected=" + Out.Set + ", UV transforms ignored");
	else
	{
		FUVTransform Selected; bool HasSelected = false, Invalid = false, Different = false;
		for (auto* Input : Inputs) if (Input)
		{
			const FUVTransform Transform = UVTransform(Input); Invalid |= !Transform.bValid;
			if (HasSelected) Different |= !Near(Selected, Transform);
			else { Selected = Transform; HasSelected = true; }
		}
		if (Invalid)
			Warn("Unsupported or invalid texture UV mapping: " + Context + "; UV transforms ignored");
		else if (Different)
			Warn("Different texture UV transforms: " + Context + "; selected=" + Out.Set + ", UV transforms ignored");
		else Out.Transform = Selected;
	}
	// UV 세트가 속한 레이어의 탄젠트를 사용한다. 다른 세트의 기저를 재사용하지 않는다.
	for (int L = 0; L < Mesh->GetLayerCount(); ++L)
	{
		auto* Layer = Mesh->GetLayer(L); const auto UVSets = Layer->GetUVSets();
		for (int U = 0; U < UVSets.GetCount(); ++U)
			if (UVSets[U] && Out.Set == UVSets[U]->GetName()) { Out.Tangents = Layer->GetTangents(); Out.Binormals = Layer->GetBinormals(); return Out; }
	}
	return Out;
}

FString FImport::Texture(FbxProperty Property)
{
	if (!Property.IsValid()) return {};
	if (Property.GetSrcObjectCount<FbxLayeredTexture>()) Warn("Layered textures are not converted: " + FString(Property.GetNameAsCStr()));
	if (!Property.GetSrcObjectCount<FbxFileTexture>()) return {};
	if (Property.GetSrcObjectCount<FbxFileTexture>() > 1) Warn("Only the first texture connection is used: " + FString(Property.GetNameAsCStr()));
	auto* T = Property.GetSrcObject<FbxFileTexture>(0);
	std::vector<fs::path> Candidates;
	if (T->GetFileName()[0]) Candidates.push_back(FbxPath::FromString(T->GetFileName()));
	if (T->GetRelativeFileName()[0]) Candidates.push_back(Source.parent_path() / FbxPath::FromString(T->GetRelativeFileName()));
	if (T->GetFileName()[0]) Candidates.push_back(Source.parent_path() / FbxPath::FromString(T->GetFileName()).filename());
	FString SourceTexture;
	for (auto Candidate : Candidates)
	{
		if (!Candidate.is_absolute()) Candidate = Source.parent_path() / Candidate;
		if (fs::is_regular_file(Candidate)) { SourceTexture = FbxPath::ToString(Candidate); break; }
	}
	if (SourceTexture.empty()) { Error = "Texture file not found: " + FString(T->GetFileName()); return {}; }
	// 입력 텍스처도 원본과 같이 외부에서 읽을 수 있다. 결과에는 픽셀/압축 블록만 남긴다.
	SourceTexture = FbxPath::ToString(fs::canonical(FbxPath::FromString(SourceTexture)));
	const FString TextureKey = SourceTexture;
	if (auto It = Textures.find(Lower(TextureKey)); It != Textures.end()) return It->second;
	FImageData Image = ImageLoader::LoadAuto(SourceTexture);
	if (!Image.IsValid()) { Error = "Texture format is not supported by the existing image loader: " + SourceTexture; return {}; }
	// 임시 내장 경로 대신 소스 FBX 안의 논리 이름으로 출력 이름을 고정한다.
	int TextureIndex = 0;
	for (; TextureIndex < Scene->GetTextureCount() && Scene->GetTexture(TextureIndex) != T; ++TextureIndex) {}
	FString Identity = "Texture:" + FString(T->GetName()) + "[" + std::to_string(TextureIndex) + "]/" + FbxPath::ToString(FbxPath::FromString(T->GetFileName()).filename());
	const FString Key = MakeKey("T", T->GetName(), Identity);
	if (Key.empty()) return {};
	auto Asset = Data(EFbxAssetType::Texture2D, Key, Identity); Asset.Texture = std::move(Image);
	Output.Add(std::move(Asset)); Textures[Lower(TextureKey)] = Key;
	return Key;
}

FString FImport::OpacityTexture(FbxSurfaceMaterial* Material, const FString& DiffuseKey, const FString& OpacityKey,
	const FString& Identity, ESamplerState Sampler)
{
	const FImageData* Diffuse = nullptr; const FImageData* Opacity = nullptr;
	for (const auto& Asset : Output) if (Asset.Type == EFbxAssetType::Texture2D)
	{
		if (Asset.Key == DiffuseKey) Diffuse = &Asset.Texture;
		if (Asset.Key == OpacityKey) Opacity = &Asset.Texture;
	}
	if (!Opacity) { Error = "Missing opacity image: " + FString(Material->GetName()); return {}; }
	auto Supported = [](const FImageData& Image)
	{
		switch (Image.Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return Image.BytesPerPixel == 4;
		case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_A8_UNORM: return Image.BytesPerPixel == 1;
		default: return false;
		}
	};
	if (!Supported(*Opacity) || (Diffuse && !Supported(*Diffuse)))
	{
		Warn("Opacity baking skipped: " + FString(Material->GetName()) +
			"; unsupported image format (diffuse=" + (Diffuse ? std::to_string(Diffuse->Format) : "none") +
			", opacity=" + std::to_string(Opacity->Format) + "); original Diffuse and scalar opacity used");
		return {};
	}
	auto Pixel = [](const FImageData& Image, uint32 X, uint32 Y) -> std::array<uint8, 4>
	{
		const uint8* P = Image.Pixels.GetData() + static_cast<size_t>(Y) * Image.GetRowPitch() + X * Image.BytesPerPixel;
		switch (Image.Format)
		{
		case DXGI_FORMAT_R8_UNORM: return { P[0], P[0], P[0], 255 };
		case DXGI_FORMAT_A8_UNORM: return { 255, 255, 255, P[0] };
		case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return { P[2], P[1], P[0], P[3] };
		case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return { P[2], P[1], P[0], 255 };
		default: return { P[0], P[1], P[2], P[3] };
		}
	};
	const auto AlphaSource = FileTexture(Material, FbxSurfaceMaterial::sTransparentColor)->GetAlphaSource();
	bool UseAlpha = AlphaSource == FbxTexture::eBlack;
	if (AlphaSource == FbxTexture::eNone)
		for (uint32 Y = 0; Y < Opacity->Height && !UseAlpha; ++Y)
			for (uint32 X = 0; X < Opacity->Width && !UseAlpha; ++X) UseAlpha = Pixel(*Opacity, X, Y)[3] != 255;
	auto Address = [Sampler](int Value, uint32 Size)
	{
		const int Count = static_cast<int>(Size);
		return static_cast<uint32>(Sampler == ESamplerState::LinearWrap ? ((Value % Count) + Count) % Count : std::clamp(Value, 0, Count - 1));
	};
	auto OpacityAt = [&](int X, int Y)
	{
		const auto P = Pixel(*Opacity, Address(X, Opacity->Width), Address(Y, Opacity->Height));
		// 알파/불투명도 마스크는 수치 데이터다. RGB 밝기를 사용할 때도 sRGB 디코딩하지 않는다.
		return UseAlpha ? static_cast<double>(P[3]) : .2126 * P[0] + .7152 * P[1] + .0722 * P[2];
	};
	FImageData Image;
	Image.Width = Diffuse ? Diffuse->Width : Opacity->Width; Image.Height = Diffuse ? Diffuse->Height : Opacity->Height;
	Image.Format = DXGI_FORMAT_R8G8B8A8_UNORM; Image.BytesPerPixel = 4;
	Image.Pixels.SetNum(static_cast<int32>(Image.Width * Image.Height * 4));
	for (uint32 Y = 0; Y < Image.Height; ++Y) for (uint32 X = 0; X < Image.Width; ++X)
	{
		const auto Color = Diffuse ? Pixel(*Diffuse, X, Y) : std::array<uint8, 4>{ 255, 255, 255, 255 };
		uint8* P = Image.Pixels.GetData() + (static_cast<size_t>(Y) * Image.Width + X) * 4;
		std::copy_n(Color.begin(), 3, P);
		const double SX = (X + .5) * Opacity->Width / Image.Width - .5, SY = (Y + .5) * Opacity->Height / Image.Height - .5;
		const int IX = static_cast<int>(std::floor(SX)), IY = static_cast<int>(std::floor(SY));
		const double FX = SX - IX, FY = SY - IY;
		const double Top = OpacityAt(IX, IY) * (1 - FX) + OpacityAt(IX + 1, IY) * FX;
		const double Bottom = OpacityAt(IX, IY + 1) * (1 - FX) + OpacityAt(IX + 1, IY + 1) * FX;
		P[3] = static_cast<uint8>(std::clamp(std::lround(Top * (1 - FY) + Bottom * FY), 0l, 255l));
	}
	const FString SourceObject = "OpacityTexture:" + Identity;
	const FString Key = MakeKey("T", FString(Material->GetName()) + "_Opacity", SourceObject);
	if (Key.empty()) return {};
	auto Asset = Data(EFbxAssetType::Texture2D, Key, SourceObject); Asset.Texture = std::move(Image);
	Output.Add(std::move(Asset));
	return Key;
}

FString FImport::Material(FbxSurfaceMaterial* M)
{
	if (!M) return "DefaultMaterial";
	if (auto It = Materials.find(M); It != Materials.end()) return It->second;
	int SourceIndex = 0;
	for (; SourceIndex < Scene->GetMaterialCount() && Scene->GetMaterial(SourceIndex) != M; ++SourceIndex) {}
	const FString Identity = "Material:" + FString(M->GetName()) + "[" + std::to_string(SourceIndex) + "]";
	const FString Key = MakeKey("M", M->GetName(), Identity);
	if (Key.empty()) return {};
	auto Asset = Data(EFbxAssetType::Material, Key, Identity);
	FbxProperty TransparentColor = M->FindProperty(FbxSurfaceMaterial::sTransparentColor);
	float DiffuseFactor = 1.f;
	if (auto* Lambert = FbxCast<FbxSurfaceLambert>(M))
	{
		const FbxDouble3 Color = Lambert->Diffuse.Get();
		const double Factor = Lambert->DiffuseFactor.Get();
		DiffuseFactor = static_cast<float>(Factor);
		// UE Interchange의 FBX SDK 경로와 같이 기본값인 투명도 입력은 변환하지 않는다.
		const bool bConvertTransparency = TransparentColor.IsValid()
			&& (TransparentColor.GetSrcObjectCount<FbxTexture>() > 0 || !FbxProperty::HasDefaultValue(TransparentColor));
		const double Opacity = bConvertTransparency ? std::clamp(1. - Lambert->TransparencyFactor.Get(), 0., 1.) : 1.;
		// Diffuse를 선형 색상으로 해석하는 임포트 기본값이다. 강도 계수도 선형 공간에서 곱한다.
		Asset.Material.DiffuseOrFactor = FVector4(static_cast<float>(Color[0] * Factor), static_cast<float>(Color[1] * Factor),
			static_cast<float>(Color[2] * Factor), static_cast<float>(Opacity));
		Asset.Material.ShadingModel = ELightingModel::Lambert;
		Asset.Material.SpecularFactor = 0.f;
		const FbxDouble3 Emissive = Lambert->Emissive.Get();
		Asset.Material.EmissiveColor = FVector(static_cast<float>(Emissive[0]), static_cast<float>(Emissive[1]), static_cast<float>(Emissive[2]));
		Asset.Material.EmissiveFactor = static_cast<float>(Lambert->EmissiveFactor.Get());
	}
	else Warn("Unsupported material model; engine default used: " + FString(M->GetName()));
	if (auto* SourcePhong = FbxCast<FbxSurfacePhong>(M))
	{
		Asset.Material.ShadingModel = ELightingModel::BlinnPhong;
		const FbxDouble3 Specular = SourcePhong->Specular.Get();
		Asset.Material.SpecularColor = FVector(static_cast<float>(Specular[0]), static_cast<float>(Specular[1]), static_cast<float>(Specular[2]));
		Asset.Material.SpecularFactor = static_cast<float>(SourcePhong->SpecularFactor.Get());
		// Phong의 반사 벡터 지수를 Blinn-Phong의 하프 벡터 지수로 근사한다.
		Asset.Material.Shininess = static_cast<float>(4. * std::max(0., SourcePhong->Shininess.Get()));
	}
	for (const char* PropertyName : { FbxSurfaceMaterial::sSpecular, FbxSurfaceMaterial::sSpecularFactor,
		FbxSurfaceMaterial::sEmissive, FbxSurfaceMaterial::sEmissiveFactor })
	{
		const FbxProperty Property = M->FindProperty(PropertyName);
		if (Property.IsValid() && Property.GetSrcObjectCount<FbxTexture>() > 0)
			Warn("Unsupported material texture: " + FString(M->GetName()) + ", property=" + PropertyName + "; constant color/factor used");
	}
	auto* Diffuse = FileTexture(M, FbxSurfaceMaterial::sDiffuse); auto* Normal = FileTexture(M, FbxSurfaceMaterial::sNormalMap);
	auto* Opacity = FileTexture(M, FbxSurfaceMaterial::sTransparentColor);
	auto* Sampler = Diffuse ? Diffuse : (Normal ? Normal : Opacity);
	const FString SamplerName = Diffuse ? "Diffuse" : (Normal ? "Normal" : "Opacity");
	for (auto* Input : { Diffuse, Normal, Opacity }) if (Input)
	{
		if (Input != Sampler && (Sampler->GetWrapModeU() != Input->GetWrapModeU() || Sampler->GetWrapModeV() != Input->GetWrapModeV()))
			Warn("Different texture Wrap modes: " + FString(M->GetName()) + "; " + SamplerName + " Wrap selected");
		if (Input->GetWrapModeU() != Input->GetWrapModeV())
			Warn("Unsupported mixed U/V Wrap modes: " + FString(M->GetName()) + ", texture=" + Input->GetName() +
				(Input == Sampler ? "; LinearClamp selected" : "; " + SamplerName + " Wrap selected"));
	}
	if (Sampler && Sampler->GetWrapModeU() == Sampler->GetWrapModeV() && Sampler->GetWrapModeU() == FbxTexture::eRepeat)
		Asset.Material.SamplerState = ESamplerState::LinearWrap;
	Asset.Material.DiffuseTexture = Texture(M->FindProperty(FbxSurfaceMaterial::sDiffuse));
	if (!Error.empty()) return {};
	if (!Asset.Material.DiffuseTexture.empty())
	{
		// 색상 맵이 있으면 DiffuseFactor를 RGB 공통 배율로 사용한다. W는 불투명도다.
		Asset.Material.DiffuseOrFactor.X = Asset.Material.DiffuseOrFactor.Y = Asset.Material.DiffuseOrFactor.Z = DiffuseFactor;
	}
	Asset.Material.NormalTexture = Texture(M->FindProperty(FbxSurfaceMaterial::sNormalMap));
	if (!Error.empty()) return {};
	const FbxProperty Bump = M->FindProperty(FbxSurfaceMaterial::sBump);
	if (!Normal && Bump.IsValid() && Bump.GetSrcObjectCount<FbxTexture>() > 0)
		Warn("Unsupported Bump map: " + FString(M->GetName()) + "; normal input left empty");
	Asset.Material.BlendState = Asset.Material.DiffuseOrFactor.W < 1.f ? EBlendState::AlphaBlend : EBlendState::Opaque;
	const FString OpacityKey = Texture(TransparentColor);
	if (!Error.empty()) return {};
	if (!OpacityKey.empty())
	{
		const FString BakedTexture = OpacityTexture(M, Asset.Material.DiffuseTexture, OpacityKey, Identity, Asset.Material.SamplerState);
		if (!Error.empty()) return {};
		if (!BakedTexture.empty())
		{
			Asset.Material.DiffuseTexture = BakedTexture;
			// 연결된 맵이 불투명도 입력을 제공한다. 상수 계수와 원본 Diffuse 알파를 중복 적용하지 않는다.
			Asset.Material.DiffuseOrFactor.W = 1.f;
			Asset.Material.BlendState = EBlendState::AlphaBlend;
		}
		else SkippedOpacityTextures.insert(M);
	}
	Output.Add(std::move(Asset)); Materials[M] = Key;
	return Key;
}

bool FImport::AppendMesh(const FMeshItem& Item, FFbxAssetData& Asset, int LODIndex, std::map<uint32, std::vector<uint32>>& Sections)
{
	auto& LOD = Asset.LODs[LODIndex]; auto& Mesh = LOD.MeshData;
	FRig* Rig = Item.Rig ? &Rigs.at(NodePath(Item.Rig)) : nullptr;
	FbxAMatrix Full;
	if (Rig)
	{
		// Cluster.Transform은 이미 글로벌이다. Bake off는 루트 부모만 제거한다.
		Full = (Options.bBakeSkeletalMeshes ? FbxAMatrix() : Rig->ParentBind.Inverse()) * MeshBind(Item) * Geometry(Item.Node);
	}
	else if (Options.bBakeStaticMeshes) Full = Item.Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO) * Geometry(Item.Node);
	// Static Bake off는 자기 노드 변환도 적용하지 않는다. Combine이 배치를 대신 복원하지 않는다.
	const FMatrix Transform = EngineMatrix(Full, UnitScale, false);
	if (!FiniteMatrix(Transform)) { Error = "Invalid mesh transform: " + NodePath(Item.Node); return false; }
	const FMatrix NormalMatrix = Transform.Inverse().GetTransposed();
	const bool Reverse = Transform.Determinant() < 0;
	std::vector<uint32> MaterialMap;
	std::vector<FSurfaceUV> Surfaces;
	std::vector<int> SurfaceGroups;
	const int SlotCount = std::max(1, Item.Node->GetMaterialCount());
	for (int I = 0; I < SlotCount; ++I)
	{
		FbxSurfaceMaterial* SourceMaterial = I < Item.Node->GetMaterialCount() ? Item.Node->GetMaterial(I) : nullptr;
		const FString Key = Material(SourceMaterial);
		if (!Error.empty()) return false;
		const FSurfaceUV UV = SurfaceUV(SourceMaterial, Item.Mesh, NodePath(Item.Node) + (SourceMaterial ? SourceMaterial->GetName() : "Default"));
		int SurfaceGroup = I;
		for (int Previous = 0; Previous < I; ++Previous)
			if (UV.Set == Surfaces[Previous].Set && Near(UV.Transform, Surfaces[Previous].Transform)) { SurfaceGroup = SurfaceGroups[Previous]; break; }
		Surfaces.push_back(UV); SurfaceGroups.push_back(SurfaceGroup);
		int Index = -1;
		for (int M = 0; M < Asset.MaterialKeys.Num(); ++M) if (Asset.MaterialKeys[M] == Key) { Index = M; break; }
		if (Index < 0)
		{
			Index = Asset.MaterialKeys.Num(); Asset.MaterialKeys.Add(Key);
			// 모든 LOD는 애셋 단위 슬롯 인덱스를 사용한다.
			for (auto& Existing : Asset.LODs)
			{
				FStaticMaterialSlot Slot; Slot.Name = SourceMaterial ? SourceMaterial->GetName() : "Default";
				Existing.MeshData.MaterialSlots.Add(std::move(Slot));
			}
		}
		MaterialMap.push_back(static_cast<uint32>(Index));
	}
	const int CPCount = Item.Mesh->GetControlPointsCount();
	std::vector<std::map<uint16, float>> Influences(Rig ? CPCount : 0);
	if (Rig)
	{
		if (Item.Mesh->GetDeformerCount(FbxDeformer::eSkin))
		{
			auto* Skin = static_cast<FbxSkin*>(Item.Mesh->GetDeformer(0, FbxDeformer::eSkin));
			for (int C = 0; C < Skin->GetClusterCount(); ++C)
			{
				FbxCluster* Cluster = Skin->GetCluster(C);
				if (!Cluster || !Cluster->GetLink() || IsCompensation(Cluster)) continue;
				if (!Rig->Indices.contains(Cluster->GetLink())) { Error = "Bone is not in the mesh reference skeleton"; return false; }
				if (Cluster->GetLinkMode() == FbxCluster::eAdditive) Warn("Additive Link Mode is approximated using normalized Linear weights: " + NodePath(Item.Node));
				const uint16 Bone = Rig->Indices.at(Cluster->GetLink());
				for (int I = 0; I < Cluster->GetControlPointIndicesCount(); ++I)
				{
					const int CP = Cluster->GetControlPointIndices()[I]; const double W = Cluster->GetControlPointWeights()[I];
					if (CP < 0 || CP >= CPCount || !std::isfinite(W)) { Error = "Invalid control point influence"; return false; }
					if (W > 0) Influences[CP][Bone] = std::max(Influences[CP][Bone], static_cast<float>(W));
				}
			}
		}
		else
		{
			const uint16 Bone = Rig->Indices.at(Item.Node);
			for (auto& Vertex : Influences) Vertex[Bone] = 1;
		}
	}
	std::unordered_map<FCornerKey, uint32, FCornerHash> Cache;
	int32 PositionBase = 0;
	for (const auto& SourceVertex : Mesh.LODSourceVertices) PositionBase = std::max(PositionBase, SourceVertex.PositionIndex + 1);
	const int32 AttributeBase = Mesh.Vertices.Num();
	std::map<std::array<float, 2>, int32> UVIndices;
	std::map<std::array<float, 3>, int32> NormalIndices;
	FbxGeometryElementMaterial* MaterialsElement = Item.Mesh->GetElementMaterial();
	for (int P = 0; P < Item.Mesh->GetPolygonCount(); ++P)
	{
		if (Item.Mesh->GetPolygonSize(P) != 3) { Error = "FBX triangulation did not produce triangles"; return false; }
		int Slot = 0;
		if (MaterialsElement && MaterialsElement->GetIndexArray().GetCount())
		{
			const int Index = MaterialsElement->GetMappingMode() == FbxLayerElement::eByPolygon ? P : 0;
			if (Index < MaterialsElement->GetIndexArray().GetCount()) Slot = MaterialsElement->GetIndexArray().GetAt(Index);
		}
		if (Slot < 0 || Slot >= static_cast<int>(MaterialMap.size())) { Warn("Invalid material slot replaced with the first slot: " + NodePath(Item.Node)); Slot = 0; }
		const auto& Surface = Surfaces[Slot];
		FVertex Vertices[3]; int CPs[3]; bool MappedUVs[3]{};
		for (int C = 0; C < 3; ++C)
		{
			const int CP = Item.Mesh->GetPolygonVertex(P, C); CPs[C] = CP;
			if (CP < 0 || CP >= CPCount) { Error = "Invalid polygon control point index"; return false; }
			auto& V = Vertices[C];
			V.Tangent = FVector4(0, 0, 0, 0);
			V.Position = Reflect(Full.MultT(Item.Mesh->GetControlPointAt(CP))) * static_cast<float>(UnitScale);
			FbxVector4 N;
			if (Item.Mesh->GetPolygonVertexNormal(P, C, N)) V.Normal = NormalMatrix.TransformVector(FVector(static_cast<float>(N[0]), static_cast<float>(N[1]), static_cast<float>(N[2]))).GetSafeNormal();
			if (!Surface.Set.empty())
			{
				FbxVector2 UV; bool Unmapped = false;
				if (Item.Mesh->GetPolygonVertexUV(P, C, Surface.Set.c_str(), UV, Unmapped) && !Unmapped)
				{
					UV = Surface.Transform.Apply(UV); MappedUVs[C] = true;
					V.UV = FVector2(static_cast<float>(UV[0]), 1.f - static_cast<float>(UV[1]));
				}
				else Warn("Unmapped UV corner: " + NodePath(Item.Node) + ", selected=" + Surface.Set + "; zero UV used");
			}
			if (auto* Colors = Item.Mesh->GetElementVertexColor())
			{
				int Index = -1;
				switch (Colors->GetMappingMode())
				{
				case FbxLayerElement::eByControlPoint: Index = CP; break;
				case FbxLayerElement::eByPolygonVertex: Index = Item.Mesh->GetPolygonVertexIndex(P) + C; break;
				case FbxLayerElement::eByPolygon: Index = P; break;
				case FbxLayerElement::eAllSame: Index = 0; break;
				default: break;
				}
				if (Colors->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
					Index = Index >= 0 && Index < Colors->GetIndexArray().GetCount() ? Colors->GetIndexArray().GetAt(Index) : -1;
				if (Index >= 0 && Index < Colors->GetDirectArray().GetCount())
				{
					const FbxColor Color = Colors->GetDirectArray().GetAt(Index);
					V.Color = FVector4(static_cast<float>(Color.mRed), static_cast<float>(Color.mGreen), static_cast<float>(Color.mBlue), static_cast<float>(Color.mAlpha));
				}
			}
		}
		FVector FaceNormal = (Vertices[1].Position - Vertices[0].Position).Cross(Vertices[2].Position - Vertices[0].Position).GetSafeNormal();
		if (Reverse) FaceNormal = FaceNormal * -1.f;
		uint32 Corners[3];
		for (int C = 0; C < 3; ++C)
		{
			if (Vertices[C].Normal.SizeSquared() < 1.e-12f) Vertices[C].Normal = FaceNormal;
			if (MappedUVs[C] && !ImportTangent(Surface, Item.Mesh, P, C, Transform, Vertices[C]) && Surface.Tangents)
				Warn("Invalid source tangent basis: " + NodePath(Item.Node) + ", UV set=" + Surface.Set + "; affected tangents recalculated");
			const auto& V = Vertices[C];
			if (!std::isfinite(V.Position.X) || !std::isfinite(V.Position.Y) || !std::isfinite(V.Position.Z) ||
				!std::isfinite(V.Normal.X) || !std::isfinite(V.Normal.Y) || !std::isfinite(V.Normal.Z) ||
				!std::isfinite(V.UV.X) || !std::isfinite(V.UV.Y)) { Error = "Invalid FBX vertex attribute"; return false; }
			const FCornerKey Key{ CPs[C], SurfaceGroups[Slot], Vertices[C] };
			if (auto It = Cache.find(Key); It != Cache.end()) { Corners[C] = It->second; continue; }
			Corners[C] = Mesh.Vertices.Num(); Mesh.Vertices.Add(Vertices[C]); Cache.emplace(Key, Corners[C]);
			// 기존 스태틱 LOD 생성기에 필요한 원본 위치/이음매 식별자다.
			const auto UV = UVIndices.try_emplace({ V.UV.X, V.UV.Y }, AttributeBase + static_cast<int32>(UVIndices.size())).first->second;
			const auto Normal = NormalIndices.try_emplace({ V.Normal.X, V.Normal.Y, V.Normal.Z }, AttributeBase + static_cast<int32>(NormalIndices.size())).first->second;
			Mesh.LODSourceVertices.Add({ PositionBase + CPs[C], UV, Normal });
			if (Rig)
			{
				std::vector<std::pair<uint16, float>> Weights(Influences[CPs[C]].begin(), Influences[CPs[C]].end());
				std::sort(Weights.begin(), Weights.end(), [](auto A, auto B) { return A.second != B.second ? A.second > B.second : A.first < B.first; });
				if (Weights.size() > 4) Warn("Weights reduced to four influences: " + NodePath(Item.Node));
				Weights.resize(std::min<size_t>(Weights.size(), 4));
				std::array<uint16, 4> Bones{}; std::array<float, 4> Values{};
				float Total = 0; for (auto W : Weights) Total += W.second;
				if (Total <= 0) { Values[0] = 1; Warn("Unweighted vertices attached to root: " + NodePath(Item.Node)); }
				else for (size_t I = 0; I < Weights.size(); ++I) { Bones[I] = Weights[I].first; Values[I] = Weights[I].second / Total; }
				LOD.SkinWeights.InfluenceBones.Add(Bones); LOD.SkinWeights.InfluenceWeights.Add(Values);
			}
		}
		auto& Indices = Sections[MaterialMap[Slot]];
		Indices.push_back(Corners[0]); Indices.push_back(Corners[Reverse ? 2 : 1]); Indices.push_back(Corners[Reverse ? 1 : 2]);
	}
	return true;
}

void FImport::LODThresholds(const FOutputGroup& Group, FFbxAssetData& Asset)
{
	const auto& Box = Asset.LODs[0].MeshData.AABB;
	const float Radius = (Box.Max - Box.Min).Size() * .5f;
	std::set<FbxNode*> Seen;
	bool Set[3]{};
	for (auto* Item : Group.Items)
	{
		if (!Item->LODGroupNode || !Seen.insert(Item->LODGroupNode).second) continue;
		FbxLODGroup* LOD = nullptr;
		for (int A = 0; A < Item->LODGroupNode->GetNodeAttributeCount(); ++A)
			if (Item->LODGroupNode->GetNodeAttributeByIndex(A)->GetAttributeType() == FbxNodeAttribute::eLODGroup)
				LOD = static_cast<FbxLODGroup*>(Item->LODGroupNode->GetNodeAttributeByIndex(A));
		if (!LOD) continue;
		if (LOD->MinMaxDistance.Get()) Warn("LOD Min/MaxDistance visibility is not imported: " + NodePath(Item->LODGroupNode));
		for (int I = 0; I < std::min(3, LOD->GetNumThresholds()); ++I)
		{
			float Threshold = 0;
			if (LOD->ThresholdsUsedAsPercentage.Get())
			{
				FbxDouble Percentage = 0;
				if (LOD->GetThreshold(I, Percentage)) Threshold = static_cast<float>(Percentage / 100.);
			}
			else
			{
				FbxDistance Distance;
				if (LOD->GetThreshold(I, Distance))
				{
					// FbxDistance는 자체 단위를 갖는다. 장면 단위를 중복 곱하지 않는다.
					const double D = Distance.valueAs(FbxSystemUnit::m);
					if (D > 0) Threshold = static_cast<float>(Radius * 1.7320508075688772 / D);
				}
			}
			if (Threshold > 0 && std::isfinite(Threshold))
			{
				Asset.ScreenThresholds[I] = Set[I] ? std::min(Asset.ScreenThresholds[I], Threshold) : Threshold; Set[I] = true;
			}
		}
	}
	for (int I = 1; I < 3; ++I)
		if (Asset.ScreenThresholds[I] >= Asset.ScreenThresholds[I - 1]) Asset.ScreenThresholds[I] = Asset.ScreenThresholds[I - 1] * .5f;
}

bool FImport::CookMesh(const FOutputGroup& Group)
{
	const bool Skinned = Group.Root != nullptr;
	const auto Type = Skinned ? EFbxAssetType::SkeletalMesh : EFbxAssetType::StaticMesh;
	const FString Key = MakeKey(Skinned ? "SK" : "SM", Group.Label, Group.Identity);
	if (Key.empty()) return false;
	auto Asset = Data(Type, Key, Group.Identity);
	int MaxLOD = 0; for (auto* Item : Group.Items) MaxLOD = std::max(MaxLOD, Item->LOD);
	Asset.LODs.SetNum(std::min(4, MaxLOD + 1));
	if (Skinned) { const auto& Rig = Rigs.at(NodePath(Group.Root)); Asset.RefSkeleton = Rig.Reference; Asset.SkeletonKey = Rig.MeshSkeletonKey; }
	for (int L = 0; L < Asset.LODs.Num(); ++L)
	{
		std::map<uint32, std::vector<uint32>> Sections;
		// 그룹 밖 조각은 모든 단계에 포함한다. 그룹의 누락 LOD는 마지막 기하를 반복하지 않는다.
		for (auto* Item : Group.Items) if (Item->LOD < 0 || Item->LOD == L)
			if (!AppendMesh(*Item, Asset, L, Sections)) return false;
		auto& Mesh = Asset.LODs[L].MeshData;
		for (const auto& [MaterialIndex, Indices] : Sections)
		{
			Mesh.Sections.Add({ static_cast<uint32>(Mesh.Indices.Num()), static_cast<uint32>(Indices.size()), MaterialIndex });
			for (auto Index : Indices) Mesh.Indices.Add(Index);
		}
		if (!Mesh.Vertices.IsEmpty())
		{
			std::vector<std::pair<int32, FVector4>> SourceTangents;
			for (int32 I = 0; I < Mesh.Vertices.Num(); ++I)
				if (Mesh.Vertices[I].Tangent.W != 0) SourceTangents.emplace_back(I, Mesh.Vertices[I].Tangent);
			if (SourceTangents.size() != static_cast<size_t>(Mesh.Vertices.Num()))
			{
				Mesh.BuildTangentSpace();
				for (const auto& [Index, Tangent] : SourceTangents) Mesh.Vertices[Index].Tangent = Tangent;
			}
			FVector Min = Mesh.Vertices[0].Position, Max = Min;
			for (const auto& V : Mesh.Vertices)
			{
				Min.X = std::min(Min.X, V.Position.X); Min.Y = std::min(Min.Y, V.Position.Y); Min.Z = std::min(Min.Z, V.Position.Z);
				Max.X = std::max(Max.X, V.Position.X); Max.Y = std::max(Max.Y, V.Position.Y); Max.Z = std::max(Max.Z, V.Position.Z);
			}
			Mesh.AABB = FBox{ Min, Max };
		}
	}
	if (Asset.LODs[0].MeshData.Vertices.IsEmpty()) { Error = "No visible LOD0 geometry: " + Group.Label; return false; }
	LODThresholds(Group, Asset);
	if (!Asset.Validate(Error)) return false;
	Output.Add(std::move(Asset));
	return true;
}

bool FImport::Run()
{
	const bool bImportAnimations = Options.bImportAnimations || Options.bImportOnlyAnimations;
	if (Options.bImportOnlyAnimations && Options.AnimationSkeletonKey.empty())
	{ Error = "Import Only Animations requires an existing Animation Skeleton"; return false; }
	if (bImportAnimations && (Options.CustomBoneAnimationSampleRate < 0 ||
		(Options.AnimationRange == EFbxAnimationRange::SetRange && Options.FirstAnimationFrame > Options.LastAnimationFrame)))
	{ Error = "Invalid animation sample rate or frame range"; return false; }
	if (!Options.bImportOnlyAnimations && !LoadSelectedSkeleton(Options.SkeletalMeshSkeletonKey)) return false;
	if (bImportAnimations && !LoadSelectedSkeleton(Options.AnimationSkeletonKey)) return false;
	auto* MeshSkeleton = !Options.bImportOnlyAnimations && !Options.SkeletalMeshSkeletonKey.empty()
		? &SelectedSkeletons.at(Options.SkeletalMeshSkeletonKey) : nullptr;
	const int32 OriginalMeshBoneCount = MeshSkeleton ? MeshSkeleton->RefSkeleton.GetNum() : 0;

	Gather(Scene->GetRootNode(), true);
	FindRigs(); if (!Error.empty()) return false;
	PreparePoses();
	for (auto& [Path, Rig] : Rigs)
	{
		const bool bImportMesh = !Options.bImportOnlyAnimations && Rig.bHasMesh;
		if ((bImportMesh || (bImportAnimations && Rig.bHasTransformAnimation)) && !BuildRig(Rig)) return false;
		if (bImportMesh && !ChooseMeshSkeleton(Rig)) return false;
	}
	std::map<FString, FOutputGroup> Groups;
	if (!Options.bImportOnlyAnimations)
	{
		for (auto& Item : Meshes)
		{
			if (Item.Rig && Rigs.at(NodePath(Item.Rig)).MeshSkeletonKey.empty()) continue;
			const bool Combine = Item.Rig ? Options.bCombineSkeletalMeshes : Options.bCombineStaticMeshes;
			const FString Identity = Item.Rig ? "Skeletal:" + NodePath(Item.Rig) + (Combine ? "Combined" : Item.UnitKey)
				: "Static:" + (Combine ? FString("Combined") : Item.UnitKey);
			auto& Group = Groups[Identity]; Group.Identity = Identity; Group.Root = Item.Rig;
			Group.Label = Combine ? (Item.Rig ? FString(Item.Rig->GetName()) : FString("Combined")) : Item.Label;
			Group.Items.push_back(&Item);
		}
	}
	for (const auto& [Key, Group] : Groups) if (!CookMesh(Group)) return false;
	if (bImportAnimations)
	{
		if (Rigs.empty())
			Warn("No skeleton joint or node matching the selected Skeleton root was found");
		for (auto& [Path, Rig] : Rigs)
		{
			if (!Rig.bHasTransformAnimation)
			{
				Warn("Animation skipped for rig without animated transforms [" + Path + "], " +
					std::to_string(Rig.Needed.size()) + " node(s)");
				continue;
			}
			if (!CookAnimations(Rig)) return false;
		}
	}
	if (MeshSkeleton && MeshSkeleton->RefSkeleton.GetNum() != OriginalMeshBoneCount)
	{
		Warn("Added " + std::to_string(MeshSkeleton->RefSkeleton.GetNum() - OriginalMeshBoneCount) +
			" bone(s) to selected mesh Skeleton: " + MeshSkeleton->Key);
		Output.Add(std::move(*MeshSkeleton));
	}
	std::set<FString> ReferencedTextures;
	for (const auto& Asset : Output) if (Asset.Type == EFbxAssetType::Material)
	{
		ReferencedTextures.insert(Asset.Material.DiffuseTexture); ReferencedTextures.insert(Asset.Material.NormalTexture);
	}
	for (int32 I = Output.Num() - 1; I >= 0; --I)
		if (Output[I].Type == EFbxAssetType::Texture2D && !ReferencedTextures.contains(Output[I].Key)) Output.RemoveAt(I);
	if (Output.IsEmpty())
	{
		Error = Options.bImportOnlyAnimations ? "No compatible bone animation was found for the selected Skeleton"
			: SelectedSkeletons.empty() ? "No supported FBX assets found; animation-only files require an Animation Skeleton"
			: "No supported FBX assets were compatible with the selected Skeletons";
		return false;
	}
	for (const auto& Asset : Output) if (!Asset.Validate(Error)) return false;
	return true;
}
}
#endif

bool FFbxImporter::IsAvailable() { return WITH_FBX_SDK != 0; }

bool FFbxImporter::Cook(const FString& SourcePath, const FFbxImportOptions& Options, const FString& ExtractionDirectory,
	TArray<FFbxAssetData>& Out, TArray<FString>& Warnings, FString& Error, const FString& OutputDirectory)
{
	Out.Empty(); Warnings.Empty(); Error.clear();
#if WITH_FBX_SDK
	try
	{
		FString SourceKey;
		fs::path Source, Destination, Extraction;
		if (!FbxPath::GetSourcePath(SourcePath, Source, SourceKey, Error) ||
			!FbxPath::GetOutputDirectory(OutputDirectory.empty() ? FbxPath::DefaultOutputDirectory(SourcePath) : OutputDirectory, Destination, Error) ||
			!FbxPath::GetOutputDirectory(ExtractionDirectory, Extraction, Error)) return false;
		struct FManagerDelete { void operator()(FbxManager* P) const { if (P) P->Destroy(); } };
		std::unique_ptr<FbxManager, FManagerDelete> Manager(FbxManager::Create());
		if (!Manager) { Error = "Cannot create FBX SDK manager"; return false; }
		Manager->SetIOSettings(FbxIOSettings::Create(Manager.get(), IOSROOT));
		FbxImporter* Parser = FbxImporter::Create(Manager.get(), "");
		const FString CanonicalSource = FbxPath::ToString(Source);
		const FString CanonicalExtraction = FbxPath::ToString(Extraction);
		if (!Parser->Initialize(CanonicalSource.c_str(), -1, Manager->GetIOSettings())) { Error = Parser->GetStatus().GetErrorString(); return false; }
		Parser->SetEmbeddingExtractionFolder(CanonicalExtraction.c_str());
		FbxScene* Scene = FbxScene::Create(Manager.get(), "Import");
		if (!Parser->Import(Scene)) { Error = Parser->GetStatus().GetErrorString(); return false; }
		if (const int32 Repaired = RepairDetachedCurveNodes(Scene))
			Warnings.Add("Reattached " + std::to_string(Repaired) + " animation curve nodes missing from the layer");
		bool Blender = false;
		if (auto* Info = Scene->GetSceneInfo()) Blender = Lower(Info->Original_ApplicationName.Get().Buffer()).find("blender") != FString::npos;
		const double UnitScale = Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor() / 100.;
		if (!std::isfinite(UnitScale) || UnitScale <= 0) { Error = "Invalid FBX scene unit"; return false; }
		// UE의 Z-up/-ParityOdd 기준을 사용한다. Deep 변환으로 Bake off의 로컬 기하도 같은 축이 된다.
		FbxAxisSystem Target(FbxAxisSystem::eZAxis, static_cast<FbxAxisSystem::EFrontVector>(-FbxAxisSystem::eParityOdd), FbxAxisSystem::eRightHanded);
		Target.DeepConvertScene(Scene);
		FbxGeometryConverter Converter(Manager.get());
		if (!Options.bImportOnlyAnimations && !Converter.Triangulate(Scene, true)) { Error = "FBX SDK triangulation failed"; return false; }
		FImport Import(Scene, Options, SourceKey, Source, Destination, Out, Warnings, Error);
		Import.UnitScale = UnitScale; Import.bBlender = Blender;
		const bool Result = Import.Run();
		if (!Result) Out.Empty();
		return Result;
	}
	catch (const std::exception& E) { Error = E.what(); Out.Empty(); return false; }
#else
	Error = "FBX SDK is not configured. Set FBX_SDK_ROOT and regenerate the solution; existing .fbxbin assets can still be loaded.";
	return false;
#endif
}
