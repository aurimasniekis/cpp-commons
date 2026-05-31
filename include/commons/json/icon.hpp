#pragma once

/// @file
/// @brief `Icon` ⇄ nlohmann/json (gated by `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/icon.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace comms {

// Icon ⇄ JSON set:name string ------------------------------------------------

inline void to_json(::nlohmann::json& j, const Icon& i) {
    j = i.to_string();
}

inline void from_json(const ::nlohmann::json& j, Icon& i) {
    const auto str = j.template get<std::string>();
    const auto parsed = Icon::parse(str);
    if (!parsed) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: '" + str + "' is not a valid icon", &j);
    }
    i = *parsed;
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
