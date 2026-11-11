#pragma once

#include "Components/ActorComponent.h"

// Edits can remove and rebuild world state using the same registration hooks.
class FComponentReregisterContext
{
public:
	explicit FComponentReregisterContext(UActorComponent* InComponent)
		: Component(InComponent)
		, World(Component && Component->IsRegistered() ? Component->GetWorld() : nullptr)
	{
		if (World)
			Component->UnregisterComponent();
	}

	~FComponentReregisterContext()
	{
		if (World && !Component->IsRegistered())
			Component->RegisterComponentWithWorld(World);
	}

	FComponentReregisterContext(const FComponentReregisterContext&) = delete;
	FComponentReregisterContext& operator=(const FComponentReregisterContext&) = delete;

private:
	UActorComponent* Component;
	UWorld* World;
};
