#pragma once

/// @file
/// @brief A value type for a [Semantic Versioning 2.0.0](https://semver.org)
///        version — `major.minor.patch` with optional prerelease and build
///        metadata.
///
/// `comms::SemVer` holds the three numeric components plus the textual
/// `prerelease` (the part after `-`, before `+`) and `build` (the part after
/// `+`). Because those are `std::string` members it is a plain runtime value
/// type — not a `constexpr` literal type like `Color`/`Icon` — so parsing and
/// comparison are ordinary runtime functions and there is no UDL literal.
///
/// `SemVer::parse` is non-throwing (mirrors `Icon::parse`) and applies the full
/// spec where it matters:
///   - **§11 prerelease precedence.** A version with no prerelease outranks one
///     that has a prerelease; otherwise dot-separated identifiers are compared
///     left to right — numerically when both are all-digit (numeric identifiers
///     carry no leading zeros, so the shorter string is the smaller number),
///     a numeric identifier ranks below an alphanumeric one, and otherwise the
///     comparison is ASCII-lexical; a longer run of equal leading identifiers
///     outranks a shorter one (`alpha < alpha.1`).
///   - **§10 build metadata.** Parsed and validated, but **ignored** by ordering
///     and equality (so `1.0.0+a == 1.0.0+b`); it is still preserved by
///     `to_string`.
///
/// As a convenience the numeric core is parsed **leniently**: `"1"` and `"1.2"`
/// fill the missing components with `0`. Prerelease/build identifiers are still
/// validated per spec (dot-separated, each non-empty and `[0-9A-Za-z-]`, numeric
/// prerelease identifiers without leading zeros); any violation yields
/// `std::nullopt`.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// `SemVer` travels as its canonical version string.
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// all emit the canonical version string.

#include <commons/types.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <format>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace comms {

/// A semantic version: `major.minor.patch` with optional `prerelease`/`build`.
struct SemVer {
    comms::u32 major = 0;
    comms::u32 minor = 0;
    comms::u32 patch = 0;
    std::string prerelease;  ///< Part after '-', before '+' (no leading '-').
    std::string build;       ///< Part after '+' (no leading '+').

    // -- parsing ------------------------------------------------------------

    /// Non-throwing parse. Accepts an optional leading `v`/`V`, a leniently
    /// partial numeric core (`"1"`, `"1.2"`, `"1.2.3"`), and validated
    /// `-prerelease` / `+build` suffixes. Returns `std::nullopt` on any
    /// malformed input.
    [[nodiscard]] static std::optional<SemVer> parse(std::string_view s) {
        if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
            s.remove_prefix(1);
        }
        if (s.empty()) {
            return std::nullopt;
        }

        // Split off `+build` at the first '+', then `-prerelease` at the first
        // '-' in what remains; the build may itself contain '-', which is why
        // it is peeled off first.
        std::string_view build;
        bool has_build = false;
        if (const auto plus = s.find('+'); plus != std::string_view::npos) {
            has_build = true;
            build = s.substr(plus + 1);
            s = s.substr(0, plus);
        }

        std::string_view prerelease;
        bool has_prerelease = false;
        if (const auto hyphen = s.find('-'); hyphen != std::string_view::npos) {
            has_prerelease = true;
            prerelease = s.substr(hyphen + 1);
            s = s.substr(0, hyphen);
        }

        // Dotted numeric core: 1-3 components, missing ones default to 0.
        if (s.empty()) {
            return std::nullopt;
        }
        std::array<comms::u32, 3> parts{0, 0, 0};
        usize index = 0;
        std::string_view core = s;
        while (true) {
            if (index == 3) {
                return std::nullopt;  // more than three numeric components
            }
            const auto dot = core.find('.');
            const std::string_view token =
                (dot == std::string_view::npos) ? core : core.substr(0, dot);
            const auto value = parse_uint(token);
            if (!value) {
                return std::nullopt;
            }
            parts[index++] = *value;
            if (dot == std::string_view::npos) {
                break;
            }
            core = core.substr(dot + 1);
        }

