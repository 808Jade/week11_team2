#include "EnginePCH.h"
#include "JsonArchive.h"

#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectHash.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "TypeSerializer.h"
#include "Asset/AssetManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"

#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	constexpr int32 SceneVersion = 5;

	void RequireSceneData(bool bValid, const char* Message)
	{
		if (!bValid)
			throw std::runtime_error(Message);
	}

	// Version 4 scoped component names to their actor and omitted the root reference.
	void UpgradeSceneV4(FJsonValue& Document)
	{
		uint32 NextId = 1;
		auto& Actors = Document.at("World").at("PersistentLevel").at("Actors");
		RequireSceneData(Actors.is_array(), "expected an actor array");
		for (auto& Actor : Actors)
		{
			auto& Components = Actor.at("Components");
			RequireSceneData(Components.is_array(), "expected a component array");
			std::unordered_map<FString, uint32> IdsByName;
			for (auto& Component : Components)
			{
				const FString Name = Component.at("Name").get<FString>();
				Component["Id"] = NextId;
				Component["Outer"] = 0u;
				IdsByName.try_emplace(Name, NextId++);
			}
			for (auto& Component : Components)
			{
				const auto Parent = IdsByName.find(Component.value("Parent", FString{}));
				Component["Parent"] = Parent == IdsByName.end() ? 0u : Parent->second;
			}
			Actor["RootComponent"] = nullptr; // Recovered after constructing the actor.
		}
	}

	void ReadSceneObject(UObject& Object, FArchive& Reader, const FJsonValue& Record)
	{
		FJsonArchiveInputFormatter Formatter(Reader, Record);
		FStructuredArchive Archive(Formatter);
		Object.Serialize(Archive.Open().EnterRecord());
		Archive.Close();
	}

	uint32 ReadSceneId(const FJsonValue& Record, const char* Field)
	{
		const auto& Value = Record.at(Field);
		RequireSceneData(Value.is_number_unsigned() && Value.get<uint64>() <= UINT32_MAX, "invalid object reference");
		return Value.get<uint32>();
	}

	struct FSceneActorRecord
	{
		const FJsonValue* Data;
		AActor* Actor;
	};

	struct FSceneComponentRecord
	{
		const FJsonValue* Data;
		AActor* Actor;
		uint32 OuterId;
		uint32 ParentId;
		UActorComponent* Component = nullptr;
		bool bResolving = false;
	};

	using FSceneComponentRecords = std::unordered_map<uint32, FSceneComponentRecord>;

	UActorComponent* ResolveSceneComponent(FSceneComponentRecords& Records, uint32 Id,
		std::unordered_set<UActorComponent*>& ResolvedComponents)
	{
		auto& Record = Records.at(Id);
		if (Record.Component)
			return Record.Component;
		RequireSceneData(!Record.bResolving, "circular component Outer");
		Record.bResolving = true;

		UObject* Outer = Record.Actor;
		if (Record.OuterId)
		{
			auto& OuterRecord = Records.at(Record.OuterId);
			RequireSceneData(OuterRecord.Actor == Record.Actor, "component Outer belongs to another actor");
			Outer = ResolveSceneComponent(Records, Record.OuterId, ResolvedComponents);
		}

		const FName Name(Record.Data->at("Name").get<FString>());
		UClass* Class = FindClass(Record.Data->at("Class").get<FString>());
		RequireSceneData(!Name.IsNone() && Class && Class->IsChildOf(UActorComponent::StaticClass()),
			"invalid component name or class");

		if (UObject* Existing = StaticFindObjectFast(nullptr, Outer, Name))
		{
			RequireSceneData(Existing->GetClass() == Class, "saved component class differs from the constructed object");
			Record.Component = static_cast<UActorComponent*>(Existing);
		}
		else
		{
			Record.Component = NewObject<UActorComponent>(Outer, Class, Name);
		}

		RequireSceneData(Record.Component && Record.Component->GetOwner() == Record.Actor, "invalid component owner");
		RequireSceneData(ResolvedComponents.insert(Record.Component).second, "duplicate component identity");
		return Record.Component;
	}

	USceneComponent* GetSavedSceneComponent(const FSceneComponentRecords& Records, uint32 Id)
	{
		if (!Id)
			return nullptr;
		auto* Component = Cast<USceneComponent>(Records.at(Id).Component);
		RequireSceneData(Component != nullptr, "attachment or root refers to a non-scene component");
		return Component;
	}

	void RestoreActorRoot(const FSceneActorRecord& Record, const FSceneComponentRecords& Components)
	{
		const auto& Root = Record.Data->at("RootComponent");
		if (Root.is_null())
		{
			if (!Record.Actor->GetRootComponent())
			{
				auto* LegacyRoot = Cast<USceneComponent>(Record.Actor->FindComponentByName(FName("DefaultSceneRoot")));
				Record.Actor->SetRootComponent(LegacyRoot);
			}
			return;
		}

		auto* SceneRoot = GetSavedSceneComponent(Components, ReadSceneId(*Record.Data, "RootComponent"));
		RequireSceneData(!SceneRoot || SceneRoot->GetOwner() == Record.Actor, "root belongs to another actor");
		Record.Actor->SetRootComponent(SceneRoot);
	}

	void RestoreSceneAttachments(const FSceneComponentRecords& Components, const std::vector<uint32>& Order)
	{
		for (uint32 Id : Order)
		{
			const auto& Record = Components.at(Id);
			if (Record.ParentId)
			{
				RequireSceneData(Cast<USceneComponent>(Record.Component) != nullptr, "non-scene component has an attachment");
				std::unordered_set<uint32> Ancestors;
				Ancestors.insert(Id);
				for (uint32 Parent = Record.ParentId; Parent; Parent = Components.at(Parent).ParentId)
				{
					RequireSceneData(Ancestors.insert(Parent).second, "circular attachment");
					GetSavedSceneComponent(Components, Parent);
				}
			}
			if (auto* Scene = Cast<USceneComponent>(Record.Component))
				Scene->DetachFromParent(EAttachmentRule::KeepRelative);
		}

		for (uint32 Id : Order)
		{
			const auto& Record = Components.at(Id);
			if (Record.ParentId)
				static_cast<USceneComponent*>(Record.Component)->SetupAttachment(
					GetSavedSceneComponent(Components, Record.ParentId), EAttachmentRule::KeepRelative);
			if (auto* Scene = Cast<USceneComponent>(Record.Component))
				Scene->MarkTransformDirty();
		}
	}

	void RestoreSceneActors(ULevel& Level, FArchive& Reader, const FJsonValue& LevelData)
	{
		const auto& ActorArray = LevelData.at("Actors");
		RequireSceneData(ActorArray.is_array(), "expected an actor array");
		std::vector<FSceneActorRecord> Actors;
		FSceneComponentRecords Components;
		std::vector<uint32> ComponentOrder;

		for (const auto& Data : ActorArray)
		{
			UClass* Class = FindClass(Data.at("Class").get<FString>());
			RequireSceneData(Class && Class->IsChildOf(AActor::StaticClass()), "invalid actor class");
			FName Name(Data.value("Name", FString{}));
			if (!Name.IsNone() && StaticFindObjectFast(nullptr, &Level, Name))
			{
				HTR_LOG(Warning, "Load: duplicate actor name '{}', renamed", Name.ToString());
				Name = NAME_None;
			}
			AActor* Actor = NewObject<AActor>(&Level, Class, Name);
			RequireSceneData(Actor != nullptr, "could not create actor");
			Level.AddActor(Actor);
			Actors.push_back({ &Data, Actor });

			const auto& ComponentArray = Data.at("Components");
			RequireSceneData(ComponentArray.is_array(), "expected a component array");
			for (const auto& Component : ComponentArray)
			{
				const uint32 Id = ReadSceneId(Component, "Id");
				const FSceneComponentRecord Record{ &Component, Actor, ReadSceneId(Component, "Outer"), ReadSceneId(Component, "Parent") };
				RequireSceneData(Id && Components.emplace(Id, Record).second, "duplicate or null component ID");
				ComponentOrder.push_back(Id);
			}
		}

		std::unordered_set<UActorComponent*> ResolvedComponents;
		for (uint32 Id : ComponentOrder)
			ResolveSceneComponent(Components, Id, ResolvedComponents);

		for (const auto& Record : Actors)
			ReadSceneObject(*Record.Actor, Reader, *Record.Data);
		for (uint32 Id : ComponentOrder)
		{
			const auto& Record = Components.at(Id);
			ReadSceneObject(*Record.Component, Reader, *Record.Data);
		}

		for (const auto& Record : Actors)
			RestoreActorRoot(Record, Components);
		RestoreSceneAttachments(Components, ComponentOrder);
	}

	void FinishSceneLoad(ULevel& Level)
	{
		const TArray<AActor*> Actors = Level.GetActors();
		for (AActor* Actor : Actors)
		{
			Actor->PostLoad();
			const TArray<UActorComponent*> Components = Actor->GetComponents();
			for (UActorComponent* Component : Components)
				if (Actor->GetComponents().Find(Component) != INDEX_NONE)
					Component->PostLoad();
		}

		for (AActor* Actor : Actors)
			Actor->RegisterAllComponents();
	}
}

