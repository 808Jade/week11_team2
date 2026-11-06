#pragma once

#include "GameFramework/Actor/DecalActor.h"
#include "Components/BillboardComponent.h"
#include "Components/DecalComponent.h"
#include "FakeSpotLightActor.generated.h"


UCLASS()
class AFakeSpotLightActor : public ADecalActor
{
    GENERATED_BODY()

public:
    AFakeSpotLightActor();

private:
    UBillboardComponent* GlowComponent = nullptr;
};