        if (has_prerelease && !valid_identifiers(prerelease, /*numeric_no_leading_zero=*/true)) {
            return std::nullopt;
        }
        if (has_build && !valid_identifiers(build, /*numeric_no_leading_zero=*/false)) {
            return std::nullopt;
        }

        SemVer v;
        v.major = parts[0];
        v.minor = parts[1];
        v.patch = parts[2];
        v.prerelease = std::string{prerelease};
        v.build = std::string{build};
        return v;
    }

    // -- text ---------------------------------------------------------------

    /// The canonical string: `major.minor.patch`, then `-prerelease` if set,
    /// then `+build` if set.
    [[nodiscard]] std::string to_string() const {
        std::string s =
            std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        if (!prerelease.empty()) {
            s += "-" + prerelease;
        }
        if (!build.empty()) {
            s += "+" + build;
        }
        return s;
    }

    // -- ordering -----------------------------------------------------------

    /// Total order over `major`, `minor`, `patch`, then prerelease per §11.
    /// Build metadata is excluded (§10).
    [[nodiscard]] std::strong_ordering operator<=>(const SemVer& o) const {
        if (const auto c = major <=> o.major; c != 0) {
            return c;
        }
        if (const auto c = minor <=> o.minor; c != 0) {
            return c;
        }
        if (const auto c = patch <=> o.patch; c != 0) {
            return c;
        }
        return compare_prerelease(prerelease, o.prerelease);
    }

    /// Equality consistent with `<=>` (so it likewise ignores build metadata).
    /// Cannot be defaulted: the custom `<=>` excludes `build`.
    [[nodiscard]] bool operator==(const SemVer& o) const {
        return std::is_eq(*this <=> o);
    }

private:
    /// Parse a non-empty all-digit string into a `u32`, rejecting non-digits and
    /// overflow. Leading zeros are accepted here (the numeric core is lenient);
    /// the no-leading-zero rule applies only to numeric prerelease identifiers.
    [[nodiscard]] static std::optional<comms::u32> parse_uint(const std::string_view s) {
        if (s.empty()) {
            return std::nullopt;
        }
        constexpr comms::u64 u32_max = ~static_cast<comms::u32>(0);
        comms::u64 result = 0;  // accumulate wide so overflow is detectable
        for (const char c : s) {
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            result = result * 10 + static_cast<comms::u64>(c - '0');
            if (result > u32_max) {
                return std::nullopt;
            }
        }
        return static_cast<comms::u32>(result);
    }

    /// True when `s` is non-empty and made up only of ASCII digits.
    [[nodiscard]] static bool is_numeric(std::string_view s) {
        return !s.empty() &&
               std::ranges::all_of(s, [](const char c) { return c >= '0' && c <= '9'; });
    }

    /// Validate a dot-separated identifier list: each identifier non-empty and
    /// drawn from `[0-9A-Za-z-]`; when `numeric_no_leading_zero`, an all-digit
    /// identifier must not carry a leading zero.
    [[nodiscard]] static bool valid_identifiers(const std::string_view s,
                                                const bool numeric_no_leading_zero) {
        usize start = 0;
        while (true) {
            const auto dot = s.find('.', start);
            const std::string_view id =
                (dot == std::string_view::npos) ? s.substr(start) : s.substr(start, dot - start);
            if (id.empty()) {
                return false;
            }
            bool all_digits = true;
            for (const char c : id) {
                const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                                (c >= 'a' && c <= 'z') || c == '-';
                if (!ok) {
                    return false;
                }
                if (c < '0' || c > '9') {
                    all_digits = false;
                }
            }
            if (numeric_no_leading_zero && all_digits && id.size() > 1 && id.front() == '0') {
                return false;
            }
            if (dot == std::string_view::npos) {
                return true;
            }
            start = dot + 1;
        }
    }

    /// Compare two prerelease strings per SemVer §11. An empty prerelease ranks
    /// above a non-empty one.
    [[nodiscard]] static std::strong_ordering compare_prerelease(const std::string_view a,
                                                                 const std::string_view b) {
        if (a.empty() && b.empty()) {
            return std::strong_ordering::equal;
        }
        if (a.empty()) {
            return std::strong_ordering::greater;  // no prerelease outranks one
        }
        if (b.empty()) {
            return std::strong_ordering::less;
        }

        usize ai = 0;
        usize bi = 0;
        while (ai < a.size() && bi < b.size()) {
            const auto ad = a.find('.', ai);
            const auto bd = b.find('.', bi);
            const std::string_view ida =
                (ad == std::string_view::npos) ? a.substr(ai) : a.substr(ai, ad - ai);
            const std::string_view idb =
                (bd == std::string_view::npos) ? b.substr(bi) : b.substr(bi, bd - bi);

            const bool a_num = is_numeric(ida);
            if (const bool b_num = is_numeric(idb); a_num && b_num) {
                // No leading zeros, so the shorter string is the smaller number;
                // equal length falls back to a lexical (== numeric) compare.
                if (ida.size() != idb.size()) {
                    return ida.size() <=> idb.size();
                }
                if (const auto c = ida <=> idb; c != 0) {
                    return c;
                }
            } else if (a_num != b_num) {
                return a_num ? std::strong_ordering::less : std::strong_ordering::greater;
            } else {
                if (const auto c = ida <=> idb; c != 0) {
                    return c;
                }
            }

            ai = (ad == std::string_view::npos) ? a.size() : ad + 1;
            bi = (bd == std::string_view::npos) ? b.size() : bd + 1;
        }

        // All shared identifiers equal: the longer list has higher precedence.
        const bool a_more = ai < a.size();
        if (const bool b_more = bi < b.size(); a_more == b_more) {
            return std::strong_ordering::equal;
        }
        return a_more ? std::strong_ordering::greater : std::strong_ordering::less;
    }
};