bool FJsonArchive::SaveWorldToBytes(UWorld* World, TArray<uint8>& OutBytes)
{
	if (!World)
		return false;

	FMemoryWriter Writer(OutBytes);
	Writer.SetIsPersistent(true);   // 파일에는 Transient를 쓰지 않는다
	{
		// 선언 순서 = 소멸 역순: Ar(Close) → Formatter(dump → Writer)
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);

		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		int32 Version = SceneVersion;
		Root << SA_VALUE("Version", Version);
		World->Serialize(Root.EnterRecord("World"));

		Ar.Close();
	} 

	return !Writer.IsError();
}

bool FJsonArchive::LoadWorldFromBytes(UWorld* World, const TArray<uint8>& Bytes)
{
	if (!World)
		return false;

	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);   
	if (Reader.IsError())
	{
		HTR_LOG(Error, "Load: invalid scene data");
		return false;                               
	}

	ULevel* PersistentLevel = World->GetPersistentLevel();
	if (!PersistentLevel)
		return false;

	const auto DeleteStagedLevel = [](ULevel* Level)
	{
		Level->ClearActors();
		delete Level;
	};
	std::unique_ptr<ULevel, decltype(DeleteStagedLevel)> StagedLevel(NewObject<ULevel>(World), DeleteStagedLevel);
	if (!StagedLevel)
		return false;
	StagedLevel->SetWorld(World);

	FJsonValue& Document = Formatter.GetDocument();
	try
	{
		const int32 Version = Document.at("Version").get<int32>();
		switch (Version)
		{
		case 4:
			UpgradeSceneV4(Document);
			break;
		case SceneVersion:
			break;
		default:
			HTR_LOG(Warning, "Load: scene version {} is not supported", Version);
			return false;
		}
		RestoreSceneActors(*StagedLevel, Reader, Document.at("World").at("PersistentLevel"));
	}
	catch (const std::exception& Error)
	{
		HTR_LOG(Error, "Load: invalid scene data ({})", Error.what());
		return false;
	}
	if (Reader.IsError())
		return false;

	World->ClearWorld();
	PersistentLevel->AdoptLoadedActors(*StagedLevel);
	ReadSceneObject(*World, Reader, Document.at("World"));
	FinishSceneLoad(*PersistentLevel);
	World->GetScene().BuildBVH();
	return !Reader.IsError();
}

