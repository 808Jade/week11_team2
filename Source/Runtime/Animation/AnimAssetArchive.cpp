#include "EnginePCH.h"
#include "Animation/AnimAssetArchive.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Core/Misc/Guid.h"

#include <fstream>

FAnimAssetArchive::FAnimAssetArchive(bool Loading)
    : mLoading{Loading} {
    mDocument = nlohmann::json::object();
    mStack.push_back(&mDocument);
}

bool FAnimAssetArchive::Load(const FString& Path) {
    std::ifstream Input{FbxPath::FromString(Path), std::ios::binary | std::ios::ate};
    if (!Input || Input.tellg() > 4 * 1024 * 1024) {
        SetError("Cannot read animation asset: " + Path);
        return false;
    }

    Input.seekg(0);
    mDocument = nlohmann::json::parse(Input, nullptr, false);
    if (!mDocument.is_object()) {
        SetError("Invalid animation asset JSON: " + Path);
        return false;
    }

    return true;
}

bool FAnimAssetArchive::Save(const FString& Path) {
    const auto Target{FbxPath::FromString(Path)};
    auto Temporary{Target};
    Temporary += FbxPath::FromString("." + FGuid::NewGuid().ToString() + ".tmp");

    try {
        std::ofstream Output{Temporary, std::ios::binary | std::ios::trunc};
        Output << mDocument.dump(4) << '\n';
        Output.close();
        if (Output && ::MoveFileExW(Temporary.c_str(), Target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
    } catch (const nlohmann::json::exception&) {
    }

    std::error_code Ignored{};
    std::filesystem::remove(Temporary, Ignored);
    SetError("Cannot save animation asset: " + Path);
    return false;
}

bool FAnimAssetArchive::IsLoading() const {
    return mLoading;
}

bool FAnimAssetArchive::HasError() const {
    return !mError.empty();
}

const FString& FAnimAssetArchive::GetError() const {
    return mError;
}

void FAnimAssetArchive::SetError(const FString& Error) {
    if (!HasError()) {
        mError = Error;
    }
}

bool FAnimAssetArchive::HasField(const FString& Name) const {
    return mStack.back()->contains(Name);
}

bool FAnimAssetArchive::BeginArray(const FString& Name, uint32& Count) {
    auto& Object{*mStack.back()};
    if (!IsLoading()) {
        mStack.push_back(&(Object[Name] = nlohmann::json::array()));
        return true;
    }

    const auto Found{Object.find(Name)};
    if (Found == Object.end() || !Found->is_array()) {
        SetError("Invalid animation asset array: " + Name);
        return false;
    }

    Count = static_cast<uint32>(Found->size());
    mStack.push_back(&*Found);
    return true;
}

void FAnimAssetArchive::EndArray() {
    mStack.pop_back();
}

bool FAnimAssetArchive::BeginObject(const FString& Name) {
    auto& Array{*mStack.back()};
    if (!IsLoading()) {
        Array.push_back(nlohmann::json::object());
        mStack.push_back(&Array.back());
        return true;
    }

    const std::size_t Index{static_cast<std::size_t>(std::stoul(Name))};
    if (Index >= Array.size() || !Array[Index].is_object()) {
        SetError("Invalid animation asset object: " + Name);
        return false;
    }

    mStack.push_back(&Array[Index]);
    return true;
}

void FAnimAssetArchive::EndObject() {
    mStack.pop_back();
}
