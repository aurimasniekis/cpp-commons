#pragma once

/// @file
/// @brief `DisplayInfo` ⇄ nlohmann/json (gated by `COMMONS_WITH_NLOHMANN_JSON`).
///
/// Reuses the `Icon` / `Color` mappings for the `icon` / `color` fields, so it
/// pulls in those modules.

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/display_info.hpp>
#include <commons/json/color.hpp>
#include <commons/json/icon.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace comms {

// DisplayInfo ⇄ JSON object (absent fields are omitted) ----------------------

inline void to_json(::nlohmann::json& j, const DisplayInfo& d) {
    j = ::nlohmann::json::object();
    if (d.name) {
        j["name"] = *d.name;
    }
    if (d.description) {
        j["description"] = *d.description;
    }
    if (d.icon) {
        j["icon"] = *d.icon;  // reuses Icon to_json
    }
    if (d.color) {
        j["color"] = *d.color;  // reuses Color to_json
    }
}

inline void from_json(const ::nlohmann::json& j, DisplayInfo& d) {
    d = DisplayInfo{};
    if (const auto it = j.find("name"); it != j.end() && !it->is_null()) {
        d.name = it->template get<std::string>();
    }
    if (const auto it = j.find("description"); it != j.end() && !it->is_null()) {
        d.description = it->template get<std::string>();
    }
    if (const auto it = j.find("icon"); it != j.end() && !it->is_null()) {
        d.icon = it->template get<Icon>();  // reuses Icon from_json (validates)
    }
    if (const auto it = j.find("color"); it != j.end() && !it->is_null()) {
        d.color = it->template get<Color>();  // reuses Color from_json (validates)
    }
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
