#pragma once

/// @file
/// @brief Optional nlohmann/json integration for the Commons types.
///
/// The whole body is gated behind `COMMONS_WITH_NLOHMANN_JSON` (see
/// `commons/config.hpp`): define it, or simply have `<nlohmann/json.hpp>` on
/// the include path, and these hooks light up. The header is otherwise inert.
///
/// What it adds:
///   - `FixedString<N>` ⇄ JSON string. A string that does not fit the
///     fixed `N` capacity throws on parse.
///   - `Color` ⇄ JSON **hex string** (`#RRGGBB`, or `#RRGGBBAA` when not
///     opaque). A string that does not parse throws.
///   - `Hsl` / `Hsv` ⇄ JSON objects (`{"h","s","l","a"}` / `{"h","s","v","a"}`).
///   - `Icon` ⇄ JSON **`set:name` string** (e.g. `"mdi:abacus"`). A string that
///     does not parse throws.
///   - `DisplayInfo` ⇄ JSON **object** with `name`/`description`/`icon`/`color`
///     keys; absent (`std::nullopt`) fields are omitted, and `icon`/`color`
///     reuse the `Icon`/`Color` mappings above.
///   - `FlagRef` ⇄ JSON **string** (the flag name); `FlagSet` ⇄ JSON **array**
///     of names. Reading them back resolves each name against the
///     `GlobalFlagRegistry`; an unknown name throws.
///   - `i128` / `u128` ⇄ JSON **decimal string**. Plain JSON numbers cannot
///     represent 128-bit integers without loss, so they travel as strings —
///     a genuinely useful integration rather than a silent narrowing.
///   - `WithPriority<T>` ⇄ JSON **object** `{"priority":N,"value":<T>}` and
///     `PrioritizedSet<T>` ⇄ JSON **array** in sorted order — both only when
///     `T` is itself json-serializable; the set's `from_json` additionally
///     requires `T`'s priority to be recoverable from the element.
///
/// The fixed-width builtin aliases (`i8`…`u64`, `f32`, `f64`, `usize`,
/// `isize`) need nothing here: nlohmann already serializes the underlying
/// arithmetic types natively.

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/color.hpp>
#include <commons/display_info.hpp>
#include <commons/fixed_string.hpp>
#include <commons/flag.hpp>
#include <commons/icon.hpp>
#include <commons/prioritized.hpp>
#include <commons/types.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <complex>
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace comms {

// FixedString<N> ⇄ JSON string ----------------------------------------------

template <std::size_t N>
inline void to_json(::nlohmann::json& j, const FixedString<N>& s) {
    j = std::string{s.view()};
}

template <std::size_t N>
inline void from_json(const ::nlohmann::json& j, FixedString<N>& s) {
    const auto str = j.template get<std::string>();
    if (str.size() > FixedString<N>::size()) {
        throw ::nlohmann::detail::other_error::create(
            502,
            "commons: string of length " + std::to_string(str.size()) +
                " does not fit FixedString<" + std::to_string(N) + "> (capacity " +
                std::to_string(FixedString<N>::size()) + ")",
            &j);
    }
    std::size_t i = 0;
    for (; i < str.size(); ++i) {
        s.value[i] = str[i];
    }
    for (; i < N; ++i) {
        s.value[i] = '\0';
    }
}

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
    if (auto it = j.find("name"); it != j.end() && !it->is_null()) {
        d.name = it->template get<std::string>();
    }
    if (auto it = j.find("description"); it != j.end() && !it->is_null()) {
        d.description = it->template get<std::string>();
    }
    if (auto it = j.find("icon"); it != j.end() && !it->is_null()) {
        d.icon = it->template get<Icon>();  // reuses Icon from_json (validates)
    }
    if (auto it = j.find("color"); it != j.end() && !it->is_null()) {
        d.color = it->template get<Color>();  // reuses Color from_json (validates)
    }
}

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

// WithPriority<T> / PrioritizedSet<T> ⇄ JSON ---------------------------------
// Gated on T being json-serializable so a non-serializable payload does not
// break this header. A WithPriority travels as {"priority":N,"value":<T>}
// (reusing T's own hooks for the value, the way DisplayInfo reuses Icon/Color);
// a PrioritizedSet travels as a JSON array in sorted (ascending-priority) order.

namespace detail {

template <typename T>
concept JsonSerializable = requires(::nlohmann::json& j, const T& v) { j = v; };

template <typename T>
concept JsonDeserializable = requires(const ::nlohmann::json& j, T& v) { j.get_to(v); };

}  // namespace detail

template <typename T>
    requires detail::JsonSerializable<T>
inline void to_json(::nlohmann::json& j, const WithPriority<T>& w) {
    j = ::nlohmann::json{{"priority", w.priority()}, {"value", w.value()}};
}

