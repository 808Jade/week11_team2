#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"

#include <nlohmann/json.hpp>
#include <type_traits>
#include <utility>
#include <vector>

// 애니메이션 에셋의 JSON 읽기와 쓰기를 같은 Serialize 경로로 처리하고 첫 오류를 보존한다.
class FAnimAssetArchive {
public:
    // 읽기 또는 쓰기 모드를 지정하고 루트 JSON 객체와 탐색 스택을 준비한다.
    explicit FAnimAssetArchive(bool Loading);

public:
    // 크기를 제한해 JSON 파일을 읽고 루트 객체 형식을 검사한다.
    bool Load(const FString& Path);
    // JSON을 임시 파일에 기록한 뒤 대상 파일을 교체하며 실패 원인을 보관한다.
    bool Save(const FString& Path);
    // 현재 아카이브가 읽기 모드인지 반환한다.
    bool IsLoading() const;
    // 앞선 읽기, 쓰기 또는 데이터 검증에서 오류가 발생했는지 반환한다.
    bool HasError() const;
    // 가장 먼저 기록한 오류 메시지를 반환한다.
    const FString& GetError() const;
    // 첫 오류만 보관해 후속 오류가 실제 실패 원인을 덮어쓰지 않게 한다.
    void SetError(const FString& Error);
    // 현재 JSON 객체에 지정 필드가 존재하는지 확인한다.
    bool HasField(const FString& Name) const;
    // 배열 필드로 진입하고 읽기 모드에서는 항목 개수를 반환한다.
    bool BeginArray(const FString& Name, uint32& Count);
    // 현재 배열을 벗어나 직전 JSON 컨테이너로 돌아간다.
    void EndArray();
    // 현재 컨테이너의 이름 또는 배열 인덱스로 하위 객체에 진입한다.
    bool BeginObject(const FString& Name);
    // 현재 객체를 벗어나 직전 JSON 컨테이너로 돌아간다.
    void EndObject();

    template <typename T>
    // 같은 필드 이름으로 값을 읽거나 쓰며 누락, 타입 및 정수 범위 오류를 기록한다.
    void Serialize(const FString& Name, T& Value);

private:
    // 파일에 저장하거나 파일에서 읽어 온 전체 JSON 문서이다.
    nlohmann::json mDocument{};
    // 중첩된 배열과 객체의 현재 위치를 Begin/End 호출에 맞춰 추적한다.
    std::vector<nlohmann::json*> mStack{};
    // 처리 중 가장 먼저 발생한 오류이며 빈 문자열이면 오류가 없다.
    FString mError{};
    // true이면 값을 읽고 false이면 값을 JSON에 기록한다.
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
                // ID와 개수 필드에서 소수점 절삭이나 정수 범위 초과가 조용히 발생하지 않게 한다.
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
