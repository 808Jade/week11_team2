#pragma once

#include <fbxsdk.h>
#include "Core/EngineString.h"
#include "Render/SkeletalMeshData.h"
#include "Asset/FbxImporter/FbxMeshBake.h"

class UAnimSequence;

struct FFbxImportResult
{
	TArray<FString> Paths;
	TArray<FString> Warnings;
	bool bSkipped = false;
};

// Import 중에만 사용하는 Node별 자료. SDK 포인터는 최종 엔진 출력에 넘기지 않는다.
struct FFbxMeshNodeData {
	FbxNode* Node = nullptr;
	bool bHasSkin = false;
	FStaticMeshData Geometry;
	TArray<int32> ControlPointIndices; // Geometry.Vertices와 같은 순서/길이
};

// Manager와 Scene의 수명을 관리하며 한 번에 FBX 파일 하나를 보관한다.
class FFbxImporter {
public:
	FFbxImporter() = default;
	~FFbxImporter();
	FFbxImporter(const FFbxImporter&) = delete;
	FFbxImporter& operator=(const FFbxImporter&) = delete;

public:
	// 이전 Scene을 닫고 새 파일을 읽는다. 실패하면 닫힌 상태와 오류를 반환한다.
	// 추출 폴더를 지정하면 Embedded Texture를 추출한다. 폴더는 호출자가 소유하며 Texture 복사가 끝난 뒤 정리한다.
	bool Open(const FString& Path, FString& OutError, const FString& ExtractionFolder = {});
	void Close();
	// 비소유 참조. 다음 Open/Close 또는 이 객체의 소멸 전까지만 유효하다.
	FbxScene* GetScene() const;

	bool ReadMeshData(TArray<FString>& OutWarnings, FString& OutError);
	// 참조는 다음 ReadMeshData/Open/Close/소멸 전까지만 유효하다.
	const TArray<FFbxMeshNodeData>& GetMeshNodes() const;
	void BuildStaticMesh(FStaticMeshData& OutData, bool ExcludeSkeletalMeshes = false) const;

	bool ReadSkeletonData(TArray<FString>& OutWarnings, FString& OutError);
	const TArray<FBoneInfo>& GetBones() const;
	// ReadSkeletonData 성공 후 호출한다. SDK 수명과 독립적인 Geometry/Weight/Bone 출력을 만든다.
	bool BuildSkeletalMesh(FSkeletalMeshData& OutData, TArray<FString>& OutWarnings, FString& OutError) const;

	TArray<FString> GetAnimationNames() const;
	bool BuildAnimSequence(int32 AnimationIndex, float RequestedSampleRate, UAnimSequence& OutSequence, TArray<FString>& OutWarnings, FString& OutError) const;

	// Assets/FBX에 저장한 뒤 미로드 Asset을 등록한다. 이미 로드된 객체는 유지한다.
	static bool ImportToAssets(const FString& Path, EFbxImportMode Mode, FFbxImportResult& OutResult, FString& OutError);

private:
	void PrepareScene();
	void ClearSkeletonData();

private:
	FbxManager* mManager{};
	FbxScene* mScene{};
	FString mSourcePath{};
	TArray<FFbxMeshNodeData> mMeshNodes{};
	bool mIsBlender{};
	TArray<FBoneInfo> mBones{};
	TMap<FbxNode*, int32> mBoneIndices{};
	TArray<FbxAMatrix> mBindGlobals{};
};
