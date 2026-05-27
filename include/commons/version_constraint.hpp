#pragma once

/// @file
/// @brief A semver range constraint that answers `satisfies(SemVer)`.
///
/// `comms::VersionConstraint` parses an npm-flavoured range string and matches
/// it against a `comms::SemVer`. Supported tokens:
///
///   - `*`               — any version
///   - `1.2.3`           — exact match
///   - `>=1.2.0` / `>1.2.0` / `<=1.2.0` / `<1.2.0` — comparisons
///   - `!=1.2.0`         — not equal
///   - `^1.2.3`          — caret: compatible-with (`>=1.2.3 <2.0.0`; for a `0.x`
///                          version the range narrows, e.g. `^0.2.3` → `<0.3.0`,
///                          `^0.0.3` → `==0.0.3`)
///   - `~1.2.3`          — tilde: patch-level (`>=1.2.3 <1.3.0`)
///   - `>=1.0.0 <2.0.0`  — intersection: space-separated tokens, all must match
///
/// Unlike `SemVer::parse`, `VersionConstraint::parse` **throws**
/// `std::invalid_argument` on a malformed sub-version (aligning with the
/// throwing `Icon::from`); an empty input is the single `*` (any) matcher.
///
/// `VersionConstraint` carries no natural ordering — only equality (by raw
/// string) and a `std::hash`. The matching itself is backed by `SemVer`'s
/// correct §11 ordering.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// a `VersionConstraint` travels as its raw range string.
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// all emit the raw range string.

#include <commons/semver.hpp>
#include <commons/types.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace comms {

/// A semver range constraint (npm `^`/`~`, comparisons, space-separated
/// intersection).
class VersionConstraint {
    enum class Op { Any, Eq, Neq, Gt, Gte, Lt, Lte, Caret, Tilde };

    struct Matcher {
        Op op = Op::Any;
        SemVer ver;
    };

    std::string raw_;
    std::vector<Matcher> matchers_;

public:
    VersionConstraint() = default;

    /// Parse a range string into a constraint. Throws `std::invalid_argument`
    /// if any token holds a malformed version; an empty input is `*`.
    [[nodiscard]] static VersionConstraint parse(const std::string_view s) {
        VersionConstraint vc;
        vc.raw_ = std::string{s};

        for (const auto part : split(s)) {
            vc.matchers_.push_back(parse_single(part));
        }
        if (vc.matchers_.empty()) {
            vc.matchers_.push_back(Matcher{});  // Op::Any by default
        }
        return vc;
    }

    /// True when `v` satisfies every matcher (logical AND).
    [[nodiscard]] bool satisfies(const SemVer& v) const {
        return std::ranges::all_of(matchers_, [&v](const Matcher& m) { return matches(m, v); });
    }

    /// The original range string this constraint was parsed from.
    [[nodiscard]] const std::string& raw() const noexcept {
        return raw_;
    }

    /// The canonical text form — the raw range string.
    [[nodiscard]] std::string to_string() const {
        return raw_;
    }

    /// Equality by raw string (there is no semantic normalization).
    [[nodiscard]] bool operator==(const VersionConstraint& o) const {
        return raw_ == o.raw_;
    }

private:
    [[nodiscard]] static bool matches(const Matcher& m, const SemVer& v) {
        switch (m.op) {
        case Op::Any:
            return true;
        case Op::Eq:
            return v == m.ver;
        case Op::Neq:
            return v != m.ver;
        case Op::Gt:
            return v > m.ver;
        case Op::Gte:
            return v >= m.ver;
        case Op::Lt:
            return v < m.ver;
        case Op::Lte:
            return v <= m.ver;
        case Op::Caret:
            return caret_matches(m.ver, v);
        case Op::Tilde:
            return tilde_matches(m.ver, v);
        }
        return false;
    }

    /// `^c`: `v >= c` and the left-most non-zero component of `c` is held fixed
    /// (`^1.2.3` pins major, `^0.2.3` pins minor, `^0.0.3` pins patch).
    [[nodiscard]] static bool caret_matches(const SemVer& constraint, const SemVer& v) {
        if (v < constraint) {
            return false;
        }
        if (constraint.major != 0) {
            return v.major == constraint.major;
        }
        if (constraint.minor != 0) {
            return v.major == 0 && v.minor == constraint.minor;
        }
        return v.major == 0 && v.minor == 0 && v.patch == constraint.patch;
    }

