#pragma once

#include <fbxsdk.h>
#include "Core/EngineString.h"
#include "Render/SkeletalMeshData.h"
#include "Asset/FbxImporter/FbxMeshBake.h"

struct FFbxImportResult
{
	TArray<FString> Paths;
	TArray<FString> Warnings;
	bool bSkipped = false;
};

// Import 중에만 사용하는 Node별 자료. SDK 포인터는 최종 엔진 출력에 넘기지 않는다.
struct FFbxMeshNodeData
{
	FbxNode* Node = nullptr;
	bool bHasSkin = false;
	int32 RigidBoneIndex = -1; // Skin 없는 Mesh가 선택된 Bone 계층에 속하면 자기 Node의 Bone 번호
	FStaticMeshData Geometry;
	TArray<int32> ControlPointIndices; // Geometry.Vertices와 같은 순서/길이
};

// Manager와 Scene의 수명을 관리하며 한 번에 FBX 파일 하나를 보관한다.
class FFbxImporter
{
public:
	FFbxImporter() = default;
	~FFbxImporter();
	FFbxImporter(const FFbxImporter&) = delete;
	FFbxImporter& operator=(const FFbxImporter&) = delete;

	// 이전 Scene을 닫고 새 파일을 읽는다. 실패하면 닫힌 상태와 오류를 반환한다.
	// 추출 폴더를 지정하면 Embedded Texture를 추출한다. 폴더는 호출자가 소유하며 Texture 복사가 끝난 뒤 정리한다.
	bool Open(const FString& Path, FString& OutError, const FString& ExtractionFolder = {});
	void Close();
	// 비소유 참조. 다음 Open/Close 또는 이 객체의 소멸 전까지만 유효하다.
	FbxScene* GetScene() const { return Scene; }
	// 열린 Scene의 축/단위를 맞추고 Node별 자료를 생성한다. 실패하면 Node 결과는 비운다.
	bool ReadMeshData(TArray<FString>& OutWarnings, FString& OutError);
	// 참조는 다음 ReadMeshData/Open/Close/소멸 전까지만 유효하다.
	const TArray<FFbxMeshNodeData>& GetMeshNodes() const { return MeshNodes; }
	// Import Static용: Skin 유무와 관계없이 모든 Node를 하나로 합친다.
	void BuildStaticMesh(FStaticMeshData& OutData) const;
	// ReadMeshData 성공 후 호출한다. Bone 계층/Bind와 rigid의 Bind 기준 Geometry를 만들며 Weight는 만들지 않는다.
	bool ReadSkeletonData(TArray<FString>& OutWarnings, FString& OutError);
	// 다음 Skeleton/Mesh 재추출, Open/Close/소멸 전까지만 유효하다.
	const TArray<FBoneInfo>& GetBones() const { return Bones; }
	// ReadSkeletonData 성공 후 호출한다. SDK 수명과 독립적인 Geometry/Weight/Bone 출력을 만든다.
	bool BuildSkeletalMesh(FSkeletalMeshData& OutData, TArray<FString>& OutWarnings, FString& OutError) const;
	// Assets/FBX에 저장한 뒤 미로드 Asset을 등록한다. 이미 로드된 객체는 유지한다.
	static bool ImportToAssets(const FString& Path, EFbxImportMode Mode, FFbxImportResult& OutResult, FString& OutError);

private:
	void ClearSkeletonData();
	FbxManager* Manager = nullptr;
	FbxScene* Scene = nullptr;
	FString SourcePath;			// 원본 FBX 파일 경로 문자열
	TArray<FFbxMeshNodeData> MeshNodes;
	bool bIsBlender = false;	// Armature 예외 처리용
	TArray<FBoneInfo> Bones;
	TMap<FbxNode*, int32> BoneIndices; // Import 중 SDK Node를 출력 Bone 번호로 연결
	TMap<FbxNode*, FFbxMeshNodeData> RigidBindMeshes; // Skeleton과 함께 비우며, 현재 Global 기준 MeshNodes는 유지한다.
};
