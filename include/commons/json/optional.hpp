#pragma once

/// @file
/// @brief `std::optional<T>` ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <nlohmann/json.hpp>

#include <optional>

// std::optional<T> lives in namespace `std`, so (like std::complex) ADL cannot
// find a `to_json` / `from_json` for it in namespace `comms`. Specialize
// nlohmann's serializer instead: `nullopt` round-trips as JSON `null`, and a
// held value travels via the wrapped T's own serializer.
template <typename T>
struct nlohmann::adl_serializer<std::optional<T>> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const std::optional<T>& opt) {
        if (opt.has_value()) {
            j = *opt;
        } else {
            j = nullptr;
        }
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, std::optional<T>& opt) {
        if (j.is_null()) {
            opt = std::nullopt;
        } else {
            opt = j.template get<T>();
        }
    }
};  // namespace nlohmann

#endif  // COMMONS_WITH_NLOHMANN_JSON
