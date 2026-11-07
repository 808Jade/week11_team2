#include "EnginePCH.h"
#include "SkeletalMeshActor.h"
#include "Asset/AssetManager.h"
#include "Render/SkeletalMesh.h"

#include <utility>

namespace
{
    // A의 임포터가 붙기 전까지 쓰는 테스트 막대. 에셋은 하나만 만들어 모든 액터가 공유한다
	USkeletalMesh* GetTestBoneStickMesh() {
		static USkeletalMesh* Cached{};
		if (Cached) {
			return Cached;
		}

		FSkeletalMeshData Data{ MakeTestBoneStick(3, 1.0f) };
		FString Error{};
		USkeleton* Skeleton{ UAssetManager::FindOrCreateSkeleton(Data.Bones, Error) };
		if (!Skeleton) {
			return nullptr;
		}

		TUniquePtr<USkeletalMesh> Mesh{ NewObject<USkeletalMesh>() };
		if (Mesh->Build(std::move(Data), *Skeleton, Error)) {
			Cached = Mesh.release();
		}

		return Cached;
	}
}

ASkeletalMeshActor::ASkeletalMeshActor()
{
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("USkeletalMeshComponent");
    SkeletalMeshComponent->SetupAttachment(GetRootComponent());
    SkeletalMeshComponent->SetSkeletalMesh(GetTestBoneStickMesh());
}
