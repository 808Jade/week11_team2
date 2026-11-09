#include "EnginePCH.h"
#include "HumanoidRetarget.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace
{
	// 접두어, 대소문자, 구분자, 끝 번호, 좌우 표기를 통일한 비교용 이름(Mixamo 키 기준).
	// Mixamo:      "mixamorig:LeftArm", "mixamorig_LeftArm_09"          → "leftarm"
	// Max Biped:   "ValveBiped_Bip01_L_UpperArm", "Bip01 L UpperArm"   → "leftarm"
	// UE 마네킹:   "upperarm_l", "thigh_r", "spine_01"                  → "leftarm", "rightupleg", "spine"
	FString HumanoidKey(const FString& Name)
	{
		FString Base = Name.substr(Name.find_last_of(':') == FString::npos ? 0 : Name.find_last_of(':') + 1);
		for (char& C : Base) if (C >= 'A' && C <= 'Z') C = static_cast<char>(C - 'A' + 'a');

		// 구분자('_', ' ', '.', '-')로 나눈다.
		TArray<FString> Tokens;
		FString Token;
		for (const char C : Base)
		{
			if ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9')) { Token += C; continue; }
			if (!Token.empty()) Tokens.Add(std::move(Token));
			Token.clear();
		}
		if (!Token.empty()) Tokens.Add(std::move(Token));

		auto IsNumber = [](const FString& S) { return !S.empty() && std::all_of(S.begin(), S.end(), [](char C) { return C >= '0' && C <= '9'; }); };
		// 끝 번호(중복 이름 접미사, "spine_01")는 지운다. 단독 토큰이면 남긴다.
		if (Tokens.Num() > 1 && IsNumber(Tokens.Last())) Tokens.Pop();

		FString Side, Part;
		for (FString& T : Tokens)
		{
			if (T.starts_with("mixamorig")) T.erase(0, 9);
			if (T.empty() || T == "valvebiped" || T == "bip01" || T == "bip001" || T == "biped") continue;
			if (T == "l" || T == "left") { Side = "left"; continue; }
			if (T == "r" || T == "right") { Side = "right"; continue; }
			Part += T;
		}
		// "leftarm"처럼 붙어 있는 좌우 표기도 분리한다.
		for (const char* Prefix : { "left", "right" })
			if (Side.empty() && Part.starts_with(Prefix) && Part.size() > strlen(Prefix))
			{
				Side = Prefix;
				Part.erase(0, strlen(Prefix));
			}

		static const std::unordered_map<FString, FString> Aliases{
			{ "pelvis", "hips" }, { "hip", "hips" }, { "head1", "head" },
			{ "upperarm", "arm" }, { "lowerarm", "forearm" },
			{ "upperleg", "upleg" }, { "thigh", "upleg" },
			{ "lowerleg", "leg" }, { "calf", "leg" },
			{ "toe", "toebase" }, { "toe0", "toebase" }, { "ball", "toebase" },
			{ "clavicle", "shoulder" },
		};
		if (const auto It = Aliases.find(Part); It != Aliases.end()) Part = It->second;
		return Side + Part;
	}

	// 단위 벡터 From을 To로 돌리는 최소 회전(row-vector 행렬).
	FMatrix RotationBetween(const FVector& From, const FVector& To)
	{
		const float Cos = std::clamp(From.Dot(To), -1.0f, 1.0f);
		if (Cos > 0.99999f) return FMatrix::Identity;
		FVector Axis = From.Cross(To);
		if (Axis.Length() < 1.e-5f)
		{
			// 정반대: From에 수직인 아무 축으로 180° 돌린다.
			Axis = From.Cross(std::abs(From.X) < 0.9f ? FVector(1, 0, 0) : FVector(0, 1, 0));
		}
		Axis = Axis.Normalized();
		const float Half = std::acos(Cos) * 0.5f;
		const float S = std::sin(Half);
		return FQuat(Axis.X * S, Axis.Y * S, Axis.Z * S, std::cos(Half)).ToFMatrix();
	}

	FMatrix RotationMatrix(const FTransform& Transform) { return Transform.Rotation.Quaternion().ToFMatrix(); }

	// FQuat::ToFMatrix의 역변환. 행렬은 순수 회전이라고 가정한다.
	FQuat QuatFromRotationMatrix(const FMatrix& M)
	{
		const float Trace = M.M[0][0] + M.M[1][1] + M.M[2][2];
		float X, Y, Z, W;
		if (Trace > 0.0f)
		{
			const float S = std::sqrt(Trace + 1.0f) * 2.0f; // 4W
			W = 0.25f * S;
			X = (M.M[1][2] - M.M[2][1]) / S;
			Y = (M.M[2][0] - M.M[0][2]) / S;
			Z = (M.M[0][1] - M.M[1][0]) / S;
		}
		else if (M.M[0][0] > M.M[1][1] && M.M[0][0] > M.M[2][2])
		{
			const float S = std::sqrt(1.0f + M.M[0][0] - M.M[1][1] - M.M[2][2]) * 2.0f; // 4X
			X = 0.25f * S;
			W = (M.M[1][2] - M.M[2][1]) / S;
			Y = (M.M[0][1] + M.M[1][0]) / S;
			Z = (M.M[2][0] + M.M[0][2]) / S;
		}
		else if (M.M[1][1] > M.M[2][2])
		{
			const float S = std::sqrt(1.0f + M.M[1][1] - M.M[0][0] - M.M[2][2]) * 2.0f; // 4Y
			Y = 0.25f * S;
			W = (M.M[2][0] - M.M[0][2]) / S;
			X = (M.M[0][1] + M.M[1][0]) / S;
			Z = (M.M[1][2] + M.M[2][1]) / S;
		}
		else
		{
			const float S = std::sqrt(1.0f + M.M[2][2] - M.M[0][0] - M.M[1][1]) * 2.0f; // 4Z
			Z = 0.25f * S;
			W = (M.M[0][1] - M.M[1][0]) / S;
			X = (M.M[2][0] + M.M[0][2]) / S;
			Y = (M.M[1][2] + M.M[2][1]) / S;
		}
		return FQuat(X, Y, Z, W).Normalized();
	}

	FVector Origin(const FMatrix& M) { return FVector(M.M[3][0], M.M[3][1], M.M[3][2]); }

	// row-vector: V × M의 회전 부분
	FVector RotateRow(const FVector& V, const FMatrix& M)
	{
		return FVector(
			V.X * M.M[0][0] + V.Y * M.M[1][0] + V.Z * M.M[2][0],
			V.X * M.M[0][1] + V.Y * M.M[1][1] + V.Z * M.M[2][1],
			V.X * M.M[0][2] + V.Y * M.M[1][2] + V.Z * M.M[2][2]);
	}

	// 캐릭터 기준 축을 행으로 갖는 회전 행렬. 행0=오른쪽(왼다리→오른다리), 행2=위(Hips→Head), 행1=위×오른쪽.
	bool CharacterFrame(const FVector& Right, const FVector& Up, FMatrix& Out)
	{
		if (Right.Length() < 1.e-4f || Up.Length() < 1.e-4f) return false;
		const FVector X = Right.Normalized();
		const FVector UpOrtho = Up - X * Up.Dot(X);
		if (UpOrtho.Length() < 1.e-4f) return false;
		const FVector Z = UpOrtho.Normalized();
		const FVector Y = Z.Cross(X);
		Out = FMatrix::Identity;
		for (int32 I = 0; I < 3; ++I)
		{
			Out.M[0][I] = X.V[I];
			Out.M[1][I] = Y.V[I];
			Out.M[2][I] = Z.V[I];
		}
		return true;
	}

	// 부모가 먼저 오는 순서를 이용해 월드 회전(회전만)을 누적한다.
	void WorldRotations(const FReferenceSkeleton& Skeleton, const TArray<FTransform>& Local, TArray<FMatrix>& Out)
	{
		Out.SetNum(Skeleton.GetNum());
		for (int32 I = 0; I < Skeleton.GetNum(); ++I)
		{
			const int32 Parent = Skeleton.RefBoneInfo[I].ParentIndex;
			Out[I] = Parent >= 0 ? RotationMatrix(Local[I]) * Out[Parent] : RotationMatrix(Local[I]);
		}
	}

	void WorldMatrices(const FReferenceSkeleton& Skeleton, const TArray<FTransform>& Local, TArray<FMatrix>& Out)
	{
		Out.SetNum(Skeleton.GetNum());
		for (int32 I = 0; I < Skeleton.GetNum(); ++I)
		{
			const int32 Parent = Skeleton.RefBoneInfo[I].ParentIndex;
			Out[I] = Parent >= 0 ? Local[I].GetLocalMatrix() * Out[Parent] : Local[I].GetLocalMatrix();
		}
	}

	struct FHumanoidLayout
	{
		TArray<FString> Keys;
		int32 Hips = INDEX_NONE;
		TArray<int32> Spine; // Hips와 첫 Neck 사이 (Hips 쪽부터)
		TArray<int32> Neck;  // 첫 Neck부터 Head 직전까지
	};

	FHumanoidLayout BuildLayout(const FReferenceSkeleton& Skeleton)
	{
		FHumanoidLayout Layout;
		int32 Head = INDEX_NONE;
		for (int32 I = 0; I < Skeleton.GetNum(); ++I)
		{
			Layout.Keys.Add(HumanoidKey(Skeleton.RefBoneInfo[I].Name));
			if (Layout.Keys[I] == "hips" && Layout.Hips == INDEX_NONE) Layout.Hips = I;
			if (Layout.Keys[I] == "head" && Head == INDEX_NONE) Head = I;
		}
		if (Layout.Hips == INDEX_NONE || Head == INDEX_NONE) return Layout;

		// Head에서 Hips까지 올라가며 중간 본을 모은다. Hips가 조상이 아니면 체인을 만들지 않는다.
		TArray<int32> Path;
		int32 Bone = Skeleton.RefBoneInfo[Head].ParentIndex;
		while (Bone != INDEX_NONE && Bone != Layout.Hips)
		{
			Path.Add(Bone);
			Bone = Skeleton.RefBoneInfo[Bone].ParentIndex;
		}
		if (Bone != Layout.Hips) return Layout;
		std::reverse(Path.begin(), Path.end());

		bool bInNeck = false;
		for (const int32 Index : Path)
		{
			bInNeck |= Layout.Keys[Index].starts_with("neck");
			(bInNeck ? Layout.Neck : Layout.Spine).Add(Index);
		}
		return Layout;
	}

	// 길이가 다른 두 체인을 비율로 대응시킨다. 예: 소스 Spine~Spine3(4개) → 타깃 Spine~Spine2(3개)
	void MapChain(const TArray<int32>& SourceChain, const TArray<int32>& TargetChain, TArray<int32>& TargetToSource)
	{
		if (SourceChain.IsEmpty()) return;
		const int32 SourceLast = SourceChain.Num() - 1;
		const int32 TargetLast = TargetChain.Num() - 1;
		for (int32 J = 0; J < TargetChain.Num(); ++J)
		{
			const int32 S = TargetLast <= 0 ? 0
				: static_cast<int32>(std::lround(static_cast<double>(J) * SourceLast / TargetLast));
			TargetToSource[TargetChain[J]] = SourceChain[std::clamp(S, 0, SourceLast)];
		}
	}
}

