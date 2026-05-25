#pragma once

/// @file
/// @brief A tiny value type carrying an [Iconify](https://iconify.design) icon
///        identifier such as `mdi:abacus` (a `set:name` pair).
///
/// `comms::Icon` stores the canonical `set:name` string inline in a fixed char
/// buffer (no heap), so it is a constexpr literal type, trivially copyable, and
/// usable in `constexpr`/`static_assert` contexts — much like `comms::Color`.
/// Construct one from a whole value (`Icon::from("mdi:abacus")`) or from the two
/// parts (`Icon::from("mdi", "abacus")`); both validate and reject malformed or
/// oversize input. `Icon::parse` is the non-throwing variant used by the JSON
/// integration.
///
/// Predefined catalogs (e.g. `comms::Icons::mdi::abacus`) are opt-in and live
/// behind `#include <commons/icons.hpp>`; this header carries only the core type
/// and the `IconifySet<Set>` base.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// `Icon` travels as its `set:name` string.
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// all emit the canonical `set:name` string.

#include <commons/fixed_string.hpp>
#include <commons/types.hpp>

#include <format>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace comms {

/// An Iconify icon identifier — a `set:name` pair such as `mdi:abacus`.
///
/// The full `set:name` string is stored inline in `buf_`; `len_` is its length.
/// Bytes past `len_` stay zero (the buffer is value-initialized and the
/// factories only ever write `len_` bytes), which keeps the defaulted `==`
/// well-behaved.
struct Icon {
    /// Inline buffer capacity. The longest MDI `set:name` is 46 chars; 64 leaves
    /// headroom for other sets and custom identifiers.
    static constexpr usize capacity = 64;

    // -- construction -------------------------------------------------------

    constexpr Icon() = default;

    /// Build from a whole `set:name` value, e.g. `"mdi:abacus"`. Throws on
    /// malformed input (`std::invalid_argument`) or one too long to fit
    /// (`std::length_error`); in a constant-evaluated context either becomes a
    /// compile error.
    [[nodiscard]] static constexpr Icon from(const std::string_view value) {
        if (value.size() > capacity) {
            throw std::length_error{"commons: Icon value exceeds capacity"};
        }
        const auto parsed = parse(value);
        if (!parsed) {
            throw std::invalid_argument{"commons: invalid Icon value (expected 'set:name')"};
        }
        return *parsed;
    }

    /// Build by joining `set` and `name` with a `:`. Same validation and
    /// failure modes as the single-argument `from`.
    [[nodiscard]] static constexpr Icon from(const std::string_view set,
                                             const std::string_view name) {
        const usize total = set.size() + 1U + name.size();
        if (total > capacity) {
            throw std::length_error{"commons: Icon 'set:name' exceeds capacity"};
        }
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        char tmp[capacity]{};
        usize k = 0;
        for (const char c : set) {
            tmp[k++] = c;
        }
        tmp[k++] = ':';
        for (const char c : name) {
            tmp[k++] = c;
        }
        const auto parsed = parse(std::string_view{static_cast<const char*>(tmp), total});
        if (!parsed) {
            throw std::invalid_argument{
                "commons: invalid Icon set/name (expected non-empty parts)"};
        }
        return *parsed;
    }

    /// Non-throwing validation: returns the `Icon` for a well-formed value
    /// (exactly one `:`, non-empty `set` and `name`, total length `<= capacity`)
    /// or `std::nullopt`.
    [[nodiscard]] static constexpr std::optional<Icon> parse(const std::string_view value) {
        if (value.size() > capacity) {
            return std::nullopt;
        }
        usize colon = std::string_view::npos;
        usize colons = 0;
        for (usize i = 0; i < value.size(); ++i) {
            if (value[i] == ':') {
                colon = i;
                ++colons;
            }
        }
        if (colons != 1 || colon == 0 || colon + 1 == value.size()) {
            return std::nullopt;
        }
        Icon icon;
        for (usize i = 0; i < value.size(); ++i) {
            icon.buf_[i] = value[i];
        }
        icon.len_ = static_cast<u8>(value.size());
        return icon;
    }

    // -- accessors ----------------------------------------------------------

    /// The whole `set:name` string.
    [[nodiscard]] constexpr std::string_view value() const noexcept {
        return std::string_view{static_cast<const char*>(buf_), len_};
    }

    /// The set prefix (before the `:`), e.g. `mdi`.
    [[nodiscard]] constexpr std::string_view set() const noexcept {
        const auto v = value();
        return v.substr(0, colon_index(v));
    }

    /// The icon name (after the `:`), e.g. `abacus`.
    [[nodiscard]] constexpr std::string_view name() const noexcept {
        const auto v = value();
        const auto pos = colon_index(v);
        return pos == std::string_view::npos ? std::string_view{} : v.substr(pos + 1);
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return len_ == 0;
    }

    /// The canonical `set:name` string as a `std::string`.
    [[nodiscard]] std::string to_string() const {
        return std::string{value()};
    }

    bool operator==(const Icon&) const = default;

private:
    /// Index of the first ':' in `v`, or `npos`. A manual scan rather than
    /// `std::string_view::find`: libstdc++'s `find` is not usable in a constant
    /// expression when the view points into this type's inline `buf_`.
    [[nodiscard]] static constexpr usize colon_index(std::string_view v) noexcept {
        for (usize i = 0; i < v.size(); ++i) {
            if (v[i] == ':') {
                return i;
            }
        }
        return std::string_view::npos;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    char buf_[capacity]{};
    u8 len_ = 0;
};

/// Base for a predefined Iconify set: carries the set name as a `string_view`.
/// Concrete catalogs derive from it, e.g. `struct MdiSet : IconifySet<"mdi">`.
template <FixedString Set>
struct IconifySet {
    static constexpr std::string_view set = Set.view();
};

// ---------------------------------------------------------------------------
// Text output: to_string + std::ostream insertion. (std::format support is the
// std::formatter specialization below, outside namespace comms.)
// ---------------------------------------------------------------------------

/// `Icon` as its canonical `set:name` string.
[[nodiscard]] inline std::string to_string(const Icon& i) {
    return i.to_string();
}

inline std::ostream& operator<<(std::ostream& os, const Icon& i) {
    return os << i.value();
}

}  // namespace comms

// ---------------------------------------------------------------------------
// std::format support. The specialization lives in namespace std (the primary
// template is visible), like nlohmann's adl_serializer route in json.hpp.
// ---------------------------------------------------------------------------

// This spec-less formatter reads no member state, but `std::formatter` requires
// `parse`/`format` to be non-static members — so silence the convert-to-static
// suggestion here.
// NOLINTBEGIN(readability-convert-member-functions-to-static)

/// Formats `comms::Icon` as its `set:name` string. Takes no format spec.
template <>
struct std::formatter<comms::Icon> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: Icon takes no format spec");
        }
        return it;
    }

    auto format(const comms::Icon& i, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", i.value());
    }
};

// NOLINTEND(readability-convert-member-functions-to-static)