bool FJsonArchive::SaveWorld(UWorld* World, const FString& Path)
{
	TArray<uint8> Bytes;
	if (!SaveWorldToBytes(World, Bytes))
		return false;

	std::ofstream File(Path, std::ios::binary);
	if (!File.is_open())
		return false;
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	return !File.fail();
}

bool FJsonArchive::LoadWorld(UWorld* World, const FString& Path)
{
	if (!std::filesystem::exists(Path))
	{
		HTR_LOG(Warning, "{} is Not Exist!", Path);
		return false;
	}

	std::ifstream File(Path, std::ios::binary);
	if (!File.is_open())
		return false;

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(std::filesystem::file_size(Path)));
	File.read(reinterpret_cast<char*>(Bytes.GetData()), Bytes.Num());
	if (File.fail())
		return false;

	return LoadWorldFromBytes(World, Bytes);
}

//namespace
//{
//	// "FOV": [60.0] 처럼 배열 하나로 저장된 값과 숫자 하나 모두 받는다.
//	float ReadScalar(const json& Value, float Default)
//	{
//		if (Value.is_number())
//			return Value.get<float>();
//		if (Value.is_array() && !Value.empty() && Value[0].is_number())
//			return Value[0].get<float>();
//		return Default;
//	}
//
//	// 기본 씬 형식의 "PerspectiveCamera"를 메인 카메라에 적용한다.
//	// Rotation은 [Roll, Pitch, Yaw] 라디안이고, 엔진 FRotator는 도 단위다 (Pitch 양수 = 아래를 봄, 씬과 같은 방향).
//	void LoadPerspectiveCamera(UWorld* World, const json& CameraJson)
//	{
//		ACameraActor* CameraActor = World->GetMainCamera();
//		UCameraComponent* Camera = CameraActor ? CameraActor->GetCameraComponent() : nullptr;
//		if (!Camera || !CameraJson.is_object())
//			return;
//
//		if (CameraJson.contains("Location"))
//			Camera->SetRelativeLocation(CameraJson["Location"].get<FVector>());
//
//		if (CameraJson.contains("Rotation") && CameraJson["Rotation"].is_array() && CameraJson["Rotation"].size() >= 3)
//		{
//			const json& R = CameraJson["Rotation"];
//			Camera->SetRelativeRotation(FRotator(
//				FMath::RadiansToDegrees(R[1].get<float>()),    // Pitch
//				FMath::RadiansToDegrees(R[2].get<float>()),    // Yaw
//				FMath::RadiansToDegrees(R[0].get<float>())));  // Roll
//		}
//
//		if (CameraJson.contains("FOV"))
//			Camera->SetFieldOfView(ReadScalar(CameraJson["FOV"], Camera->GetFieldOfView()));
//		if (CameraJson.contains("NearClip"))
//			Camera->SetNearZ(ReadScalar(CameraJson["NearClip"], Camera->GetNearZ()));
//		if (CameraJson.contains("FarClip"))
//			Camera->SetFarZ(ReadScalar(CameraJson["FarClip"], Camera->GetFarZ()));
//	}
//}
//
//bool FJsonArchive::SaveWorld(UWorld* World, const FString& Path)
//{
//	if (!World)
//		return false;
//
//	ULevel* Level = World->GetPersistentLevel();
//
//	if (!Level)
//		return false;
//
//	json Json;
//
//	Json["Version"] = 2;
//	Json["Actors"] = json::array();
//
//	for (AActor* Actor : Level->GetActors())
//	{
//		if (!Actor || Actor->HasAnyFlags(EObjectFlags::RF_Transient))
//			continue;
//
//		json ActorJson;
//		ActorJson["Class"] = Actor->GetClass()->Name;
//		Actor->Serialize(ActorJson["Properties"], false);
//
//		for (UActorComponent* Component : Actor->GetComponents())
//		{
//			if (!Component) continue;
//			json ComponentJson;
//			ComponentJson["Name"] = Component->GetName();
//			ComponentJson["Class"] = Component->GetClass()->Name;
//			Component->Serialize(ComponentJson["Properties"], false);
//
//			ActorJson["Components"].push_back(ComponentJson);
//		}
//
//		Json["Actors"].push_back(ActorJson);
//	}
//
//	std::ofstream File(Path);
//
//	if (!File.is_open())
//		return false;
//
//	File << Json.dump(4);
//
//	File.close();
//
//	return !File.fail();
//}
//
//bool FJsonArchive::LoadWorld(UWorld* World, const FString& Path)
//{
//	if (!World)
//		return false;
//
//	if (!std::filesystem::exists(Path))
//	{
//		HTR_LOG(Warning, "{} is Not Exist!", Path);
//		return false;
//	}
//
//	std::ifstream File(Path);
//
//	if (!File.is_open())
//		return false;
//
//	json Json;
//
//	try
//	{
//		File >> Json;
//	}
//	catch (const json::parse_error&)
//	{
//		return false;
//	}
//
//	// 임시 DefaultScene.Scene 로딩용
//	if (Json.contains("Primitives"))
//	{
//		World->ClearWorld();
//		for (const auto& [KeyString, PrimJson] : Json["Primitives"].items())
//		{
//			FTransform Transform;
//			Transform.Location = PrimJson["Location"].get<FVector>();
//			Transform.Rotation = PrimJson["Rotation"].get<FRotator>();
//			Transform.Scale = PrimJson["Scale"].get<FVector>();
//
//			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(NAME_None, &Transform);
//			UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>(PrimJson["ObjStaticMeshAsset"].get<FString>());
//			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
//		}
//		World->GetScene().BuildBVH();
//
//		if (Json.contains("PerspectiveCamera"))
//			LoadPerspectiveCamera(World, Json["PerspectiveCamera"]);
//
//		return true;
//	}
//
//	if (!Json.contains("Version") || Json["Version"] != 2)
//		return false;
//
//	// 1. 모든 액터의 기본 정보를 먼저 검사
//	for (const json& ActorJson : Json["Actors"])
//	{
//		if (!ActorJson.is_object())
//			return false;
//
//		if (!ActorJson.contains("Class") ||
//			!ActorJson["Class"].is_string())
//			return false;
//
//		UClass* Class =
//			FindClass(ActorJson["Class"].get<FString>());
//
//		if (!Class || !Class->IsChildOf(AActor::StaticClass()))
//			return false;
//	}
//
//	if (!Json.contains("Actors") || !Json["Actors"].is_array())
//		return false;
//
//	World->ClearWorld();
//
//	// 3. 실제 생성
//	for (json& ActorJson : Json["Actors"])
//	{
//		if (!ActorJson.is_object())
//			return false;
//		if (!ActorJson.contains("Class") || !ActorJson["Class"].is_string())
//			return false;
//
//		UClass* Class = FindClass(ActorJson["Class"].get<FString>());
//		if (!Class || !Class->IsChildOf(AActor::StaticClass()))
//			return false;
//
//		AActor* Actor = World->SpawnActor(Class);
//		if (!Actor)
//		{
//			HTR_LOG(Warning, "Load: failed to spawn {}", ActorJson["Class"].get<FString>());
//			continue;
//		}
//		Actor->Serialize(ActorJson["Properties"], true);
//
//		for (json& ComponentJson : ActorJson["Components"])       // json → json&
//		{
//			const FName Name(ComponentJson["Name"].get<FString>());
//
//			// 이름으로 컴포넌트 찾기
//			UActorComponent* Component = nullptr;
//			for (UActorComponent* C : Actor->GetComponents())
//			{
//				if (C && C->GetFName() == Name)
//				{
//					Component = C;
//					break;
//				}
//			}
//
//			if (!Component)
//			{
//				HTR_LOG(Warning, "Load: {} has no component {}", Class->Name, Name.ToString());
//				continue;
//			}
//
//			if (Component->GetClass()->Name != ComponentJson["Class"].get<FString>())
//			{
//				HTR_LOG(Warning, "Load: component {} class mismatch", Name.ToString());
//				continue;
//			}
//
//			Component->Serialize(ComponentJson["Properties"], true);
//		}
//	}

	//if (!Json.contains("NextUUID"))
	//	return false;

	//// 파일 검증이 끝난 뒤 Clear
	//uint64 SavedNextUUID = Json["NextUUID"].get<uint64>();

	//World->ClearScene();

	//FEngineStatics::NextUUID = SavedNextUUID;

	//if (!Json.contains("Primitives"))
	//{
	//	return true;
	//}

	//for (auto& [UUIDString, PrimitiveJson] : Json["Primitives"].items())
	//{
	//	uint64 UUID = std::stoull(UUIDString);

	//	FTransform Transform;

	//	if (PrimitiveJson.contains("Location"))
	//	{
	//		Transform.Location.X = PrimitiveJson["Location"][0].get<float>();
	//		Transform.Location.Y = PrimitiveJson["Location"][1].get<float>();
	//		Transform.Location.Z = PrimitiveJson["Location"][2].get<float>();
	//	}

	//	if (PrimitiveJson.contains("Rotation"))
	//	{
	//		Transform.Rotation.Roll = PrimitiveJson["Rotation"][0].get<float>();
	//		Transform.Rotation.Pitch = PrimitiveJson["Rotation"][1].get<float>();
	//		Transform.Rotation.Yaw = PrimitiveJson["Rotation"][2].get<float>();
	//	}

	//	if (PrimitiveJson.contains("Scale"))
	//	{
	//		Transform.Scale.X = PrimitiveJson["Scale"][0].get<float>();
	//		Transform.Scale.Y = PrimitiveJson["Scale"][1].get<float>();
	//		Transform.Scale.Z = PrimitiveJson["Scale"][2].get<float>();
	//	}

	//	if (!PrimitiveJson.contains("Type"))
	//		continue;

	//	FString TypeString = PrimitiveJson["Type"].get<FString>();

	//	if (TypeString == "Other")
	//		continue;

	//	EPrimitiveType Type = FStringToPrimitiveType(TypeString);

	//	AStaticMeshActor* Actor =
	//		World->SpawnActor<AStaticMeshActor>("Test", &Transform);

	//	if (!Actor)
	//		continue;

	//	Actor->SetPrimitiveType(Type);
	//	Actor->SetUUID(UUID);
	//}

	//// SpawnActor하면서 증가했을 UUID를 저장 당시 값으로 복원
	//FEngineStatics::NextUUID = SavedNextUUID;

//	World->GetScene().BuildBVH();
//
//	return true;
//}