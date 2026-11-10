#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"

#include <nlohmann/json.hpp>
#include <type_traits>
#include <utility>
#include <vector>

class FAnimAssetArchive {
public:
    explicit FAnimAssetArchive(bool Loading);

public:
    bool Load(const FString& Path);
    bool Save(const FString& Path);
    bool IsLoading() const;
    bool HasError() const;
    const FString& GetError() const;
    void SetError(const FString& Error);
    bool HasField(const FString& Name) const;
    bool BeginArray(const FString& Name, uint32& Count);
    void EndArray();
    bool BeginObject(const FString& Name);
    void EndObject();

    template <typename T>
    void Serialize(const FString& Name, T& Value);

private:
    nlohmann::json mDocument{};
    std::vector<nlohmann::json*> mStack{};
    FString mError{};
    bool mLoading{};
};

template <typename T>
void FAnimAssetArchive::Serialize(const FString& Name, T& Value) {
    if (HasError()) {
        return;
    }

    auto& Object{*mStack.back()};
    if (!IsLoading()) {
        Object[Name] = Value;
        return;
    }

    if (const auto Found{Object.find(Name)}; Found != Object.end()) {
        try {
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                if (!Found->is_number_integer() || !(Found->is_number_unsigned() ? std::in_range<T>(Found->get<uint64>()) : std::in_range<T>(Found->get<int64>()))) {
                    SetError("Invalid animation asset field: " + Name);
                    return;
                }
            }
            Found->get_to(Value);
        } catch (const nlohmann::json::exception&) {
            SetError("Invalid animation asset field: " + Name);
        }
    } else {
        SetError("Missing animation asset field: " + Name);
    }
}
