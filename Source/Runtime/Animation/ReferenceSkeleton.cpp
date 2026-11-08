#include "EnginePCH.h"
#include "ReferenceSkeleton.h"
#include <cmath>
#include <unordered_map>
#include <unordered_set>

int32 FReferenceSkeleton::FindBoneIndex(const FString& Name) const
{
	for (int32 Index = 0; Index < RefBoneInfo.Num(); ++Index)
	{
		if (RefBoneInfo[Index].Name == Name) return Index;
	}
	return -1;
}

bool FReferenceSkeleton::Validate(FString& Error) const
{
	if (RefBoneInfo.IsEmpty() || RefBoneInfo.Num() != RefBonePose.Num() || RefBoneInfo.Num() > 65536)
	{
		Error = "Invalid reference skeleton size";
		return false;
	}
	std::unordered_set<FString> Names;
	for (int32 Index = 0; Index < RefBoneInfo.Num(); ++Index)
	{
		const FMeshBoneInfo& Bone = RefBoneInfo[Index];
		if (Bone.Name.empty() || !Names.insert(Bone.Name).second ||
			(Index == 0 ? Bone.ParentIndex != -1 : Bone.ParentIndex < 0 || Bone.ParentIndex >= Index))
		{
			Error = "Invalid or duplicate bone: " + Bone.Name;
			return false;
		}
		const FMatrix Local = RefBonePose[Index].GetLocalMatrix();
		for (int Row = 0; Row < 4; ++Row)
			for (int Col = 0; Col < 4; ++Col)
				if (!std::isfinite(Local.M[Row][Col])) { Error = "Non-finite bone transform"; return false; }
		if (std::abs(Local.Determinant()) < 1.e-12f) { Error = "Singular bone transform"; return false; }
	}
	return true;
}

bool FReferenceSkeleton::GetModelSpaceMatrices(TArray<FMatrix>& Out, FString& Error) const
{
	if (!Validate(Error)) return false;
	Out.SetNum(GetNum());
	for (int32 Index = 0; Index < GetNum(); ++Index)
	{
		Out[Index] = RefBonePose[Index].GetLocalMatrix();
		const int32 Parent = RefBoneInfo[Index].ParentIndex;
		if (Parent >= 0) Out[Index] = Out[Index] * Out[Parent];
	}
	return true;
}

bool FReferenceSkeleton::MergeBonesFrom(const FReferenceSkeleton& Source, FString& Error)
{
	if (!Validate(Error) || !Source.Validate(Error)) return false;
	if (FName(RefBoneInfo[0].Name) != FName(Source.RefBoneInfo[0].Name))
	{
		Error = "Root bone differs: expected " + RefBoneInfo[0].Name + ", found " + Source.RefBoneInfo[0].Name;
		return false;
	}

	FReferenceSkeleton Merged = *this;
	std::unordered_map<FName, int32, FNameHash> ByName;
	for (int32 Index = 0; Index < GetNum(); ++Index)
	{
		if (!ByName.emplace(FName(RefBoneInfo[Index].Name), Index).second)
		{
			Error = "Duplicate Skeleton bone name: " + RefBoneInfo[Index].Name;
			return false;
		}
	}

	std::unordered_set<FName, FNameHash> Seen;
	TArray<int32> SourceToSkeleton;
	for (int32 Index = 0; Index < Source.GetNum(); ++Index)
	{
		const auto& Bone = Source.RefBoneInfo[Index];
		const FName Name(Bone.Name);
		if (!Seen.insert(Name).second)
		{
			Error = "Duplicate source bone name: " + Bone.Name;
			return false;
		}
		const int32 Parent = Bone.ParentIndex == INDEX_NONE ? INDEX_NONE : SourceToSkeleton[Bone.ParentIndex];
		const auto [It, bAdded] = ByName.emplace(Name, Merged.GetNum());
		if (bAdded)
		{
			if (Merged.GetNum() == 65536) { Error = "More than 65536 bones after merge"; return false; }
			Merged.RefBoneInfo.Add({ Bone.Name, Parent });
			Merged.RefBonePose.Add(Source.RefBonePose[Index]);
		}
		else if (Merged.RefBoneInfo[It->second].ParentIndex != Parent)
		{
			Error = "Different parent chain for bone: " + Bone.Name;
			return false;
		}
		SourceToSkeleton.Add(It->second);
	}
	*this = std::move(Merged);
	return true;
}
