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
		// 철권 규칙은 끝 번호가 본을 구분하므로("Arm left shoulder 1/2") 아래 별칭 비교용으로 남겨 둔다.
		FString TrailingNumber;
		if (Tokens.Num() > 1 && IsNumber(Tokens.Last()))
		{
			TrailingNumber = Tokens.Last();
			Tokens.Pop();
		}

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

		// 철권 모델(Devil Jin 등) 규칙: "Root hips", "Spine lower", "Arm left shoulder 2", "Leg left knee", "Arm left finger index a"
		//   어깨 1 = 쇄골, 어깨 2 = 위팔, 손가락 a/b/c = 1/2/3 마디.
		//   "Pelvis"는 다리의 부모일 뿐이고 실제 Hips는 "Root hips"다. 둘 다 "hips"가 되지만 겹치는 이름은 이름 대응에서 빠지고,
		//   BuildLayout은 먼저 나오는 "Root hips"를 Hips로 고른다. Pelvis는 기준 자세 그대로 Root hips를 따라간다.
		static const std::unordered_map<FString, FString> TekkenAliases{
			{ "roothips", "hips" },
			{ "spinelower", "spine" }, { "spineupper", "spine2" },
			{ "headnecklower", "neck" }, { "headneckupper", "head" },
			{ "armshoulder1", "shoulder" }, { "armshoulder2", "arm" },
			{ "armelbow", "forearm" }, { "armwrist", "hand" },
			{ "legthigh", "upleg" }, { "legknee", "leg" }, { "legankle", "foot" }, { "legtoe", "toebase" },
			// 머리 방향을 맞출 기준점(Mixamo "LeftEye", "HeadTop_End")
			{ "headeyeball", "eye" }, { "unusedathheadtop", "headtopend" },
		};
		if (const auto It = TekkenAliases.find(Part + TrailingNumber); It != TekkenAliases.end())
			return Side + It->second;
		// "armfingerindexa" → "handindex1" (Mixamo "LeftHandIndex1")
		if (Part.starts_with("armfinger") && Part.size() > 10 && TrailingNumber.empty())
		{
			const char Joint = Part.back();
			const FString Finger = Part.substr(9, Part.size() - 10);
			if (Joint >= 'a' && Joint <= 'c' && (Finger == "thumb" || Finger == "index" || Finger == "middle" || Finger == "ring" || Finger == "pinky"))
				return Side + "hand" + Finger + static_cast<char>('1' + (Joint - 'a'));
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

	// 손가락 마디 키인지 본다. 예: "lefthandindex3", "righthandthumb2"
	bool IsFingerKey(const FString& Key)
	{
		const size_t Hand = Key.find("hand");
		return Hand != FString::npos && Key.size() > Hand + 5 && Key.back() >= '1' && Key.back() <= '9'
			&& (Key.find("thumb") != FString::npos || Key.find("index") != FString::npos || Key.find("middle") != FString::npos
				|| Key.find("ring") != FString::npos || Key.find("pinky") != FString::npos);
	}

	// 같은 본에서 본 기준점(매핑된 하위 본) 방향. Target은 타깃 기준 자세, Source는 소스 기준 자세를 타깃 캐릭터 축으로 옮긴 값이다.
	struct FLandmark
	{
		FVector Target;
		FVector Source;
	};

	// 기준점들로 타깃 본 방향을 소스 본 방향으로 돌리는 회전(row-vector: TargetDir × Out = SourceDir)을 구한다.
	// 기준점이 둘 이상이면 비틀림까지 맞춘다. 손목(손가락 첫 마디들)이나 머리(눈·머리끝)처럼 자식이 여럿인 본에 쓴다.
	bool LandmarkRotation(const TArray<FLandmark>& Landmarks, FMatrix& Out)
	{
		FVector TargetSum, SourceSum;
		for (const FLandmark& L : Landmarks)
		{
			TargetSum = TargetSum + L.Target;
			SourceSum = SourceSum + L.Source;
		}
		if (TargetSum.Length() < 1.e-4f || SourceSum.Length() < 1.e-4f) return false;
		const FVector TargetMain = TargetSum.Normalized();
		const FVector SourceMain = SourceSum.Normalized();

		// 두 번째 축: 주 방향에 가장 수직으로 벌어진 기준점 쌍(손이면 검지↔새끼, 머리면 왼눈↔오른눈). 같은 쌍을 양쪽에 쓴다.
		float Best = 1.e-3f;
		FVector TargetSide, SourceSide;
		for (int32 I = 0; I < Landmarks.Num(); ++I)
			for (int32 J = I + 1; J < Landmarks.Num(); ++J)
			{
				const FVector Delta = Landmarks[I].Target - Landmarks[J].Target;
				const float Perpendicular = (Delta - TargetMain * Delta.Dot(TargetMain)).Length();
				if (Perpendicular <= Best) continue;
				Best = Perpendicular;
				TargetSide = Delta;
				SourceSide = Landmarks[I].Source - Landmarks[J].Source;
			}

		FMatrix TargetFrame, SourceFrame;
		if (Best > 1.e-3f && CharacterFrame(TargetMain, TargetSide, TargetFrame) && CharacterFrame(SourceMain, SourceSide, SourceFrame))
		{
			Out = TargetFrame.GetTransposed() * SourceFrame;
			return true;
		}
		Out = RotationBetween(TargetMain, SourceMain); // 기준점이 하나이거나 한 줄이면 방향만 맞춘다.
		return true;
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

bool FHumanoidRetargeter::Initialize(const FReferenceSkeleton& InSource, const FReferenceSkeleton& InTarget, FString& Error,
	bool bExtractRootMotion)
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

	const int32 NumTarget = InTarget.GetNum();
	auto TargetPos = [&](int32 Bone) { return Origin(TargetRefWorld[Bone]); };
	auto SourcePos = [&](int32 Bone) { return Origin(SourceRefWorld[Bone]); };

	// 가장 가까운 매핑된 조상. 손목 → Hand base(매핑 안 됨) → 손가락처럼 중간에 매핑 안 된 본이 있어도 이어 준다.
	// 그 조상에서 본 매핑된 하위 본 방향을 기준점으로 모은다.
	TArray<int32> MappedAncestor;
	MappedAncestor.SetNum(NumTarget);
	TArray<TArray<FLandmark>> Landmarks;
	Landmarks.SetNum(NumTarget);
	for (int32 T = 0; T < NumTarget; ++T)
	{
		const int32 Parent = InTarget.RefBoneInfo[T].ParentIndex;
		MappedAncestor[T] = Parent == INDEX_NONE ? INDEX_NONE
			: TargetToSource[Parent] != INDEX_NONE ? Parent : MappedAncestor[Parent];
		const int32 A = MappedAncestor[T];
		if (A == INDEX_NONE || TargetToSource[T] == INDEX_NONE) continue;
		const int32 SA = TargetToSource[A], ST = TargetToSource[T];
		if (ST == SA || !IsSourceDescendant(ST, SA)) continue;
		const FVector TargetDir = TargetPos(T) - TargetPos(A);
		const FVector SourceDir = RotateRow(SourcePos(ST) - SourcePos(SA), Align);
		if (TargetDir.Length() > 1.e-4f && SourceDir.Length() > 1.e-4f)
			Landmarks[A].Add({ TargetDir, SourceDir });
	}
	TArray<int32> SourceChildCount, SourceChild;
	SourceChildCount.SetNum(InSource.GetNum());
	SourceChild.SetNum(InSource.GetNum());
	std::fill(SourceChildCount.begin(), SourceChildCount.end(), 0);
	for (int32 I = 0; I < InSource.GetNum(); ++I)
		if (const int32 Parent = InSource.RefBoneInfo[I].ParentIndex; Parent != INDEX_NONE)
		{
			++SourceChildCount[Parent];
			SourceChild[Parent] = I;
		}

	RotationOffset.SetNum(NumTarget);
	TargetRefLocalRot.SetNum(NumTarget);
	DrivenTargetBones.Empty();
	TArray<FMatrix> Corrections;
	Corrections.SetNum(NumTarget);
	for (int32 T = 0; T < NumTarget; ++T)
	{
		TargetRefLocalRot[T] = RotationMatrix(InTarget.RefBonePose[T]);
		Corrections[T] = FMatrix::Identity;
		const int32 S = TargetToSource[T];
		if (S == INDEX_NONE) continue;

		// 소스 기준 자세가 T-포즈가 아닐 수 있다. 스킨 없는 애니메이션 FBX는 BindPose가 없어 노드 기본값(없으면 0초 포즈)이 기준이 된다.
		// 그래서 타깃 기준 자세를 본마다 소스 기준 자세 쪽으로 돌려 둔다.
		//   a) 자식 하나를 조준한다(팔·다리·척추).
		//   b) 자식이 여럿이거나 매핑 안 된 중간 본을 건너야 하면 매핑된 하위 본들로 비틀림까지 맞춘다(손목→손가락 첫 마디, 머리→눈·머리끝).
		//   c) 기준점이 없으면 부모 보정을 물려받는다(눈, 손가락 끝마디). 손가락 끝마디는 소스 끝마디의 굽힘을 더한다.
		//   보정이 없으면 그 본만 T-포즈 방향에 남아 주먹이 펴지거나 머리가 엉뚱한 곳을 본다.
		FMatrix Correction = FMatrix::Identity;
		bool bCorrected = T == TargetHips;
		if (const int32 C = bCorrected ? INDEX_NONE : AimChild(T); C != INDEX_NONE)
		{
			const int32 SC = TargetToSource[C];
			const FVector TargetDir = TargetPos(C) - TargetPos(T);
			const FVector SourceDir = RotateRow(SourcePos(SC) - SourcePos(S), Align);
			if (TargetDir.Length() > 1.e-4f && SourceDir.Length() > 1.e-4f)
				Correction = RotationBetween(TargetDir.Normalized(), SourceDir.Normalized());
			bCorrected = true;
		}
		if (!bCorrected && !Landmarks[T].IsEmpty())
			bCorrected = LandmarkRotation(Landmarks[T], Correction);
		if (!bCorrected && MappedAncestor[T] != INDEX_NONE)
		{
			Correction = Corrections[MappedAncestor[T]];
			const int32 Parent = InTarget.RefBoneInfo[T].ParentIndex;
			if (IsFingerKey(TargetLayout.Keys[T]) && SourceChildCount[S] == 1)
			{
				// 타깃 끝마디는 부모 마디의 연장선(T-포즈의 곧은 손가락)이라고 보고, 소스 끝마디→끝점 방향으로 더 굽힌다.
				const FVector TargetDir = RotateRow(TargetPos(T) - TargetPos(Parent), Correction);
				const FVector SourceDir = RotateRow(SourcePos(SourceChild[S]) - SourcePos(S), Align);
				if (TargetDir.Length() > 1.e-4f && SourceDir.Length() > 1.e-4f)
					Correction = Correction * RotationBetween(TargetDir.Normalized(), SourceDir.Normalized());
			}
		}
		Corrections[T] = Correction;

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

	// 6) Root Motion: 런타임은 Skeleton 0번 본의 이동만 Actor로 넘긴다. 0번 본이 Hips 위에 따로 있고 소스 본을 받지 않을 때만
	//    Hips의 바닥 평면 이동을 그 본으로 옮길 수 있다. (0번 본이 Hips 자체면 옮길 곳이 없다)
	RootMotionBone = INDEX_NONE;
	if (bExtractRootMotion && TargetHips != 0 && TargetToSource[0] == INDEX_NONE)
	{
		bool bRootAboveHips = false;
		for (int32 Bone = InTarget.RefBoneInfo[TargetHips].ParentIndex; Bone != INDEX_NONE; Bone = InTarget.RefBoneInfo[Bone].ParentIndex)
			bRootAboveHips = bRootAboveHips || Bone == 0;
		if (bRootAboveHips)
		{
			RootMotionBone = 0;
			// 서 있는 축은 기준 자세의 Hips → Head 방향에서 가장 큰 성분으로 정한다. Head가 없으면 엔진의 위(Z)다.
			TargetUpAxis = 2;
			if (const int32 Head = FindKey(TargetLayout, "head"); Head != INDEX_NONE)
			{
				const FVector Up = Origin(TargetRefWorld[Head]) - TargetRefHipsPosition;
				for (int32 Axis = 0; Axis < 3; ++Axis)
					if (std::abs(Up.V[Axis]) > std::abs(Up.V[TargetUpAxis])) TargetUpAxis = Axis;
			}
			DrivenTargetBones.Add(RootMotionBone);
		}
	}
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

	// 기준 시점 대비 Hips 이동량(타깃 축·타깃 크기 기준)
	const FVector HipsDelta = RotateRow(Origin(SourceWorld[SourceHips]) - SourceRefHipsPosition, Align) * HeightScale;

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

		if (T == RootMotionBone)
		{
			// root 본(부모 없음)의 위치는 컴포넌트 공간이다. Hips의 바닥 평면 이동을 여기로 옮긴다.
			// 아래에서 Hips 로컬 위치를 부모 월드의 역으로 구하므로, 옮긴 만큼 Hips에서는 저절로 빠진다(월드 위치는 그대로).
			FVector Ground = HipsDelta;
			Ground.V[TargetUpAxis] = 0.0f;
			Local.Location = Local.Location + Ground;
		}

		if (T == TargetHips)
		{
			const FVector World = TargetRefHipsPosition + HipsDelta;
			Local.Location = Parent >= 0 ? ParentWorld.Inverse().TransformPosition(World) : World;
		}

		OutTargetLocal[T] = Local;
		TargetWorldRot[T] = WorldRot;
		TargetWorld[T] = Local.GetLocalMatrix() * ParentWorld;
	}
}