FString FHumanoidRetargeter::HumanoidBoneKey(const FString& Name)
{
	return HumanoidKey(Name);
}

bool FHumanoidRetargeter::Initialize(const FReferenceSkeleton& InSource, const FReferenceSkeleton& InTarget, FString& Error)
{
	if (!InSource.Validate(Error) || !InTarget.Validate(Error)) return false;
	Source = &InSource;
	Target = &InTarget;

	const FHumanoidLayout SourceLayout = BuildLayout(InSource);
	const FHumanoidLayout TargetLayout = BuildLayout(InTarget);
	if (SourceLayout.Hips == INDEX_NONE || TargetLayout.Hips == INDEX_NONE)
	{
		Error = SourceLayout.Hips == INDEX_NONE ? "Humanoid retarget: source has no Hips/Pelvis bone"
			: "Humanoid retarget: target Skeleton has no Hips/Pelvis bone";
		return false;
	}
	SourceHips = SourceLayout.Hips;
	TargetHips = TargetLayout.Hips;

	// 1) 같은 정규화 이름끼리 연결한다. 한 Skeleton 안에서 겹치는 이름은 모호하므로 쓰지 않는다.
	std::unordered_map<FString, int32> SourceByKey;
	for (int32 I = 0; I < InSource.GetNum(); ++I)
	{
		if (SourceLayout.Keys[I].empty()) continue; // "Bip01"처럼 접두어만 있는 본
		const auto [It, bAdded] = SourceByKey.emplace(SourceLayout.Keys[I], I);
		if (!bAdded) It->second = INDEX_NONE;
	}
	TargetToSource.SetNum(InTarget.GetNum());
	std::fill(TargetToSource.begin(), TargetToSource.end(), INDEX_NONE);
	std::unordered_map<FString, int32> TargetKeyCount;
	for (const FString& Key : TargetLayout.Keys) ++TargetKeyCount[Key];
	for (int32 I = 0; I < InTarget.GetNum(); ++I)
		if (!TargetLayout.Keys[I].empty() && TargetKeyCount[TargetLayout.Keys[I]] == 1)
			if (const auto It = SourceByKey.find(TargetLayout.Keys[I]); It != SourceByKey.end())
				TargetToSource[I] = It->second;

	// 2) 척추·목은 본 개수가 달라도 대응되도록 체인 비율로 덮어쓴다.
	MapChain(SourceLayout.Spine, TargetLayout.Spine, TargetToSource);
	MapChain(SourceLayout.Neck, TargetLayout.Neck, TargetToSource);
	TargetToSource[TargetHips] = SourceHips;

	// 3) 기준 자세의 월드 회전 차이를 미리 구한다.
	TArray<FMatrix> SourceRefWorldRot, TargetRefWorldRot, SourceRefWorld, TargetRefWorld;
	WorldRotations(InSource, InSource.RefBonePose, SourceRefWorldRot);
	WorldRotations(InTarget, InTarget.RefBonePose, TargetRefWorldRot);
	WorldMatrices(InSource, InSource.RefBonePose, SourceRefWorld);
	WorldMatrices(InTarget, InTarget.RefBonePose, TargetRefWorld);

	// 4) 두 캐릭터가 기준 자세에서 바라보는 방향이 다를 수 있다(예: Ronaldo는 -Z, Mixamo는 +Z를 본다).
	//    월드 변화량을 그대로 옮기면 앞뒤·좌우가 뒤집혀 무릎·팔꿈치가 반대로 꺾이므로 캐릭터 축끼리 맞춘다.
	auto FindKey = [](const FHumanoidLayout& Layout, const char* Key)
	{
		for (int32 I = 0; I < Layout.Keys.Num(); ++I) if (Layout.Keys[I] == Key) return I;
		return static_cast<int32>(INDEX_NONE);
	};
	auto Frame = [&](const FHumanoidLayout& Layout, const TArray<FMatrix>& World, FMatrix& Out)
	{
		const int32 Head = FindKey(Layout, "head");
		const int32 Pairs[2][2] = {
			{ FindKey(Layout, "leftupleg"), FindKey(Layout, "rightupleg") },
			{ FindKey(Layout, "leftarm"), FindKey(Layout, "rightarm") } };
		if (Head == INDEX_NONE) return false;
		const FVector Up = Origin(World[Head]) - Origin(World[Layout.Hips]);
		for (const auto& Pair : Pairs)
			if (Pair[0] != INDEX_NONE && Pair[1] != INDEX_NONE
				&& CharacterFrame(Origin(World[Pair[1]]) - Origin(World[Pair[0]]), Up, Out))
				return true;
		return false;
	};
	FMatrix SourceFrame, TargetFrame;
	const bool bAligned = Frame(SourceLayout, SourceRefWorld, SourceFrame) && Frame(TargetLayout, TargetRefWorld, TargetFrame);
	// row-vector: SourceFrame × Align = TargetFrame
	Align = bAligned ? SourceFrame.GetTransposed() * TargetFrame : FMatrix::Identity;

	// 5) 기준 자세 정렬(UE Retarget Pose / Auto Align). T-포즈와 A-포즈처럼 기준 자세가 달라도
	//    타깃 본 방향(본→자식)을 소스 기준 자세의 같은 본 방향으로 먼저 돌려 둔다.
	auto IsSourceDescendant = [&](int32 Bone, int32 Ancestor)
	{
		for (Bone = InSource.RefBoneInfo[Bone].ParentIndex; Bone != INDEX_NONE; Bone = InSource.RefBoneInfo[Bone].ParentIndex)
			if (Bone == Ancestor) return true;
		return false;
	};
	auto AimChild = [&](int32 T)
	{
		const int32 S = TargetToSource[T];
		int32 Found = INDEX_NONE, Count = 0;
		for (int32 C = T + 1; C < InTarget.GetNum(); ++C)
		{
			if (InTarget.RefBoneInfo[C].ParentIndex != T) continue;
			const int32 SC = TargetToSource[C];
			if (SC == INDEX_NONE || SC == S || !IsSourceDescendant(SC, S)) continue;
			// 가슴처럼 자식이 여럿(목·양쪽 어깨)이면 몸통 체인(목·척추·머리)을 따른다.
			const FString& Key = TargetLayout.Keys[C];
			if (Key.starts_with("neck") || Key.starts_with("spine") || Key == "head") return C;
			Found = C;
			++Count;
		}
		return Count == 1 ? Found : static_cast<int32>(INDEX_NONE);
	};

	RotationOffset.SetNum(InTarget.GetNum());
	TargetRefLocalRot.SetNum(InTarget.GetNum());
	DrivenTargetBones.Empty();
	for (int32 T = 0; T < InTarget.GetNum(); ++T)
	{
		TargetRefLocalRot[T] = RotationMatrix(InTarget.RefBonePose[T]);
		const int32 S = TargetToSource[T];
		if (S == INDEX_NONE) continue;

		FMatrix Correction = FMatrix::Identity;
		if (const int32 C = T == TargetHips ? INDEX_NONE : AimChild(T); C != INDEX_NONE)
		{
			const int32 SC = TargetToSource[C];
			const FVector TargetDir = Origin(TargetRefWorld[C]) - Origin(TargetRefWorld[T]);
			const FVector SourceDir = RotateRow(Origin(SourceRefWorld[SC]) - Origin(SourceRefWorld[S]), Align);
			if (TargetDir.Length() > 1.e-4f && SourceDir.Length() > 1.e-4f)
				Correction = RotationBetween(TargetDir.Normalized(), SourceDir.Normalized());
		}

		// row-vector, 소스 월드 변화량 D = SrcRefWorld⁻¹ × SrcWorld를 타깃 축으로 옮기면 Align⁻¹ × D × Align.
		// TgtWorld = (TgtRefWorld × Correction) × Align⁻¹ × SrcRefWorld⁻¹ × SrcWorld × Align (회전 행렬의 역은 전치)
		RotationOffset[T] = TargetRefWorldRot[T] * Correction * Align.GetTransposed() * SourceRefWorldRot[S].GetTransposed();
		DrivenTargetBones.Add(T);
	}

	SourceRefHipsPosition = Origin(SourceRefWorld[SourceHips]);
	TargetRefHipsPosition = Origin(TargetRefWorld[TargetHips]);
	// 이동량은 다리 길이(Hips→발) 비율로 맞춘다. 바인드 포즈의 Hips가 원점에 있는 파일도 있어 높이 비율은 쓰지 않는다.
	auto LegLength = [&](const FHumanoidLayout& Layout, const TArray<FMatrix>& World)
	{
		const int32 Foot = FindKey(Layout, "leftfoot");
		return Foot == INDEX_NONE ? 0.0f : (Origin(World[Foot]) - Origin(World[Layout.Hips])).Length();
	};
	const float SourceLeg = LegLength(SourceLayout, SourceRefWorld);
	const float TargetLeg = LegLength(TargetLayout, TargetRefWorld);
	HeightScale = SourceLeg > 1.e-4f && TargetLeg > 1.e-4f ? TargetLeg / SourceLeg : 1.0f;
	return true;
}

