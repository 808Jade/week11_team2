#pragma once

#include "Animation/ReferenceSkeleton.h"

// UE4 Retarget Manager 방식의 인간형 리타기팅.
// 소스/타깃 Skeleton의 본을 정규화한 이름(+척추·목 체인 비율 매핑)으로 대응시키고,
// 기준 자세 대비 월드 회전 변화량을 캐릭터 축(좌우·위)을 맞춘 뒤 옮긴다. 루트(Hips) 이동은 다리 길이 비율로 스케일한다.
// 결과는 타깃 Skeleton의 로컬 포즈이므로 런타임은 일반 AnimSequence로 재생한다.
class FHumanoidRetargeter
{
public:
	// 이름 규칙(Mixamo/Biped/UE 마네킹)을 통일한 비교용 키. 예: "ValveBiped_Bip01_Pelvis", "mixamorig_Hips_01" → "hips"
	static FString HumanoidBoneKey(const FString& Name);

	// Source/Target은 부모가 먼저 오는 기준 Skeleton이다. 실패하면 Error에 이유를 남긴다.
	// bExtractRootMotion이면 Hips의 바닥 평면 이동을 타깃 Skeleton의 root 본(0번) 트랙으로 옮긴다(UE의 "골반 이동을 root로 굽기").
	// 런타임 Root Motion은 0번 본의 이동만 Actor로 넘기는데, Mixamo처럼 골반이 최상위인 소스는 이동이 전부 골반에 있기 때문이다.
	// 타깃의 0번 본이 Hips의 조상이고 소스 본에 매핑되지 않았을 때만 적용된다(HasRootMotion으로 확인).
	bool Initialize(const FReferenceSkeleton& Source, const FReferenceSkeleton& Target, FString& Error,
		bool bExtractRootMotion = false);
	bool HasRootMotion() const { return RootMotionBone != INDEX_NONE; }

	// SourceLocal(소스 본 수만큼의 로컬 포즈) → 타깃 로컬 포즈(타깃 본 수만큼).
	void Retarget(const TArray<FTransform>& SourceLocal, TArray<FTransform>& OutTargetLocal) const;

	// 소스 본에서 회전을 받아오는 타깃 본(그리고 Hips)의 목록. 이 본들만 트랙으로 내보내면 된다.
	const TArray<int32>& GetDrivenTargetBones() const { return DrivenTargetBones; }
	int32 GetTargetHips() const { return TargetHips; }
	int32 GetNumMappedBones() const { return DrivenTargetBones.Num(); }

	// 첫 샘플 프레임의 소스 포즈를 Hips 이동의 기준으로 삼는다(바인드 포즈 원점과 애니메이션 공간이 다를 수 있다).
	void SetSourceHipsAnchor(const TArray<FTransform>& SourceLocal);

private:
	const FReferenceSkeleton* Source = nullptr;
	const FReferenceSkeleton* Target = nullptr;

	TArray<int32> TargetToSource;        // 타깃 본 → 회전을 받아올 소스 본 (없으면 INDEX_NONE)
	TArray<FMatrix> RotationOffset;      // TgtRefWorldRot × Align⁻¹ × SrcRefWorldRot⁻¹ (row-vector)
	FMatrix Align;                       // 소스 캐릭터 축(좌우·위) → 타깃 캐릭터 축. 바라보는 방향 차이를 없앤다.
	TArray<FMatrix> TargetRefLocalRot;   // 타깃 기준 로컬 회전
	TArray<int32> DrivenTargetBones;

	int32 SourceHips = INDEX_NONE;
	int32 TargetHips = INDEX_NONE;
	FVector SourceRefHipsPosition;
	FVector TargetRefHipsPosition;
	float HeightScale = 1.0f;

	// Root Motion을 구울 타깃 root 본(0번). 굽지 않으면 INDEX_NONE.
	int32 RootMotionBone = INDEX_NONE;
	// 타깃 캐릭터가 서 있는 축(0=X, 1=Y, 2=Z). 이 축의 이동(점프 높이)은 골반에 남기고 나머지 두 축만 root로 옮긴다.
	int32 TargetUpAxis = 2;
};