    /// `~c`: `v >= c` with major and minor pinned (`~1.2.3` → `>=1.2.3 <1.3.0`).
    [[nodiscard]] static bool tilde_matches(const SemVer& constraint, const SemVer& v) {
        if (v < constraint) {
            return false;
        }
        return v.major == constraint.major && v.minor == constraint.minor;
    }

    /// Parse one trimmed token into a matcher, throwing on a malformed version.
    [[nodiscard]] static Matcher parse_single(std::string_view s) {
        while (!s.empty() && s.front() == ' ') {
            s.remove_prefix(1);
        }
        while (!s.empty() && s.back() == ' ') {
            s.remove_suffix(1);
        }

        if (s.empty() || s == "*") {
            return Matcher{};  // Op::Any by default
        }

        // Two-char operators are checked before their one-char prefixes.
        if (s.starts_with(">=")) {
            return Matcher{.op = Op::Gte, .ver = require(s.substr(2), s)};
        }
        if (s.starts_with("<=")) {
            return Matcher{.op = Op::Lte, .ver = require(s.substr(2), s)};
        }
        if (s.starts_with("!=")) {
            return Matcher{.op = Op::Neq, .ver = require(s.substr(2), s)};
        }
        if (s.starts_with("^")) {
            return Matcher{.op = Op::Caret, .ver = require(s.substr(1), s)};
        }
        if (s.starts_with("~")) {
            return Matcher{.op = Op::Tilde, .ver = require(s.substr(1), s)};
        }
        if (s.starts_with(">")) {
            return Matcher{.op = Op::Gt, .ver = require(s.substr(1), s)};
        }
        if (s.starts_with("<")) {
            return Matcher{.op = Op::Lt, .ver = require(s.substr(1), s)};
        }
        return Matcher{.op = Op::Eq, .ver = require(s, s)};
    }

    /// Parse `ver` or throw, quoting the whole `token` in the error.
    [[nodiscard]] static SemVer require(const std::string_view ver, const std::string_view token) {
        const auto parsed = SemVer::parse(ver);
        if (!parsed) {
            throw std::invalid_argument{"commons: invalid version in constraint: " +
                                        std::string{token}};
        }
        return *parsed;
    }

    /// Split a range into tokens. A space only separates tokens when it is
    /// followed by the start of another token (an operator or a digit), so a
    /// stray trailing space does not create an empty token.
    [[nodiscard]] static std::vector<std::string_view> split(std::string_view s) {
        std::vector<std::string_view> parts;
        while (!s.empty()) {
            while (!s.empty() && s.front() == ' ') {
                s.remove_prefix(1);
            }
            if (s.empty()) {
                break;
            }

            usize end = 0;
            bool in_version = false;
            for (usize i = 0; i < s.size(); ++i) {
                if (s[i] != ' ') {
                    continue;
                }
                usize next = i + 1;
                while (next < s.size() && s[next] == ' ') {
                    ++next;
                }
                if (next < s.size()) {
                    if (const char nc = s[next]; nc == '^' || nc == '~' || nc == '>' || nc == '<' ||
                                                 nc == '!' || nc == '=' ||
                                                 (nc >= '0' && nc <= '9')) {
                        end = i;
                        in_version = true;
                        break;
                    }
                }
            }

            if (in_version) {
                parts.push_back(s.substr(0, end));
                s.remove_prefix(end);
            } else {
                parts.push_back(s);
                break;
            }
        }
        return parts;
    }
};

// ---------------------------------------------------------------------------
// Text output: to_string + std::ostream insertion. (std::format support is the
// std::formatter specialization below, outside namespace comms.)
// ---------------------------------------------------------------------------

/// `VersionConstraint` as its raw range string.
[[nodiscard]] inline std::string to_string(const VersionConstraint& v) {
    return v.to_string();
}

inline std::ostream& operator<<(std::ostream& os, const VersionConstraint& v) {
    return os << v.raw();
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

/// Formats `comms::VersionConstraint` as its raw range string. Takes no spec.
template <>
struct std::formatter<comms::VersionConstraint> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: VersionConstraint takes no format spec");
        }
        return it;
    }

    auto format(const comms::VersionConstraint& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", v.raw());
    }
};

// NOLINTEND(readability-convert-member-functions-to-static)

/// Hash consistent with `operator==`: the hash of the raw range string.
template <>
struct std::hash<comms::VersionConstraint> {
    [[nodiscard]] std::size_t operator()(const comms::VersionConstraint& v) const noexcept {
        return std::hash<std::string>{}(v.raw());
    }
};