// ---------------------------------------------------------------------------
// Text output: to_string + std::ostream insertion. (std::format support is the
// std::formatter specialization below, outside namespace comms.)
// ---------------------------------------------------------------------------

/// `SemVer` as its canonical version string.
[[nodiscard]] inline std::string to_string(const SemVer& v) {
    return v.to_string();
}

inline std::ostream& operator<<(std::ostream& os, const SemVer& v) {
    return os << v.to_string();
}

}  // namespace comms

// ---------------------------------------------------------------------------
// std::format and std::hash support. The specializations live in namespace std
// (the primary templates are visible), like nlohmann's adl_serializer route in
// json.hpp.
// ---------------------------------------------------------------------------

// This spec-less formatter reads no member state, but `std::formatter` requires
// `parse`/`format` to be non-static members — so silence the convert-to-static
// suggestion here.
// NOLINTBEGIN(readability-convert-member-functions-to-static)

/// Formats `comms::SemVer` as its canonical version string. Takes no format spec.
template <>
struct std::formatter<comms::SemVer> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: SemVer takes no format spec");
        }
        return it;
    }

    auto format(const comms::SemVer& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", comms::to_string(v));
    }
};

// NOLINTEND(readability-convert-member-functions-to-static)

/// Hash consistent with `operator==`: it combines major/minor/patch and the
/// prerelease string, and (like equality) excludes build metadata.
template <>
struct std::hash<comms::SemVer> {
    [[nodiscard]] std::size_t operator()(const comms::SemVer& v) const noexcept {
        std::size_t seed = 0;
        const auto mix = [&seed](const std::size_t h) {
            seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
        mix(std::hash<comms::u32>{}(v.major));
        mix(std::hash<comms::u32>{}(v.minor));
        mix(std::hash<comms::u32>{}(v.patch));
        mix(std::hash<std::string>{}(v.prerelease));
        return seed;
    }
};
