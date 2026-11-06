#include "EnginePCH.h"

#include "Components/DecalComponent.h"
#include "FakeSpotLightActor.h"
#include "Asset/AssetManager.h"


AFakeSpotLightActor::AFakeSpotLightActor() {
	// 데칼 스포트라이트 
	UDecalComponent* Decal = GetDecalComponent();
	Decal->SetPerspective(true);
	Decal->SetAdditive(true);
	Decal->SetExtent(FVector(5.0f, 2.0f, 2.0f));
	Decal->SetFadeTimes(0.0f, 0.0f, 10e6f, 0.0f);
	Decal->SetDecalTexture(UAssetManager::Get().LoadTexture("Assets/textures/SpotLight_Warm.png"));

	// 빛번짐 Billboard 
	GlowComponent = CreateDefaultSubobject<UBillboardComponent>("GlowComponent");
	GlowComponent->SetupAttachment(Decal);
	GlowComponent->SetEditorIcon(false);
	GlowComponent->SetHiddenInGame(false);
	GlowComponent->SetIconSize(0.6f);

	// 빛번짐에 텍스처 입히기
	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Glow = UAssetManager::Get().LoadTexture("Assets/textures/SpotLight_Warm.png");
	if (Base && Glow) {
		UMaterial* GlowMat = UMaterial::CreateInstance(Base);
		GlowMat->Textures[0] = Glow;
		GlowComponent->SetMaterial(0, GlowMat);
	}
}