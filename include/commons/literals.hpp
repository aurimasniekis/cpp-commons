#pragma once

/// @file
/// @brief User-defined literals for the Commons value types: `_color` and
///        `_icon`.
///
/// Both live in `namespace comms::literals` — bring them in with
/// `using namespace comms::literals;`. Each is `consteval`, so a malformed
/// literal is a compile error (the `throw` is never reached in a well-formed
/// constant expression). They are kept out of `color.hpp`/`icon.hpp` so a
/// translation unit pays for the literal operators only when it asks for them;
/// the umbrella `commons/commons.hpp` includes this header.

#include <commons/color.hpp>
#include <commons/icon.hpp>

#include <cstddef>
#include <string_view>

namespace comms::literals {

/// Compile-time color literal, e.g. `"#6366f1"_color`. Accepts the hex forms of
/// `Color::parse_hex` (`#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, the `#`
/// optional). Being `consteval`, a malformed literal is a compile error.
consteval Color operator""_color(const char* str, const std::size_t len) {
    const auto c = Color::parse_hex(std::string_view{str, len});
    if (!c) {
        throw "commons: invalid color literal";
    }
    return *c;
}

/// Compile-time icon literal, e.g. `"mdi:abacus"_icon`. Accepts any well-formed
/// Iconify `set:name` value (see `Icon::parse`: exactly one `:`, non-empty parts,
/// within `Icon::capacity`). Being `consteval`, a malformed literal is a compile
/// error.
consteval Icon operator""_icon(const char* str, const std::size_t len) {
    const auto i = Icon::parse(std::string_view{str, len});
    if (!i) {
        throw "commons: invalid icon literal";
    }
    return *i;
}

}  // namespace comms::literals
