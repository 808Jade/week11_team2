#include "EnginePCH.h"
#include "Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/SceneComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Core/EngineLog.h"
#include "UObject/Object.h"

#include "Script/LuaScriptInstance.h"
#include "Script/LuaBindings.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
enum class EAttachmentRule;


AActor::AActor()
{
	PrimaryActorTick.Target = this;
}

AActor::~AActor()
{
	TArray<UActorComponent*> ToDelete = Components;
	const auto GetOuterDepth = [this](const UActorComponent* Component)
	{
		int32 Depth = 0;
		for (const UObject* Object = Component; Object && Object != this; Object = Object->GetOuter())
			++Depth;
		return Depth;
	};
	std::stable_sort(ToDelete.begin(), ToDelete.end(), [&](const UActorComponent* A, const UActorComponent* B)
	{
		return GetOuterDepth(A) > GetOuterDepth(B);
	});
	Components.Reset();
	RootComponent = nullptr;

	for (UActorComponent* Component : ToDelete)
	{
		delete Component;
	}
}

void AActor::BeginPlay()
{
	if (bHasBegunPlay || !World || !World->HasBegunPlay()) return;
	RegisterAllComponents();
	bHasBegunPlay = true;
	for (UActorComponent* Component : Components)
		if (Component) Component->BeginPlayComponent();

	if (!ScriptPath.empty())
	{
		Script = new FLuaScriptInstance();
		Script->Load(this, ScriptPath);
		PrimaryActorTick.bCanEverTick = true;
	}

	RegisterAllActorTickFunctions(true);

	if (Script)
		Script->Call("BeginPlay");
}

void AActor::TickActor(float DeltaTime)
{
	Tick(DeltaTime);
	if (Script)
		Script->Tick(DeltaTime);
}

void AActor::EndPlay()
{
	if (!Script)
		return;

	Script->Call("EndPlay");
	if (World) World->GetCoroutineScheduler().StopAll(this);
	FLuaBindings::ReleaseCallbacks(this);       // 델리게이트에 걸린 Lua 함수 해제 (단계 8 안전장치 참고)
	delete Script;
	Script = nullptr;
}

void AActor::RegisterAllActorTickFunctions(bool bRegister)
{
	if (bRegister && !World)
		return;

	// bCanEverTick이 꺼진 함수는 등록하지 않으므로 정적 메시 액터는 매 프레임 순회 대상에서 빠진다.
	auto Apply = [&](FTickFunction& Function)
		{
			if (bRegister)
				Function.RegisterTickFunction(World->GetTickTaskManager());
			else
				Function.UnRegisterTickFunction();
		};

	Apply(PrimaryActorTick);
	for (UActorComponent* Component : Components)
	{
		if (Component)
			Component->RegisterComponentTickFunctions(bRegister);
	}
}

void AActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Components.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Components", Num);
		if (Ar.IsLoading())
			Components.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Components[i];
			ActorArray.EnterElement() << Obj;
			Components[i] = static_cast<UActorComponent*>(Obj);
		}

		UObject* WorldObj = World;
		UObject* LevelObj = Level;
		UObject* RootObj = RootComponent;
		Record << SA_VALUE("OwningWorld", WorldObj);
		Record << SA_VALUE("OwningLevel", LevelObj);
		Record << SA_VALUE("RootComponent", RootObj);
		World = static_cast<UWorld*>(WorldObj);
		Level = static_cast<ULevel*>(LevelObj);
		RootComponent = static_cast<USceneComponent*>(RootObj);
		return;
	}

	if (!Ar.IsSaving())
		return;

	const auto GetSavedId = [this](const UActorComponent* Component) -> uint32
	{
		if (!Component || !Component->GetOwner() || Component->GetOwner()->GetLevel() != Level)
			return 0;
		for (const UObject* Object = Component; Object; Object = Object->GetOuter())
		{
			if (Object->HasAnyFlags(EObjectFlags::RF_Transient))
				return 0;
			if (Object == Component->GetOwner())
				return Component->GetUUID();
			if (!Object->IsA(UActorComponent::StaticClass()))
				return 0;
		}
		return 0;
	};

	uint32 RootId = GetSavedId(RootComponent);
	Record << SA_VALUE("RootComponent", RootId);

	TArray<UActorComponent*> SavedComponents;
	for (UActorComponent* Component : Components)
		if (GetSavedId(Component))
			SavedComponents.Add(Component);

	int32 Num = SavedComponents.Num();
	FStructuredArchive::FArray ComponentArray = Record.EnterArray("Components", Num);
	for (UActorComponent* Component : SavedComponents)
	{
		FStructuredArchive::FRecord ComponentRecord = ComponentArray.EnterElement().EnterRecord();
		FString Name = Component->GetName();
		FString ClassName = Component->GetClass()->Name;
		uint32 Id = GetSavedId(Component);
		uint32 OuterId = GetSavedId(Cast<UActorComponent>(Component->GetOuter()));
		uint32 ParentId = 0;
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			ParentId = GetSavedId(SceneComponent->GetAttachParent());
		ComponentRecord << SA_VALUE("Id", Id) << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName)
			<< SA_VALUE("Outer", OuterId) << SA_VALUE("Parent", ParentId);
		Component->Serialize(ComponentRecord);
	}
}

