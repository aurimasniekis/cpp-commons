#pragma once

/// @file
/// @brief `SemVer` ⇄ nlohmann/json (gated by `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/semver.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace comms {

// SemVer ⇄ JSON version string -----------------------------------------------

inline void to_json(::nlohmann::json& j, const SemVer& v) {
    j = v.to_string();
}

inline void from_json(const ::nlohmann::json& j, SemVer& v) {
    const auto str = j.template get<std::string>();
    const auto parsed = SemVer::parse(str);
    if (!parsed) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: '" + str + "' is not a valid semantic version", &j);
    }
    v = *parsed;
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
