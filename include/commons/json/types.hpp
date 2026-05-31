#pragma once

/// @file
/// @brief Fundamental-type ⇄ nlohmann/json serializers (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).
///
/// Covers the 128-bit integer aliases (`i128` / `u128`, under
/// `COMMONS_HAS_INT128`), which travel as decimal strings to avoid lossy
/// narrowing, and the complex aliases (`std::complex<T>`), which travel as a
/// two-element `[real, imaginary]` array. Both are fundamental/`std` types, so
/// ADL can't find free functions in `comms` — they use `adl_serializer`.

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/types.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <complex>
#include <cstddef>
#include <string>

#if defined(COMMONS_HAS_INT128)

namespace comms::detail {

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

[[nodiscard]] inline std::string i128_to_string(const i128 v) {
    const bool negative = v < 0;
    // Form the magnitude in unsigned space so INT128_MIN is handled correctly.
    const u128 mag = negative ? (~static_cast<u128>(v) + 1) : static_cast<u128>(v);
    std::string digits = u128_to_string(mag);
    return negative ? "-" + digits : digits;
}

// Parse a decimal JSON string into a u128, validating and range-checking as we
// go. Plain numbers narrow through `long long` and lose precision above 2^64,
// so the wire form is a string; every failure surfaces as a commons error (the
// project analog of the rest of this file's `other_error::create(502, ...)`).
template <typename BasicJsonType>
[[nodiscard]] u128 json_to_u128(const BasicJsonType& j) {
    if (!j.is_string()) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: u128 must be encoded as a JSON string", &j);
    }
    const auto& s = j.template get_ref<const std::string&>();
    if (s.empty()) {
        throw ::nlohmann::detail::other_error::create(502, "commons: u128 string is empty", &j);
    }
    constexpr u128 u128_max = ~static_cast<u128>(0);
    u128 v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: non-digit character in u128 string", &j);
        }
        const auto d = static_cast<u128>(c - '0');
        if (v > (u128_max - d) / 10) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: u128 string out of range", &j);
        }
        v = v * 10 + d;
    }
    return v;
}

// Parse a signed decimal JSON string into an i128. Accepts an optional leading
// `+`/`-`; the magnitude is accumulated in unsigned space so `i128_min` (whose
// positive counterpart does not fit in i128) round-trips without overflow.
template <typename BasicJsonType>
[[nodiscard]] i128 json_to_i128(const BasicJsonType& j) {
    if (!j.is_string()) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: i128 must be encoded as a JSON string", &j);
    }
    const auto& s = j.template get_ref<const std::string&>();
    if (s.empty()) {
        throw ::nlohmann::detail::other_error::create(502, "commons: i128 string is empty", &j);
    }
    std::size_t i = 0;
    bool negative = false;
    if (s[0] == '-') {
        negative = true;
        i = 1;
    } else if (s[0] == '+') {
        i = 1;
    }
    if (i == s.size()) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: i128 string has sign but no digits", &j);
    }
    // Permissible magnitudes: 0 .. 2^127     when negative (i128_min),
    //                         0 .. 2^127 - 1 when non-negative.
    constexpr u128 neg_mag_max = static_cast<u128>(1) << 127;
    constexpr u128 pos_mag_max = neg_mag_max - 1;
    u128 mag = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: non-digit character in i128 string", &j);
        }
        const auto d = static_cast<u128>(c - '0');
        if (mag > (neg_mag_max - d) / 10) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: i128 string out of range", &j);
        }
        mag = mag * 10 + d;
    }
    if (!negative && mag > pos_mag_max) {
        throw ::nlohmann::detail::other_error::create(502, "commons: i128 string out of range", &j);
    }
    if (negative) {
        // Map magnitude → negative without signed overflow on i128_min (mag ==
        // 2^127): v = -mag, computed as -(mag - 1) - 1.
        const u128 mm1 = mag - 1;
        const i128 partial = -static_cast<i128>(mm1);
        return partial - 1;
    }
    return static_cast<i128>(mag);
}

}  // namespace comms::detail

// 128-bit integers are fundamental types, so ADL cannot find a `to_json` /
// `from_json` for them in namespace `comms`. Specialize nlohmann's serializer
// directly instead. They travel as decimal strings to avoid lossy narrowing.
namespace nlohmann {

template <>
struct adl_serializer<::comms::i128> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const ::comms::i128 v) {
        j = ::comms::detail::i128_to_string(v);
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::i128& v) {
        v = ::comms::detail::json_to_i128(j);
    }
};

template <>
struct adl_serializer<::comms::u128> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const ::comms::u128 v) {
        j = ::comms::detail::u128_to_string(v);
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::u128& v) {
        v = ::comms::detail::json_to_u128(j);
    }
};

}  // namespace nlohmann

#endif  // COMMONS_HAS_INT128

// std::complex<T> lives in namespace `std`, so ADL cannot find a `to_json` /
// `from_json` for the complex aliases (cs8…cs64, cu8…cu64, cf32/cf64) in
// namespace `comms`. Specialize nlohmann's serializer instead. They travel as a
// two-element JSON array [real, imaginary]; the component type T serializes
// natively.
template <typename T>
struct nlohmann::adl_serializer<std::complex<T>> {
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
};  // namespace nlohmann

#endif  // COMMONS_WITH_NLOHMANN_JSON