void AActor::AddOwnedComponent(UActorComponent* Component)
{
	if (Component && Components.Find(Component) == INDEX_NONE)
		Components.Add(Component);
}

void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
	for (uint32 i = 0; i < Components.Num(); ++i)
	{
		if (Components[i] == Component)
		{
			Components.RemoveAt(i, 1);
			break;
		}
	}

	if (RootComponent == Component)
	{
		RootComponent = nullptr;
	}
}

FVector AActor::GetActorLocation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldLocation();
	}
	return FVector::ZeroVector;
}

FRotator AActor::GetActorRotation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation();
	}
	return FRotator();
}

FVector AActor::GetActorScale3D() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldScale3D();
	}
	return FVector::OneVector;
}

FQuat AActor::GetActorQuat() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation().Quaternion();
	}
	return FQuat::Identity();
}

FTransform AActor::GetActorTransform() const
{
	if (RootComponent)
	{
		return FTransform(
			RootComponent->GetWorldRotation(),
			RootComponent->GetWorldLocation(),
			RootComponent->GetWorldScale3D()
		);

		// return FTransform(RootComponent->GetWorldMatrix());
	}
	return FTransform::Identity;
}

bool AActor::Destroy()
{
	if (!World)
		return false;

	return World->DestroyActor(this);
}

UActorComponent* AActor::AddComponent(UClass* ComponentClass, FName Name)
{
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return nullptr;
	}


	if (Name.IsNone())
		Name = MakeUniqueObjectName(this, ComponentClass);
	else if (StaticFindObjectFast(nullptr, this, Name))
		return nullptr;

	UActorComponent* NewComponent = NewObject<UActorComponent>(this, ComponentClass, Name);
	if (!NewComponent) { return nullptr; }

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(NewComponent))
	{
		if (RootComponent)
			SceneComponent->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
		else
			SetRootComponent(SceneComponent);
	}

	NewComponent->RegisterComponent();

	return NewComponent;
}

bool AActor::IsDefaultSceneRoot(const UActorComponent* Component) const
{
	return Component && Component == RootComponent && Component->GetFName() == FName("DefaultSceneRoot")
		&& Component->GetClass() == USceneComponent::StaticClass()
		&& !Component->HasAnyFlags(EObjectFlags::RF_DefaultSubObject);
}

bool AActor::CanDeleteComponentInEditor(const UActorComponent* Component) const
{
	return Component && Component->GetOwner() == this && !Component->HasAnyFlags(EObjectFlags::RF_DefaultSubObject);
}

bool AActor::CanMakeRootInEditor(const USceneComponent* Component) const
{
	if (!CanDeleteComponentInEditor(Component) || Component->IsHiddenInDetails() || Component == RootComponent
		|| (RootComponent && RootComponent->HasAnyFlags(EObjectFlags::RF_DefaultSubObject)))
		return false;

	if (IsDefaultSceneRoot(RootComponent))
	{
		for (const UObject* Outer = Component->GetOuter(); Outer; Outer = Outer->GetOuter())
			if (Outer == RootComponent)
				return false;
	}
	return true;
}

bool AActor::MakeRootInEditor(USceneComponent* Component)
{
	if (!CanMakeRootInEditor(Component))
		return false;

	USceneComponent* OldRoot = RootComponent;
	const bool bReplacePlaceholder = IsDefaultSceneRoot(OldRoot);
	USceneComponent* ExternalParent = OldRoot ? OldRoot->GetAttachParent() : nullptr;
	Component->SetupAttachment(ExternalParent, EAttachmentRule::KeepWorld);
	SetRootComponent(Component);

	if (bReplacePlaceholder)
	{
		const TArray<USceneComponent*> Children = OldRoot->GetAttachChildren();
		for (USceneComponent* Child : Children)
			Child->SetupAttachment(Component, EAttachmentRule::KeepWorld);
		DestroyComponent(OldRoot);
	}
	else if (OldRoot)
	{
		OldRoot->SetupAttachment(Component, EAttachmentRule::KeepWorld);
	}

	return true;
}