template <typename T>
    requires detail::JsonDeserializable<T>
inline void from_json(const ::nlohmann::json& j, WithPriority<T>& w) {
    j.at("value").get_to(w.value());
    w.set_priority(j.at("priority").template get<int>());
}

template <typename T>
    requires detail::JsonSerializable<T>
inline void to_json(::nlohmann::json& j, const PrioritizedSet<T>& s) {
    j = ::nlohmann::json::array();
    for (const auto& v : s) {
        j.push_back(v);  // already in sorted order
    }
}

// from_json only when the priority can be recovered from the element itself;
// otherwise the set is to_json-only (a plain T's priority is not persisted).
template <typename T>
    requires detail::JsonDeserializable<T> &&
             (std::is_base_of_v<Prioritized, T> || Prioritizable<T>)
inline void from_json(const ::nlohmann::json& j, PrioritizedSet<T>& s) {
    if (!j.is_array()) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: PrioritizedSet expects a JSON array", &j);
    }
    s.clear();
    for (const auto& elem : j) {
        T value = elem.template get<T>();
        const int p = get_priority(value);  // re-snapshot from the element
        s.insert(p, std::move(value));
    }
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

#if defined(COMMONS_HAS_INT128)

namespace detail {

[[nodiscard]] inline std::string u128_to_string(u128 v) {
    if (v == 0) {
        return "0";
    }
    // 2^128 has 39 decimal digits; 40 leaves room and avoids a reverse pass.
    std::array<char, 40> buf{};
    std::size_t pos = buf.size();
    while (v > 0) {
        buf[--pos] = static_cast<char>('0' + static_cast<int>(v % 10));
        v /= 10;
    }
    return std::string{buf.data() + pos, buf.size() - pos};
}

[[nodiscard]] inline std::string i128_to_string(i128 v) {
    const bool negative = v < 0;
    // Form the magnitude in unsigned space so INT128_MIN is handled correctly.
    u128 mag = negative ? (~static_cast<u128>(v) + 1) : static_cast<u128>(v);
    std::string digits = u128_to_string(mag);
    return negative ? "-" + digits : digits;
}

[[nodiscard]] inline u128 string_to_u128(std::string_view s) {
    if (s.empty()) {
        throw std::invalid_argument{"commons: empty string is not a u128"};
    }
    u128 r = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            throw std::invalid_argument{"commons: invalid digit in u128 string"};
        }
        r = r * 10 + static_cast<u128>(c - '0');
    }
    return r;
}

[[nodiscard]] inline i128 string_to_i128(std::string_view s) {
    if (s.empty()) {
        throw std::invalid_argument{"commons: empty string is not an i128"};
    }
    const bool negative = s.front() == '-';
    if (negative || s.front() == '+') {
        s.remove_prefix(1);
    }
    const u128 mag = string_to_u128(s);
    return negative ? static_cast<i128>(~mag + 1) : static_cast<i128>(mag);
}

}  // namespace detail

#endif  // COMMONS_HAS_INT128

}  // namespace comms

#if defined(COMMONS_HAS_INT128)

// 128-bit integers are fundamental types, so ADL cannot find a `to_json` /
// `from_json` for them in namespace `comms`. Specialize nlohmann's serializer
// directly instead. They travel as decimal strings to avoid lossy narrowing.
namespace nlohmann {

template <>
struct adl_serializer<::comms::i128> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, ::comms::i128 v) {
        j = ::comms::detail::i128_to_string(v);
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::i128& v) {
        v = ::comms::detail::string_to_i128(j.template get<std::string>());
    }
};

template <>
struct adl_serializer<::comms::u128> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, ::comms::u128 v) {
        j = ::comms::detail::u128_to_string(v);
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::u128& v) {
        v = ::comms::detail::string_to_u128(j.template get<std::string>());
    }
};

}  // namespace nlohmann

#endif  // COMMONS_HAS_INT128

// std::complex<T> lives in namespace `std`, so ADL cannot find a `to_json` /
// `from_json` for the complex aliases (cs8…cs64, cu8…cu64, cf32/cf64) in
// namespace `comms`. Specialize nlohmann's serializer instead. They travel as a
// two-element JSON array [real, imaginary]; the component type T serializes
// natively.
namespace nlohmann {

template <typename T>
struct adl_serializer<std::complex<T>> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const std::complex<T>& c) {
        j = BasicJsonType::array();
        j.push_back(c.real());
        j.push_back(c.imag());
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, std::complex<T>& c) {
        c.real(j.at(0).template get<T>());
        c.imag(j.at(1).template get<T>());
    }
};

}  // namespace nlohmann

#endif  // COMMONS_WITH_NLOHMANN_JSON
