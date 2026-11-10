#include "EnginePCH.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimDataModel.h"
#include "Asset/AssetUtils.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <utility>

namespace
{
	constexpr int32 NotifyFileVersion = 1;
	constexpr const char* NotifyFileSuffix = ".notifies.json";

	std::filesystem::path ToFsPath(const FString& Value)
	{
		return std::filesystem::path(std::u8string(Value.begin(), Value.end()));
	}
}

UAnimSequenceBase::~UAnimSequenceBase()
{
	delete std::exchange(DataModel, nullptr);
}

UAnimDataModel::~UAnimDataModel()
{
	if (auto* Sequence = Cast<UAnimSequenceBase>(GetOuter()); Sequence && Sequence->DataModel == this)
		Sequence->DataModel = nullptr;
}

float UAnimSequenceBase::GetPlayLength() const
{
	return DataModel ? DataModel->GetPlayLength() : 0.0f;
}

void UAnimSequenceBase::GetAnimNotifiesFromDeltaPositions(double From, double To, bool bIncludeFrom,
	TArray<const FAnimNotifyEvent*>& OutNotifies) const
{
	if (From == To && !bIncludeFrom) return;

	const bool bForward = From <= To;
	for (const FAnimNotifyEvent& Notify : Notifies)
	{
		const double Start = Notify.TriggerTime;
		const double End = Notify.GetEndTriggerTime();

		// 정방향 (From, To] : 이벤트 [Start, End]가 To 이전에 시작하고 From 이후에 끝나야 겹칩니다.
		// 역방향 [To, From) : 거울상. Notify(Start == End)는 시점 하나가 구간 안에 있는지와 같습니다.
		const bool bOverlaps = bForward
			? (Start <= To && (bIncludeFrom ? End >= From : End > From))
			: (End >= To && (bIncludeFrom ? Start <= From : Start < From));
		if (bOverlaps)
			OutNotifies.Add(&Notify);
	}
}

FString UAnimSequenceBase::GetNotifiesFilePath(const FString& AssetPath)
{
	FString Path = FAssetUtils::ResolveAssetFilePath(AssetPath);
	if (Path.empty()) return {};
	// "AS_Run.fbxbin" → "AS_Run.notifies.json". 확장자가 없으면 그대로 붙입니다.
	const size_t Slash = Path.find_last_of("/\\");
	const size_t Dot = Path.find_last_of('.');
	if (Dot != FString::npos && (Slash == FString::npos || Dot > Slash)) Path.resize(Dot);
	return Path + NotifyFileSuffix;
}

bool UAnimSequenceBase::LoadNotifiesFromFile(const FString& AssetPath, FString& OutError)
{
	OutError.clear();
	const FString FilePath = GetNotifiesFilePath(AssetPath);
	if (FilePath.empty())
	{
		OutError = "Cannot resolve notify file for " + AssetPath;
		return false;
	}

	std::error_code Ec;
	if (!std::filesystem::exists(ToFsPath(FilePath), Ec))
	{
		Notifies.Empty();
		return true;
	}

	std::ifstream File(ToFsPath(FilePath));
	const json Root = json::parse(File, nullptr, false);
	if (Root.is_discarded() || !Root.is_object() || !Root.contains("Notifies") || !Root["Notifies"].is_array())
	{
		OutError = "Invalid notify file: " + FilePath;
		return false;
	}
	if (Root.value("Version", 0) != NotifyFileVersion)
	{
		OutError = "Unsupported notify file version: " + FilePath;
		return false;
	}

	TArray<FAnimNotifyEvent> Loaded;
	int32 Skipped = 0;
	for (const json& Item : Root["Notifies"])
	{
		const bool bValidShape = Item.is_object() && Item.contains("Name") && Item["Name"].is_string()
			&& Item.contains("Time") && Item["Time"].is_number();
		if (!bValidShape) { ++Skipped; continue; }

		FAnimNotifyEvent Notify;
		Notify.NotifyName = Item["Name"].get<FString>();
		Notify.TriggerTime = Item["Time"].get<float>();
		Notify.Duration = Item.contains("Duration") && Item["Duration"].is_number() ? Item["Duration"].get<float>() : 0.0f;
		Notify.TrackIndex = Item.contains("Track") && Item["Track"].is_number_integer() ? Item["Track"].get<int32>() : 0;
		if (Notify.NotifyName.empty() || !std::isfinite(Notify.TriggerTime) || Notify.TriggerTime < 0.0f
			|| !std::isfinite(Notify.Duration) || Notify.Duration < 0.0f || Notify.TrackIndex < 0)
		{
			++Skipped;
			continue;
		}
		Loaded.Add(std::move(Notify));
	}

	Notifies = std::move(Loaded);
	if (Skipped > 0)
		OutError = "Skipped " + std::to_string(Skipped) + " invalid notify entr" + (Skipped == 1 ? "y" : "ies") + " in " + FilePath;
	return true;
}

bool UAnimSequenceBase::SaveNotifiesToFile(const FString& AssetPath, FString& OutError) const
{
	OutError.clear();
	const FString FilePath = GetNotifiesFilePath(AssetPath);
	if (FilePath.empty())
	{
		OutError = "Cannot resolve notify file for " + AssetPath;
		return false;
	}

	const std::filesystem::path Target = ToFsPath(FilePath);
	std::error_code Ec;
	if (Notifies.IsEmpty())
	{
		// 빈 파일을 남기지 않습니다. 없는 파일을 지우는 것은 실패가 아닙니다.
		std::filesystem::remove(Target, Ec);
		if (Ec) OutError = "Cannot remove notify file: " + FilePath;
		return !Ec;
	}

	json Items = json::array();
	for (const FAnimNotifyEvent& Notify : Notifies)
	{
		Items.push_back({
			{ "Name", Notify.NotifyName },
			{ "Time", Notify.TriggerTime },
			{ "Duration", Notify.Duration },
			{ "Track", Notify.TrackIndex },
		});
	}
	const json Root = { { "Version", NotifyFileVersion }, { "Notifies", std::move(Items) } };

	// 쓰는 도중 실패해도 기존 파일이 깨지지 않도록 임시 파일에 쓴 뒤 바꿔치기합니다.
	std::filesystem::path Temp = Target;
	Temp += ".tmp";
	{
		std::ofstream File(Temp, std::ios::binary | std::ios::trunc);
		if (!File.is_open())
		{
			OutError = "Cannot write notify file: " + FilePath;
			return false;
		}
		File << Root.dump(2) << '\n';
		if (!File.good())
		{
			OutError = "Cannot write notify file: " + FilePath;
			return false;
		}
	}
	std::filesystem::rename(Temp, Target, Ec);
	if (Ec)
	{
		std::filesystem::remove(Temp, Ec);
		OutError = "Cannot replace notify file: " + FilePath;
		return false;
	}
	return true;
}