void AActor::DestroyComponent(UActorComponent* Component)
{
	if (!Component || Component->GetOwner() != this || Components.Find(Component) == INDEX_NONE)
		return;

	Component->UnregisterComponent();

	// Components nested by Outer cannot outlive that object.
	const TArray<UActorComponent*> OwnedComponents = Components;
	for (UActorComponent* Child : OwnedComponents)
	{
		if (Child && Components.Find(Child) != INDEX_NONE && Child->GetOuter() == Component)
			DestroyComponent(Child);
	}

	if (USceneComponent* Scene = Cast<USceneComponent>(Component))
	{
		USceneComponent* NewParent = Scene->GetAttachParent();
		const TArray<USceneComponent*> Children = Scene->GetAttachChildren();
		if (Scene == RootComponent)
		{
			for (USceneComponent* Child : Children)
			{
				if (Child && Child->GetOwner() == this)
				{
					NewParent = Child;
					Child->SetupAttachment(Scene->GetAttachParent(), EAttachmentRule::KeepWorld);
					break;
				}
			}
			SetRootComponent(NewParent && NewParent->GetOwner() == this ? NewParent : nullptr);
		}
		for (USceneComponent* Child : Children)
		{
			if (Child && Child != NewParent)
				Child->SetupAttachment(NewParent, EAttachmentRule::KeepWorld);
		}
	}

	RemoveOwnedComponent(Component);
	delete Component;
}

UActorComponent* AActor::FindComponentByName(FName Name) const
{
	for (UActorComponent* Component : Components)
		if (Component && Component->GetFName() == Name)
			return Component;
	return nullptr;
}

void AActor::RegisterAllComponents()
{
	if (!World) return;

	for (int32 Index = 0; Index < Components.Num(); ++Index)
	{
		UActorComponent* Component = Components[Index];
		if (Component && !Component->IsRegistered())
			Component->RegisterComponentWithWorld(World);
	}
}

void AActor::UnregisterAllComponents()
{
	const TArray<UActorComponent*> RegisteredCandidates = Components;
	for (UActorComponent* Component : RegisteredCandidates)
	{
		if (Component && Components.Find(Component) != INDEX_NONE && Component->IsRegistered())
			Component->UnregisterComponent();
	}
}

bool AActor::TryGetActorBounds(FBox& OutBounds) const
{
	bool bHasBounds = false;

	for (UActorComponent* Component : Components)
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
			continue;

		FBox PrimitiveBounds = Primitive->CalcBounds();
		if (bHasBounds)
		{
			OutBounds.Expand(PrimitiveBounds);
		}
		else
		{
			OutBounds = PrimitiveBounds;
			bHasBounds = true;
		}
	}

	return bHasBounds;
}

void AActor::NotifyActorBeginOverlap(AActor* OtherActor)
{
	// TODO(A): if (Script) Script->Call("OnBeginOverlap", OtherActor);
	//          sol.hpp는 LuaScriptInstance.cpp 안에서만 include하도록 비템플릿 진입점을 거친다
	if (Script)
		Script->Call("OnBeginOverlap", OtherActor);
}

void AActor::NotifyActorEndOverlap(AActor* OtherActor)
{
	// TODO(A): if (Script) Script->Call("OnEndOverlap", OtherActor);
	if (Script)
		Script->Call("OnEndOverlap", OtherActor);
}

void AActor::NotifyHit(AActor* OtherActor)
{
	// TODO(A): if (Script) Script->Call("OnHit", OtherActor);
	if (Script)
		Script->Call("OnHit", OtherActor);

}

void AActor::NotifyDeath(AActor* OtherActor)
{
	if (Script)
		Script->Call("OnDeath", OtherActor);
}

bool AActor::IsOverlappingActor(const AActor* Other) const
{
	if (!Other)
		return false;

	for (UActorComponent* Component : Components)
	{
		UPrimitiveComponent* Primitive =
			Cast<UPrimitiveComponent>(Component);

		if (!Primitive)
			continue;

		for (const FOverlapInfo& Info :
			Primitive->GetOverlapInfos())
		{
			if (Info.OtherComp &&
				Info.OtherComp->GetOwner() == Other)
			{
				return true;
			}
		}
	}

	return false;	return false;
}

void AActor::GetOverlappingActors(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	TSet<AActor*> Seen;
	for (UActorComponent* Component : Components)
	{
		const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive || !Primitive->IsRegistered()) continue;
		for (const FOverlapInfo& Info : Primitive->GetOverlapInfos())
		{
			if (!Info.OtherComp || !Info.OtherComp->IsRegistered()) continue;
			AActor* Other = Info.OtherComp->GetOwner();
			if (Other && Other != this && Other->GetWorld() == GetWorld() && Seen.Add(Other))
				OutActors.Add(Other);
		}
	}
}

void AActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV)
{

	// 카메라있음 -> 정보 전달
	for (UActorComponent* Component : GetComponents())
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
		{
			Camera->GetCameraView(DeltaTime, OutPOV);
			return;
		}
	}
	// 카메라없음 -> 액터자신
	OutPOV.Location = GetActorLocation();
	OutPOV.Rotation = GetActorRotation();
}