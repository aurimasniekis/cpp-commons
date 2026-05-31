#pragma once

/// @file
/// @brief `Color` / `Hsl` / `Hsv` ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/color.hpp>
#include <commons/types.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace comms {

// Color ⇄ JSON hex string ----------------------------------------------------

inline void to_json(::nlohmann::json& j, const Color& c) {
    j = c.to_hex_string();
}

inline void from_json(const ::nlohmann::json& j, Color& c) {
    const auto str = j.template get<std::string>();
    const auto parsed = Color::parse(str);
    if (!parsed) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: '" + str + "' is not a valid color", &j);
    }
    c = *parsed;
}

// Hsl / Hsv ⇄ JSON objects ---------------------------------------------------

inline void to_json(::nlohmann::json& j, const Hsl& c) {
    j = ::nlohmann::json{{"h", c.h}, {"s", c.s}, {"l", c.l}, {"a", c.a}};
}

inline void from_json(const ::nlohmann::json& j, Hsl& c) {
    c.h = j.at("h").template get<f64>();
    c.s = j.at("s").template get<f64>();
    c.l = j.at("l").template get<f64>();
    c.a = j.at("a").template get<f64>();
}

inline void to_json(::nlohmann::json& j, const Hsv& c) {
    j = ::nlohmann::json{{"h", c.h}, {"s", c.s}, {"v", c.v}, {"a", c.a}};
}

inline void from_json(const ::nlohmann::json& j, Hsv& c) {
    c.h = j.at("h").template get<f64>();
    c.s = j.at("s").template get<f64>();
    c.v = j.at("v").template get<f64>();
    c.a = j.at("a").template get<f64>();
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
