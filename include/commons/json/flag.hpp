#pragma once

/// @file
/// @brief `FlagRef` / `FlagSet` ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/flag.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace comms {

// FlagRef ⇄ JSON name string -------------------------------------------------
// Flags are compile-time types, so a name read back from JSON is resolved
// against the GlobalFlagRegistry rather than reconstructing a type — analogous
// to how Color/Icon validate on parse.

inline void to_json(::nlohmann::json& j, const FlagRef& f) {
    j = std::string{f.name};
}

inline void from_json(const ::nlohmann::json& j, FlagRef& f) {
    const auto name = j.template get<std::string>();
    const auto found = GlobalFlagRegistry::instance().find(name);
    if (!found) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: '" + name + "' is not a registered flag", &j);
    }
    f = *found;
}

// FlagSet ⇄ JSON array of names ----------------------------------------------

inline void to_json(::nlohmann::json& j, const FlagSet& s) {
    j = ::nlohmann::json::array();
    for (const auto& f : s) {
        j.push_back(std::string{f.name});
    }
}

inline void from_json(const ::nlohmann::json& j, FlagSet& s) {
    s.clear();
    for (const auto& elem : j) {
        s.insert(elem.template get<FlagRef>());
    }
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
