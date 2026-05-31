#pragma once

/// @file
/// @brief `VersionConstraint` ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/version_constraint.hpp>

#include <nlohmann/json.hpp>

#include <exception>
#include <string>

namespace comms {

// VersionConstraint ⇄ JSON range string --------------------------------------
// parse() throws std::invalid_argument on a malformed sub-version; rewrap it as
// a commons JSON error to match the rest of this file.

inline void to_json(::nlohmann::json& j, const VersionConstraint& v) {
    j = v.raw();
}

inline void from_json(const ::nlohmann::json& j, VersionConstraint& v) {
    const auto str = j.template get<std::string>();
    try {
        v = VersionConstraint::parse(str);
    } catch (const std::exception&) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: '" + str + "' is not a valid version constraint", &j);
    }
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
