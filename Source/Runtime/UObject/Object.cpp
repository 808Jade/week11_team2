#include "EnginePCH.h"
#include "Object.h"

#include "Core/EngineStatics.h"
#include "UObject/Class.h"

#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"
#include "Serialization/Formatter/BinaryArchiveFormatter.h"



TArray<UObject*> GUObjectArray;

namespace
{
	thread_local const void* ConstructingObject = nullptr;
}

FObjectConstructionScope::FObjectConstructionScope(const void* Object)
	: Previous(ConstructingObject)
{
	ConstructingObject = Object;
}

FObjectConstructionScope::~FObjectConstructionScope()
{
	ConstructingObject = Previous;
}

bool FObjectConstructionScope::IsConstructing(const void* Object)
{
	return ConstructingObject == Object;
}

UObject::UObject()
{
	ObjectUUID = FEngineStatics::GetUUID();
	InternalIndex = GUObjectArray.Num();
	GUObjectArray.Add(this);
}

UObject::UObject(bool bRegister)
{
	bIsRegistered = bRegister;
}

UObject::~UObject()
{
	DetachFromObjectRegistry();
}

void UObject::DetachFromObjectRegistry() {
	// 명시적으로 분리한 객체가 소멸할 때 같은 등록 정보를 다시 제거하지 않도록 상태도 갱신한다.
	if (bIsRegistered)
	{
		if (GetClass())
		{
			UnhashObject(this, GetClass());
		}
		int32 LastIndex = GUObjectArray.Num() - 1;
		if (InternalIndex != LastIndex)
		{
			UObject* MovedObject = GUObjectArray[LastIndex];
			GUObjectArray[InternalIndex] = MovedObject;
			MovedObject->InternalIndex = InternalIndex; // 옮겨간 오브젝트 인덱스 갱신
		}
		GUObjectArray.RemoveLast();
		bIsRegistered = false;
	}
}

UClass* UObject::StaticClass()
{
	static UClass c;
	static bool bIsInit = false;
	if (!bIsInit)
	{
		c.Name = "Object";
		c.Super = nullptr;
		c.Size = sizeof(UObject);
		c.Constructor = [](void* Memory) -> UObject*
			{
				FObjectConstructionScope Scope(Memory);
				return ::new (Memory) UObject();
			};
		bIsInit = true;
	}
	return &c;
}

bool UObject::IsA(const UClass* Class) const
{
	const UClass* ThisClass = GetClass();
	return ThisClass && ThisClass->IsChildOf(Class);
}

void UObject::Serialize(FArchive& Ar)
{
	FBinaryArchiveFormatter Formatter(Ar);
	FStructuredArchive StructuredAr(Formatter);
	Serialize(StructuredAr.Open().EnterRecord());
	StructuredAr.Close();
}

void UObject::Serialize(FStructuredArchive::FRecord Record)
{
	SerializeScriptProperties(Record.EnterField("Properties"));
}

void UObject::SerializeScriptProperties(FStructuredArchive::FSlot Slot)
{
	GetClass()->SerializeTaggedProperties(Slot, this);
}