void FHumanoidRetargeter::SetSourceHipsAnchor(const TArray<FTransform>& SourceLocal)
{
	TArray<FMatrix> SourceWorld;
	WorldMatrices(*Source, SourceLocal, SourceWorld);
	SourceRefHipsPosition = Origin(SourceWorld[SourceHips]);
}

void FHumanoidRetargeter::Retarget(const TArray<FTransform>& SourceLocal, TArray<FTransform>& OutTargetLocal) const
{
	TArray<FMatrix> SourceWorldRot, SourceWorld;
	WorldRotations(*Source, SourceLocal, SourceWorldRot);
	WorldMatrices(*Source, SourceLocal, SourceWorld);

	const int32 Num = Target->GetNum();
	OutTargetLocal.SetNum(Num);
	TArray<FMatrix> TargetWorldRot, TargetWorld;
	TargetWorldRot.SetNum(Num);
	TargetWorld.SetNum(Num);
	for (int32 T = 0; T < Num; ++T)
	{
		const int32 Parent = Target->RefBoneInfo[T].ParentIndex;
		const FMatrix ParentRot = Parent >= 0 ? TargetWorldRot[Parent] : FMatrix::Identity;
		const FMatrix ParentWorld = Parent >= 0 ? TargetWorld[Parent] : FMatrix::Identity;

		FTransform Local = Target->RefBonePose[T];
		const int32 S = TargetToSource[T];
		FMatrix WorldRot;
		if (S != INDEX_NONE)
		{
			WorldRot = RotationOffset[T] * SourceWorldRot[S] * Align;
			Local.Rotation = QuatFromRotationMatrix(WorldRot * ParentRot.GetTransposed()).ToFRotator();
		}
		else
		{
			WorldRot = TargetRefLocalRot[T] * ParentRot;
		}

		if (T == TargetHips)
		{
			const FVector Delta = RotateRow(Origin(SourceWorld[SourceHips]) - SourceRefHipsPosition, Align);
			const FVector World = TargetRefHipsPosition + Delta * HeightScale;
			Local.Location = Parent >= 0 ? ParentWorld.Inverse().TransformPosition(World) : World;
		}

		OutTargetLocal[T] = Local;
		TargetWorldRot[T] = WorldRot;
		TargetWorld[T] = Local.GetLocalMatrix() * ParentWorld;
	}
}
