#pragma once

/// @file
/// @brief A tiny RGBA color container with rich, mostly-`constexpr` color
///        manipulation, plus the `Hsl`/`Hsv` model structs and the CSS named
///        colors.
///
/// `comms::Color` is four `u8` channels (`r`, `g`, `b`, `a`). Everything that
/// can be is `constexpr`: packed-int conversion, HSL/HSV conversion, channel
/// and alpha tweaks, the HSL transforms (lighten/darken/…), WCAG luminance and
/// contrast, palette generation, and parsing of hex / CSS-functional / CSS
/// named colors. Only the `std::string` producers are non-`constexpr`.
///
/// Everything color-related lives in this one header on purpose: `Color::parse`
/// resolves color names through `CssColors::parse`, and the named colors are
/// themselves `Color` values, so splitting them would create an include cycle.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// `Color` travels as a hex string (`#RRGGBB`, or `#RRGGBBAA` when not opaque);
/// `Hsl`/`Hsv` travel as JSON objects.
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// support for all three types. `Color`'s formatter accepts `h` (lowercase
/// hex, the default), `H` (uppercase hex), and `r` (CSS `rgb(...)`).

#include <commons/types.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace comms {

// ---------------------------------------------------------------------------
// detail — small constexpr math/parse helpers.
//
// These exist so the whole API can stay `constexpr` without leaning on the
// non-portable C++23 `<cmath>` constexpr-ness. The only transcendental need is
// the sRGB gamma `pow(x, 2.4)`, served by `exp_`/`log_` with range reduction.
// ---------------------------------------------------------------------------
namespace detail {

// The math helpers carry a trailing underscore (`abs_`, `min_`, `pow_`, …) on
// purpose: it keeps them from shadowing the like-named `<cmath>` functions
// while staying recognizable. That trips the lower_case naming check, so it is
// silenced for this internal helper block.
// NOLINTBEGIN(readability-identifier-naming)

constexpr char lower_(const char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr int hex_val(const char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

constexpr bool is_space(const char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

constexpr std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

/// Index of the first `c` in `s`, or `npos`. A manual scan rather than
/// `std::string_view::find`, whose libstdc++ implementation is not usable in a
/// constant expression here (it routes through a non-constexpr pointer path).
constexpr usize find_char(std::string_view s, char c) {
    for (usize i = 0; i < s.size(); ++i) {
        if (s[i] == c) {
            return i;
        }
    }
    return std::string_view::npos;
}

constexpr bool eq_ci(const std::string_view a, const std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (usize i = 0; i < a.size(); ++i) {
        if (lower_(a[i]) != lower_(b[i])) {
            return false;
        }
    }
    return true;
}

constexpr f64 abs_(const f64 x) {
    return x < 0.0 ? -x : x;
}

constexpr f64 min_(const f64 a, const f64 b) {
    return a < b ? a : b;
}

constexpr f64 max_(const f64 a, const f64 b) {
    return a > b ? a : b;
}

constexpr f64 clamp_(const f64 x, const f64 lo, const f64 hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

constexpr f64 trunc_(const f64 x) {
    return static_cast<f64>(static_cast<long long>(x));
}

/// Round a non-negative value to the nearest integer. Routed through `trunc_`
/// so it stays valid for the clamped (non-negative) inputs we feed it.
constexpr f64 round_nonneg(const f64 x) {
    return trunc_(x + 0.5);
}

constexpr f64 fmod_(const f64 x, const f64 y) {
    return y == 0.0 ? 0.0 : x - trunc_(x / y) * y;
}

/// Wrap a hue in degrees into the canonical `[0, 360)` range.
constexpr f64 wrap_hue(f64 h) {
    h = fmod_(h, 360.0);
    return h < 0.0 ? h + 360.0 : h;
}

/// Map a unit value `[0, 1]` to a `u8` channel, clamping then rounding.
constexpr u8 round_u8(const f64 v) {
    return static_cast<u8>(round_nonneg(clamp_(v, 0.0, 1.0) * 255.0));
}

/// Round an already-`[0, 255]`-scaled value to a `u8`, clamping first.
constexpr u8 round_channel(const f64 v) {
    return static_cast<u8>(round_nonneg(clamp_(v, 0.0, 255.0)));
}

inline constexpr f64 ln2_ = 0.6931471805599453;

/// Natural log for `x > 0`, via power-of-two range reduction and the
/// `atanh`-style series; error well below 1e-12 over the reduced interval.
constexpr f64 log_(f64 x) {
    if (x <= 0.0) {
        return 0.0;
    }
    int k = 0;
    while (x > 1.5) {
        x *= 0.5;
        ++k;
    }
    while (x < 0.75) {
        x *= 2.0;
        --k;
    }
    const f64 t = (x - 1.0) / (x + 1.0);
    const f64 t2 = t * t;
    f64 term = t;
    f64 sum = 0.0;
    for (int n = 1; n < 40; n += 2) {
        sum += term / static_cast<f64>(n);
        term *= t2;
    }
    return 2.0 * sum + static_cast<f64>(k) * ln2_;
}

/// `e^x`, via reduction to `x = k*ln2 + r` with `|r| <= ln2/2` and a Taylor
/// series for `e^r`, then an exact integer scale by `2^k`.
constexpr f64 exp_(const f64 x) {
    const f64 k = trunc_(x / ln2_ + (x >= 0.0 ? 0.5 : -0.5));
    const f64 r = x - k * ln2_;
    f64 term = 1.0;
    f64 sum = 1.0;
    for (int n = 1; n < 20; ++n) {
        term *= r / static_cast<f64>(n);
        sum += term;
    }
    if (const int ki = static_cast<int>(k); ki >= 0) {
        for (int i = 0; i < ki; ++i) {
            sum *= 2.0;
        }
    } else {
        for (int i = 0; i < -ki; ++i) {
            sum *= 0.5;
        }
    }
    return sum;
}

/// `base^exp` for `base >= 0`. Used only for the sRGB gamma `pow(x, 2.4)`.
constexpr f64 pow_(const f64 base, const f64 exp) {
    if (base <= 0.0) {
        return 0.0;
    }
    return exp_(exp * log_(base));
}

/// One sRGB channel (unit `[0, 1]`) to linear light, per the WCAG definition.
constexpr f64 srgb_to_linear(const f64 cs) {
    return cs <= 0.04045 ? cs / 12.92 : pow_((cs + 0.055) / 1.055, 2.4);
}

/// A number parsed out of a CSS-functional component: its value, whether it
/// carried a `%` suffix, and whether the parse succeeded at all.
struct ParsedNum {
    f64 value = 0.0;
    bool percent = false;
    bool ok = false;
};

/// Parse a single decimal number (optional sign, optional fraction, optional
/// trailing `%`). No exponent form — CSS colors never need it.
constexpr ParsedNum parse_num(std::string_view s) {
    s = trim(s);
    bool percent = false;
    if (!s.empty() && s.back() == '%') {
        percent = true;
        s.remove_suffix(1);
        s = trim(s);
    }
    if (s.empty()) {
        return {0.0, percent, false};
    }
    usize i = 0;
    f64 sign = 1.0;
    if (s[i] == '+') {
        ++i;
    } else if (s[i] == '-') {
        sign = -1.0;
        ++i;
    }
    bool any = false;
    f64 intpart = 0.0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        intpart = intpart * 10.0 + static_cast<f64>(s[i] - '0');
        ++i;
        any = true;
    }
    f64 frac = 0.0;
    if (i < s.size() && s[i] == '.') {
        f64 scale = 1.0;
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            scale *= 0.1;
            frac += static_cast<f64>(s[i] - '0') * scale;
            ++i;
            any = true;
        }
    }
    if (!any || i != s.size()) {
        return {0.0, percent, false};
    }
    return {sign * (intpart + frac), percent, true};
}

/// Split a CSS-functional argument list into at most four tokens. Components
/// are separated by commas, whitespace, or the `/` that precedes a modern
/// alpha. Returns the token count, or -1 if there are more than four.
constexpr int split_components(const std::string_view s, std::array<std::string_view, 4>& out) {
    auto sep = [](const char c) { return c == ' ' || c == '\t' || c == ',' || c == '/'; };
    int n = 0;
    usize i = 0;
    while (i < s.size()) {
        while (i < s.size() && sep(s[i])) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        usize j = i;
        while (j < s.size() && !sep(s[j])) {
            ++j;
        }
        if (n >= 4) {
            return -1;
        }
        out[static_cast<usize>(n++)] = s.substr(i, j - i);
        i = j;
    }
    return n;
}

inline std::string hex2(const u8 v) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    out.push_back(digits[static_cast<usize>(v >> 4)]);
    out.push_back(digits[static_cast<usize>(v & 0x0F)]);
    return out;
}

// NOLINTEND(readability-identifier-naming)

}  // namespace detail

// ---------------------------------------------------------------------------
// HSL / HSV model structs. Channels are unit-ranged doubles: h in [0, 360),
// s/l/v/a in [0, 1].
// ---------------------------------------------------------------------------

/// Hue / saturation / lightness, plus unit alpha.
struct Hsl {
    f64 h = 0.0;  ///< Hue in degrees, `[0, 360)`.
    f64 s = 0.0;  ///< Saturation, `[0, 1]`.
    f64 l = 0.0;  ///< Lightness, `[0, 1]`.
    f64 a = 1.0;  ///< Alpha (opacity), `[0, 1]`.

    constexpr bool operator==(const Hsl&) const = default;  ///< Field-wise equality.
};

/// Hue / saturation / value (brightness), plus unit alpha.
struct Hsv {
    f64 h = 0.0;  ///< Hue in degrees, `[0, 360)`.
    f64 s = 0.0;  ///< Saturation, `[0, 1]`.
    f64 v = 0.0;  ///< Value (brightness), `[0, 1]`.
    f64 a = 1.0;  ///< Alpha (opacity), `[0, 1]`.

    constexpr bool operator==(const Hsv&) const = default;  ///< Field-wise equality.
};

// ---------------------------------------------------------------------------
// Color — four u8 channels with a constexpr-first manipulation API.
// ---------------------------------------------------------------------------

/// An 8-bit-per-channel RGBA color. Default is opaque black.
struct Color {
    u8 r = 0;    ///< Red channel, `[0, 255]`.
    u8 g = 0;    ///< Green channel, `[0, 255]`.
    u8 b = 0;    ///< Blue channel, `[0, 255]`.
    u8 a = 255;  ///< Alpha channel, `[0, 255]` (255 = opaque).

    // -- construction -------------------------------------------------------

    constexpr Color() = default;

    constexpr Color(const u8 red, const u8 green, const u8 blue, const u8 alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    [[nodiscard]] static constexpr Color rgb(const u8 red, const u8 green, const u8 blue) {
        return Color(red, green, blue, 255);
    }

    [[nodiscard]] static constexpr Color
    rgba(const u8 red, const u8 green, const u8 blue, const u8 alpha) {
        return Color(red, green, blue, alpha);
    }

    // -- packed integers ----------------------------------------------------

    /// 0xRRGGBB (alpha forced opaque).
    [[nodiscard]] static constexpr Color from_rgb_int(const u32 v) {
        return Color(static_cast<u8>((v >> 16) & 0xFFU),
                     static_cast<u8>((v >> 8) & 0xFFU),
                     static_cast<u8>(v & 0xFFU),
                     255);
    }

    [[nodiscard]] constexpr u32 to_rgb_int() const {
        return (static_cast<u32>(r) << 16) | (static_cast<u32>(g) << 8) | static_cast<u32>(b);
    }

    /// 0xRRGGBBAA.
    [[nodiscard]] static constexpr Color from_rgba_int(const u32 v) {
        return Color(static_cast<u8>((v >> 24) & 0xFFU),
                     static_cast<u8>((v >> 16) & 0xFFU),
                     static_cast<u8>((v >> 8) & 0xFFU),
                     static_cast<u8>(v & 0xFFU));
    }

    [[nodiscard]] constexpr u32 to_rgba_int() const {
        return (static_cast<u32>(r) << 24) | (static_cast<u32>(g) << 16) |
               (static_cast<u32>(b) << 8) | static_cast<u32>(a);
    }

    /// 0xAARRGGBB.
    [[nodiscard]] static constexpr Color from_argb_int(const u32 v) {
        return Color(static_cast<u8>((v >> 16) & 0xFFU),
                     static_cast<u8>((v >> 8) & 0xFFU),
                     static_cast<u8>(v & 0xFFU),
                     static_cast<u8>((v >> 24) & 0xFFU));
    }

    [[nodiscard]] constexpr u32 to_argb_int() const {
        return (static_cast<u32>(a) << 24) | (static_cast<u32>(r) << 16) |
               (static_cast<u32>(g) << 8) | static_cast<u32>(b);
    }

    // -- model conversion ---------------------------------------------------

    [[nodiscard]] constexpr Hsl to_hsl() const {
        using namespace detail;
        const f64 rr = static_cast<f64>(r) / 255.0;
        const f64 gg = static_cast<f64>(g) / 255.0;
        const f64 bb = static_cast<f64>(b) / 255.0;
        const f64 mx = max_(rr, max_(gg, bb));
        const f64 mn = min_(rr, min_(gg, bb));
        const f64 d = mx - mn;
        const f64 l = (mx + mn) / 2.0;
        f64 h = 0.0;
        f64 s = 0.0;
        if (d != 0.0) {
            s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
            if (mx == rr) {
                h = (gg - bb) / d;
            } else if (mx == gg) {
                h = (bb - rr) / d + 2.0;
            } else {
                h = (rr - gg) / d + 4.0;
            }
            h = wrap_hue(h * 60.0);
        }
        return Hsl{h, s, l, static_cast<f64>(a) / 255.0};
    }

    [[nodiscard]] constexpr Hsv to_hsv() const {
        using namespace detail;
        const f64 rr = static_cast<f64>(r) / 255.0;
        const f64 gg = static_cast<f64>(g) / 255.0;
        const f64 bb = static_cast<f64>(b) / 255.0;
        const f64 mx = max_(rr, max_(gg, bb));
        const f64 mn = min_(rr, min_(gg, bb));
        const f64 d = mx - mn;
        f64 h = 0.0;
        if (d != 0.0) {
            if (mx == rr) {
                h = (gg - bb) / d;
            } else if (mx == gg) {
                h = (bb - rr) / d + 2.0;
            } else {
                h = (rr - gg) / d + 4.0;
            }
            h = wrap_hue(h * 60.0);
        }
        const f64 s = mx == 0.0 ? 0.0 : d / mx;
        return Hsv{h, s, mx, static_cast<f64>(a) / 255.0};
    }

    [[nodiscard]] static constexpr Color from_hsl(const Hsl& in) {
        using namespace detail;
        const f64 h = wrap_hue(in.h);
        const f64 s = clamp_(in.s, 0.0, 1.0);
        const f64 l = clamp_(in.l, 0.0, 1.0);
        const f64 c = (1.0 - abs_(2.0 * l - 1.0)) * s;
        const f64 hp = h / 60.0;
        const f64 x = c * (1.0 - abs_(fmod_(hp, 2.0) - 1.0));
        const f64 m = l - c / 2.0;
        f64 r1 = 0.0;
        f64 g1 = 0.0;
        f64 b1 = 0.0;
        sector(hp, c, x, r1, g1, b1);
        return Color(round_u8(r1 + m), round_u8(g1 + m), round_u8(b1 + m), round_u8(in.a));
    }

    [[nodiscard]] static constexpr Color from_hsv(const Hsv& in) {
        using namespace detail;
        const f64 h = wrap_hue(in.h);
        const f64 s = clamp_(in.s, 0.0, 1.0);
        const f64 v = clamp_(in.v, 0.0, 1.0);
        const f64 c = v * s;
        const f64 hp = h / 60.0;
        const f64 x = c * (1.0 - abs_(fmod_(hp, 2.0) - 1.0));
        const f64 m = v - c;
        f64 r1 = 0.0;
        f64 g1 = 0.0;
        f64 b1 = 0.0;
        sector(hp, c, x, r1, g1, b1);
        return Color(round_u8(r1 + m), round_u8(g1 + m), round_u8(b1 + m), round_u8(in.a));
    }

    // -- channels / alpha ---------------------------------------------------

    [[nodiscard]] constexpr Color with_red(const u8 red) const {
        return Color(red, g, b, a);
    }
    [[nodiscard]] constexpr Color with_green(const u8 green) const {
        return Color(r, green, b, a);
    }
    [[nodiscard]] constexpr Color with_blue(const u8 blue) const {
        return Color(r, g, blue, a);
    }
    [[nodiscard]] constexpr Color with_alpha(const u8 alpha) const {
        return Color(r, g, b, alpha);
    }

    /// Set the alpha channel to the given `[0, 1]` opacity.
    [[nodiscard]] constexpr Color fade(const f64 opacity_unit) const {
        return Color(r, g, b, detail::round_u8(opacity_unit));
    }

    /// Alias for `fade` — set the alpha channel to a `[0, 1]` opacity.
    [[nodiscard]] constexpr Color opacity(const f64 opacity_unit) const {
        return fade(opacity_unit);
    }

    // -- HSL transforms (each: to_hsl -> mutate -> from_hsl) -----------------

    /// Increase lightness by `amount` (unit delta, clamped).
    [[nodiscard]] constexpr Color lighten(const f64 amount) const {
        Hsl c = to_hsl();
        c.l += amount;
        return from_hsl(c);
    }
    /// Decrease lightness by `amount` (unit delta, clamped).
    [[nodiscard]] constexpr Color darken(const f64 amount) const {
        Hsl c = to_hsl();
        c.l -= amount;
        return from_hsl(c);
    }
    /// Increase saturation by `amount` (unit delta, clamped).
    [[nodiscard]] constexpr Color saturate(const f64 amount) const {
        Hsl c = to_hsl();
        c.s += amount;
        return from_hsl(c);
    }
    /// Decrease saturation by `amount` (unit delta, clamped).
    [[nodiscard]] constexpr Color desaturate(const f64 amount) const {
        Hsl c = to_hsl();
        c.s -= amount;
        return from_hsl(c);
    }
    /// Rotate the hue by `degrees` around the color wheel.
    [[nodiscard]] constexpr Color rotate_hue(const f64 degrees) const {
        Hsl c = to_hsl();
        c.h = detail::wrap_hue(c.h + degrees);
        return from_hsl(c);
    }
    [[nodiscard]] constexpr Color with_hue(const f64 hue) const {
        Hsl c = to_hsl();
        c.h = detail::wrap_hue(hue);
        return from_hsl(c);
    }
    [[nodiscard]] constexpr Color with_saturation(const f64 saturation) const {
        Hsl c = to_hsl();
        c.s = detail::clamp_(saturation, 0.0, 1.0);
        return from_hsl(c);
    }
    [[nodiscard]] constexpr Color with_lightness(const f64 lightness) const {
        Hsl c = to_hsl();
        c.l = detail::clamp_(lightness, 0.0, 1.0);
        return from_hsl(c);
    }

    /// Adopt the hue and saturation of `other`, keeping this color's lightness
    /// and alpha.
    [[nodiscard]] constexpr Color recolor(const Color other) const {
        const Hsl t = other.to_hsl();
        Hsl c = to_hsl();
        c.h = t.h;
        c.s = t.s;
        return from_hsl(c);
    }

    /// Alias for `lighten`.
    [[nodiscard]] constexpr Color tint(const f64 amount) const {
        return lighten(amount);
    }
    /// Alias for `darken`.
    [[nodiscard]] constexpr Color shade(const f64 amount) const {
        return darken(amount);
    }
    /// Alias for `desaturate`.
    [[nodiscard]] constexpr Color tone(const f64 amount) const {
        return desaturate(amount);
    }

    // -- state --------------------------------------------------------------

    [[nodiscard]] constexpr bool is_opaque() const {
        return a == 255;
    }
    [[nodiscard]] constexpr bool is_transparent() const {
        return a == 0;
    }
    [[nodiscard]] constexpr bool has_alpha() const {
        return a != 255;
    }

    // -- accessors ----------------------------------------------------------

    [[nodiscard]] constexpr f64 hue() const {
        return to_hsl().h;
    }
    [[nodiscard]] constexpr f64 saturation() const {
        return to_hsl().s;
    }
    [[nodiscard]] constexpr f64 lightness() const {
        return to_hsl().l;
    }
    [[nodiscard]] constexpr f64 value() const {
        return to_hsv().v;
    }
    [[nodiscard]] constexpr f64 alpha_unit() const {
        return static_cast<f64>(a) / 255.0;
    }

    // -- UI helpers ---------------------------------------------------------

    /// Linearly interpolate toward `other` by `t` (clamped to `[0, 1]`),
    /// including the alpha channel. `t == 0` is this color, `t == 1` is `other`.
    [[nodiscard]] constexpr Color mix(const Color other, f64 t) const {
        using namespace detail;
        t = clamp_(t, 0.0, 1.0);
        auto lerp = [t](const u8 x, const u8 y) {
            return round_channel(static_cast<f64>(x) + (static_cast<f64>(y) - x) * t);
        };
        return Color(lerp(r, other.r), lerp(g, other.g), lerp(b, other.b), lerp(a, other.a));
    }

    /// Alpha-composite this color (source) over `bg` using the source-over
    /// operator. The result's alpha is the combined coverage.
    [[nodiscard]] constexpr Color blend_over(const Color bg) const {
        using namespace detail;
        const f64 sa = alpha_unit();
        const f64 ba = bg.alpha_unit();
        const f64 oa = sa + ba * (1.0 - sa);
        if (oa <= 0.0) {
            return Color(0, 0, 0, 0);
        }
        auto comp = [&](const u8 sc, const u8 bc) {
            return (static_cast<f64>(sc) / 255.0 * sa +
                    static_cast<f64>(bc) / 255.0 * ba * (1.0 - sa)) /
                   oa;
        };
        return Color(round_u8(comp(r, bg.r)),
                     round_u8(comp(g, bg.g)),
                     round_u8(comp(b, bg.b)),
                     round_u8(oa));
    }

    /// Invert the RGB channels, keeping alpha.
    [[nodiscard]] constexpr Color invert() const {
        return Color(
            static_cast<u8>(255 - r), static_cast<u8>(255 - g), static_cast<u8>(255 - b), a);
    }

    /// Convert to gray using the Rec.601 luma weights (0.299 R, 0.587 G,
    /// 0.114 B), keeping alpha.
    [[nodiscard]] constexpr Color grayscale() const {
        const f64 y =
            0.299 * static_cast<f64>(r) + 0.587 * static_cast<f64>(g) + 0.114 * static_cast<f64>(b);
        const u8 v = detail::round_channel(y);
        return Color(v, v, v, a);
    }

    /// The hue-opposite color (hue rotated 180°).
    [[nodiscard]] constexpr Color complement() const {
        return rotate_hue(180.0);
    }

    // -- WCAG ---------------------------------------------------------------

    /// Relative luminance per WCAG 2.x (sRGB linearized, then the 0.2126 /
    /// 0.7152 / 0.0722 weights). Alpha is ignored.
    [[nodiscard]] constexpr f64 relative_luminance() const {
        const f64 lr = detail::srgb_to_linear(static_cast<f64>(r) / 255.0);
        const f64 lg = detail::srgb_to_linear(static_cast<f64>(g) / 255.0);
        const f64 lb = detail::srgb_to_linear(static_cast<f64>(b) / 255.0);
        return 0.2126 * lr + 0.7152 * lg + 0.0722 * lb;
    }

    /// WCAG contrast ratio against `other`, in `[1, 21]`. Symmetric.
    [[nodiscard]] constexpr f64 contrast_ratio(const Color other) const {
        const f64 l1 = relative_luminance();
        const f64 l2 = other.relative_luminance();
        return (detail::max_(l1, l2) + 0.05) / (detail::min_(l1, l2) + 0.05);
    }

    /// True when black text is at least as readable on this color as white.
    [[nodiscard]] constexpr bool is_light() const {
        return contrast_ratio(Color(0, 0, 0)) >= contrast_ratio(Color(255, 255, 255));
    }
    [[nodiscard]] constexpr bool is_dark() const {
        return !is_light();
    }

    /// Black or white, whichever has the higher contrast against this color —
    /// the better foreground text color.
    [[nodiscard]] constexpr Color readable_text_color() const {
        return is_light() ? Color(0, 0, 0) : Color(255, 255, 255);
    }

    /// True when `text` over this background meets WCAG AA for normal text
    /// (contrast ratio >= 4.5).
    [[nodiscard]] constexpr bool has_readable_contrast(const Color text) const {
        return contrast_ratio(text) >= 4.5;
    }

    // -- palettes -----------------------------------------------------------

    /// The two split-complementary colors (hue ±150°).
    [[nodiscard]] constexpr std::array<Color, 2> split_complementary() const {
        return {rotate_hue(150.0), rotate_hue(210.0)};
    }

    /// This color plus the two triadic colors (hue +120°, +240°).
    [[nodiscard]] constexpr std::array<Color, 3> triadic() const {
        return {*this, rotate_hue(120.0), rotate_hue(240.0)};
    }

    /// This color flanked by its two analogous neighbors (hue ∓`angle`).
    [[nodiscard]] constexpr std::array<Color, 3> analogous(const f64 angle = 30.0) const {
        return {rotate_hue(-angle), *this, rotate_hue(angle)};
    }

    // -- string output ------------------------------------------------------

    /// Canonical hex: `#RRGGBB` when opaque, `#RRGGBBAA` otherwise.
    [[nodiscard]] std::string to_hex_string() const {
        return is_opaque() ? to_hex_rgb_string() : to_hex_rgba_string();
    }

    [[nodiscard]] std::string to_hex_rgb_string() const {
        return "#" + detail::hex2(r) + detail::hex2(g) + detail::hex2(b);
    }

    [[nodiscard]] std::string to_hex_rgba_string() const {
        return "#" + detail::hex2(r) + detail::hex2(g) + detail::hex2(b) + detail::hex2(a);
    }

    /// CSS space-separated form, e.g. `"rgb(99 102 241)"`.
    [[nodiscard]] std::string to_css_rgb_string() const {
        return "rgb(" + std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b) + ")";
    }

    /// CSS form with alpha, e.g. `"rgb(99 102 241 / 1.0)"` (alpha to one
    /// decimal place).
    [[nodiscard]] std::string to_css_rgba_string() const {
        const int tenths = static_cast<int>(detail::round_nonneg(alpha_unit() * 10.0));
        const std::string alpha = std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
        return "rgb(" + std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b) +
               " / " + alpha + ")";
    }

    // -- parsing ------------------------------------------------------------

    /// Parse a hex color: `#rgb`, `#rgba`, `#rrggbb`, or `#rrggbbaa` (the
    /// leading `#` is optional). Returns `nullopt` on any malformed input.
    [[nodiscard]] static constexpr std::optional<Color> parse_hex(std::string_view s) {
        s = detail::trim(s);
        if (!s.empty() && s.front() == '#') {
            s.remove_prefix(1);
        }
        for (const char c : s) {
            if (detail::hex_val(c) < 0) {
                return std::nullopt;
            }
        }
        auto h1 = [&](const usize i) {
            const int v = detail::hex_val(s[i]);
            return static_cast<u8>(v * 16 + v);
        };
        auto h2 = [&](const usize i) {
            return static_cast<u8>(detail::hex_val(s[i]) * 16 + detail::hex_val(s[i + 1]));
        };
        switch (s.size()) {
        case 3:
            return Color(h1(0), h1(1), h1(2), 255);
        case 4:
            return Color(h1(0), h1(1), h1(2), h1(3));
        case 6:
            return Color(h2(0), h2(2), h2(4), 255);
        case 8:
            return Color(h2(0), h2(2), h2(4), h2(6));
        default:
            return std::nullopt;
        }
    }

    /// Parse any supported textual color: hex (see `parse_hex`), CSS-functional
    /// `rgb()/rgba()/hsl()/hsla()`, or a CSS named color. Returns `nullopt` if
    /// none match. Defined out-of-line below, once `CssColors` is complete.
    [[nodiscard]] static constexpr std::optional<Color> parse(std::string_view s);

    friend constexpr bool operator==(Color, Color) = default;

private:
    /// Place chroma `c` and second-largest component `x` into RGB by hue
    /// sextant `hp` in `[0, 6)`.
    static constexpr void
    sector(const f64 hp, const f64 c, const f64 x, f64& r1, f64& g1, f64& b1) {
        if (hp < 1.0) {
            r1 = c;
            g1 = x;
        } else if (hp < 2.0) {
            r1 = x;
            g1 = c;
        } else if (hp < 3.0) {
            g1 = c;
            b1 = x;
        } else if (hp < 4.0) {
            g1 = x;
            b1 = c;
        } else if (hp < 5.0) {
            r1 = x;
            b1 = c;
        } else {
            r1 = c;
            b1 = x;
        }
    }
};

// ---------------------------------------------------------------------------
// CSS named colors. The `static constexpr Color` members are the single source
// of truth for the values; `parse` looks names up by referencing those members.
// ---------------------------------------------------------------------------

/// The CSS named colors as `static constexpr Color` values (the single source of
/// truth for their hex), plus a case-insensitive name lookup via `parse`.
struct CssColors {
    static constexpr Color transparent = Color::from_rgba_int(0x00000000);
    static constexpr Color aliceblue = Color::from_rgb_int(0xF0F8FF);
    static constexpr Color antiquewhite = Color::from_rgb_int(0xFAEBD7);
    static constexpr Color aqua = Color::from_rgb_int(0x00FFFF);
    static constexpr Color aquamarine = Color::from_rgb_int(0x7FFFD4);
    static constexpr Color azure = Color::from_rgb_int(0xF0FFFF);
    static constexpr Color beige = Color::from_rgb_int(0xF5F5DC);
    static constexpr Color bisque = Color::from_rgb_int(0xFFE4C4);
    static constexpr Color black = Color::from_rgb_int(0x000000);
    static constexpr Color blanchedalmond = Color::from_rgb_int(0xFFEBCD);
    static constexpr Color blue = Color::from_rgb_int(0x0000FF);
    static constexpr Color blueviolet = Color::from_rgb_int(0x8A2BE2);
    static constexpr Color brown = Color::from_rgb_int(0xA52A2A);
    static constexpr Color burlywood = Color::from_rgb_int(0xDEB887);
    static constexpr Color cadetblue = Color::from_rgb_int(0x5F9EA0);
    static constexpr Color chartreuse = Color::from_rgb_int(0x7FFF00);
    static constexpr Color chocolate = Color::from_rgb_int(0xD2691E);
    static constexpr Color coral = Color::from_rgb_int(0xFF7F50);
    static constexpr Color cornflowerblue = Color::from_rgb_int(0x6495ED);
    static constexpr Color cornsilk = Color::from_rgb_int(0xFFF8DC);
    static constexpr Color crimson = Color::from_rgb_int(0xDC143C);
    static constexpr Color cyan = Color::from_rgb_int(0x00FFFF);
    static constexpr Color darkblue = Color::from_rgb_int(0x00008B);
    static constexpr Color darkcyan = Color::from_rgb_int(0x008B8B);
    static constexpr Color darkgoldenrod = Color::from_rgb_int(0xB8860B);
    static constexpr Color darkgray = Color::from_rgb_int(0xA9A9A9);
    static constexpr Color darkgreen = Color::from_rgb_int(0x006400);
    static constexpr Color darkgrey = Color::from_rgb_int(0xA9A9A9);
    static constexpr Color darkkhaki = Color::from_rgb_int(0xBDB76B);
    static constexpr Color darkmagenta = Color::from_rgb_int(0x8B008B);
    static constexpr Color darkolivegreen = Color::from_rgb_int(0x556B2F);
    static constexpr Color darkorange = Color::from_rgb_int(0xFF8C00);
    static constexpr Color darkorchid = Color::from_rgb_int(0x9932CC);
    static constexpr Color darkred = Color::from_rgb_int(0x8B0000);
    static constexpr Color darksalmon = Color::from_rgb_int(0xE9967A);
    static constexpr Color darkseagreen = Color::from_rgb_int(0x8FBC8F);
    static constexpr Color darkslateblue = Color::from_rgb_int(0x483D8B);
    static constexpr Color darkslategray = Color::from_rgb_int(0x2F4F4F);
    static constexpr Color darkslategrey = Color::from_rgb_int(0x2F4F4F);
    static constexpr Color darkturquoise = Color::from_rgb_int(0x00CED1);
    static constexpr Color darkviolet = Color::from_rgb_int(0x9400D3);
    static constexpr Color deeppink = Color::from_rgb_int(0xFF1493);
    static constexpr Color deepskyblue = Color::from_rgb_int(0x00BFFF);
    static constexpr Color dimgray = Color::from_rgb_int(0x696969);
    static constexpr Color dimgrey = Color::from_rgb_int(0x696969);
    static constexpr Color dodgerblue = Color::from_rgb_int(0x1E90FF);
    static constexpr Color firebrick = Color::from_rgb_int(0xB22222);
    static constexpr Color floralwhite = Color::from_rgb_int(0xFFFAF0);
    static constexpr Color forestgreen = Color::from_rgb_int(0x228B22);
    static constexpr Color fuchsia = Color::from_rgb_int(0xFF00FF);
    static constexpr Color gainsboro = Color::from_rgb_int(0xDCDCDC);
    static constexpr Color ghostwhite = Color::from_rgb_int(0xF8F8FF);
    static constexpr Color gold = Color::from_rgb_int(0xFFD700);
    static constexpr Color goldenrod = Color::from_rgb_int(0xDAA520);
    static constexpr Color gray = Color::from_rgb_int(0x808080);
    static constexpr Color green = Color::from_rgb_int(0x008000);
    static constexpr Color greenyellow = Color::from_rgb_int(0xADFF2F);
    static constexpr Color grey = Color::from_rgb_int(0x808080);
    static constexpr Color honeydew = Color::from_rgb_int(0xF0FFF0);
    static constexpr Color hotpink = Color::from_rgb_int(0xFF69B4);
    static constexpr Color indianred = Color::from_rgb_int(0xCD5C5C);
    static constexpr Color indigo = Color::from_rgb_int(0x4B0082);
    static constexpr Color ivory = Color::from_rgb_int(0xFFFFF0);
    static constexpr Color khaki = Color::from_rgb_int(0xF0E68C);
    static constexpr Color lavender = Color::from_rgb_int(0xE6E6FA);
    static constexpr Color lavenderblush = Color::from_rgb_int(0xFFF0F5);
    static constexpr Color lawngreen = Color::from_rgb_int(0x7CFC00);
    static constexpr Color lemonchiffon = Color::from_rgb_int(0xFFFACD);
    static constexpr Color lightblue = Color::from_rgb_int(0xADD8E6);
    static constexpr Color lightcoral = Color::from_rgb_int(0xF08080);
    static constexpr Color lightcyan = Color::from_rgb_int(0xE0FFFF);
    static constexpr Color lightgoldenrodyellow = Color::from_rgb_int(0xFAFAD2);
    static constexpr Color lightgray = Color::from_rgb_int(0xD3D3D3);
    static constexpr Color lightgreen = Color::from_rgb_int(0x90EE90);
    static constexpr Color lightgrey = Color::from_rgb_int(0xD3D3D3);
    static constexpr Color lightpink = Color::from_rgb_int(0xFFB6C1);
    static constexpr Color lightsalmon = Color::from_rgb_int(0xFFA07A);
    static constexpr Color lightseagreen = Color::from_rgb_int(0x20B2AA);
    static constexpr Color lightskyblue = Color::from_rgb_int(0x87CEFA);
    static constexpr Color lightslategray = Color::from_rgb_int(0x778899);
    static constexpr Color lightslategrey = Color::from_rgb_int(0x778899);
    static constexpr Color lightsteelblue = Color::from_rgb_int(0xB0C4DE);
    static constexpr Color lightyellow = Color::from_rgb_int(0xFFFFE0);
    static constexpr Color lime = Color::from_rgb_int(0x00FF00);
    static constexpr Color limegreen = Color::from_rgb_int(0x32CD32);
    static constexpr Color linen = Color::from_rgb_int(0xFAF0E6);
    static constexpr Color magenta = Color::from_rgb_int(0xFF00FF);
    static constexpr Color maroon = Color::from_rgb_int(0x800000);
    static constexpr Color mediumaquamarine = Color::from_rgb_int(0x66CDAA);
    static constexpr Color mediumblue = Color::from_rgb_int(0x0000CD);
    static constexpr Color mediumorchid = Color::from_rgb_int(0xBA55D3);
    static constexpr Color mediumpurple = Color::from_rgb_int(0x9370DB);
    static constexpr Color mediumseagreen = Color::from_rgb_int(0x3CB371);
    static constexpr Color mediumslateblue = Color::from_rgb_int(0x7B68EE);
    static constexpr Color mediumspringgreen = Color::from_rgb_int(0x00FA9A);
    static constexpr Color mediumturquoise = Color::from_rgb_int(0x48D1CC);
    static constexpr Color mediumvioletred = Color::from_rgb_int(0xC71585);
    static constexpr Color midnightblue = Color::from_rgb_int(0x191970);
    static constexpr Color mintcream = Color::from_rgb_int(0xF5FFFA);
    static constexpr Color mistyrose = Color::from_rgb_int(0xFFE4E1);
    static constexpr Color moccasin = Color::from_rgb_int(0xFFE4B5);
    static constexpr Color navajowhite = Color::from_rgb_int(0xFFDEAD);
    static constexpr Color navy = Color::from_rgb_int(0x000080);
    static constexpr Color oldlace = Color::from_rgb_int(0xFDF5E6);
    static constexpr Color olive = Color::from_rgb_int(0x808000);
    static constexpr Color olivedrab = Color::from_rgb_int(0x6B8E23);
    static constexpr Color orange = Color::from_rgb_int(0xFFA500);
    static constexpr Color orangered = Color::from_rgb_int(0xFF4500);
    static constexpr Color orchid = Color::from_rgb_int(0xDA70D6);
    static constexpr Color palegoldenrod = Color::from_rgb_int(0xEEE8AA);
    static constexpr Color palegreen = Color::from_rgb_int(0x98FB98);
    static constexpr Color paleturquoise = Color::from_rgb_int(0xAFEEEE);
    static constexpr Color palevioletred = Color::from_rgb_int(0xDB7093);
    static constexpr Color papayawhip = Color::from_rgb_int(0xFFEFD5);
    static constexpr Color peachpuff = Color::from_rgb_int(0xFFDAB9);
    static constexpr Color peru = Color::from_rgb_int(0xCD853F);
    static constexpr Color pink = Color::from_rgb_int(0xFFC0CB);
    static constexpr Color plum = Color::from_rgb_int(0xDDA0DD);
    static constexpr Color powderblue = Color::from_rgb_int(0xB0E0E6);
    static constexpr Color purple = Color::from_rgb_int(0x800080);
    static constexpr Color rebeccapurple = Color::from_rgb_int(0x663399);
    static constexpr Color red = Color::from_rgb_int(0xFF0000);
    static constexpr Color rosybrown = Color::from_rgb_int(0xBC8F8F);
    static constexpr Color royalblue = Color::from_rgb_int(0x4169E1);
    static constexpr Color saddlebrown = Color::from_rgb_int(0x8B4513);
    static constexpr Color salmon = Color::from_rgb_int(0xFA8072);
    static constexpr Color sandybrown = Color::from_rgb_int(0xF4A460);
    static constexpr Color seagreen = Color::from_rgb_int(0x2E8B57);
    static constexpr Color seashell = Color::from_rgb_int(0xFFF5EE);
    static constexpr Color sienna = Color::from_rgb_int(0xA0522D);
    static constexpr Color silver = Color::from_rgb_int(0xC0C0C0);
    static constexpr Color skyblue = Color::from_rgb_int(0x87CEEB);
    static constexpr Color slateblue = Color::from_rgb_int(0x6A5ACD);
    static constexpr Color slategray = Color::from_rgb_int(0x708090);
    static constexpr Color slategrey = Color::from_rgb_int(0x708090);
    static constexpr Color snow = Color::from_rgb_int(0xFFFAFA);
    static constexpr Color springgreen = Color::from_rgb_int(0x00FF7F);
    static constexpr Color steelblue = Color::from_rgb_int(0x4682B4);
    static constexpr Color tan = Color::from_rgb_int(0xD2B48C);
    static constexpr Color teal = Color::from_rgb_int(0x008080);
    static constexpr Color thistle = Color::from_rgb_int(0xD8BFD8);
    static constexpr Color tomato = Color::from_rgb_int(0xFF6347);
    static constexpr Color turquoise = Color::from_rgb_int(0x40E0D0);
    static constexpr Color violet = Color::from_rgb_int(0xEE82EE);
    static constexpr Color wheat = Color::from_rgb_int(0xF5DEB3);
    static constexpr Color white = Color::from_rgb_int(0xFFFFFF);
    static constexpr Color whitesmoke = Color::from_rgb_int(0xF5F5F5);
    static constexpr Color yellow = Color::from_rgb_int(0xFFFF00);
    static constexpr Color yellowgreen = Color::from_rgb_int(0x9ACD32);

    /// Resolve a CSS color name (case-insensitive) to its `Color`, or
    /// `nullopt` if the name is unknown. The lookup table references the
    /// members above, so color values live in exactly one place.
    [[nodiscard]] static constexpr std::optional<Color> parse(const std::string_view name) {
        struct Named {
            std::string_view name;
            const Color* value;
        };
        constexpr std::array<Named, 149> table = {{
            {"transparent", &transparent},
            {"aliceblue", &aliceblue},
            {"antiquewhite", &antiquewhite},
            {"aqua", &aqua},
            {"aquamarine", &aquamarine},
            {"azure", &azure},
            {"beige", &beige},
            {"bisque", &bisque},
            {"black", &black},
            {"blanchedalmond", &blanchedalmond},
            {"blue", &blue},
            {"blueviolet", &blueviolet},
            {"brown", &brown},
            {"burlywood", &burlywood},
            {"cadetblue", &cadetblue},
            {"chartreuse", &chartreuse},
            {"chocolate", &chocolate},
            {"coral", &coral},
            {"cornflowerblue", &cornflowerblue},
            {"cornsilk", &cornsilk},
            {"crimson", &crimson},
            {"cyan", &cyan},
            {"darkblue", &darkblue},
            {"darkcyan", &darkcyan},
            {"darkgoldenrod", &darkgoldenrod},
            {"darkgray", &darkgray},
            {"darkgreen", &darkgreen},
            {"darkgrey", &darkgrey},
            {"darkkhaki", &darkkhaki},
            {"darkmagenta", &darkmagenta},
            {"darkolivegreen", &darkolivegreen},
            {"darkorange", &darkorange},
            {"darkorchid", &darkorchid},
            {"darkred", &darkred},
            {"darksalmon", &darksalmon},
            {"darkseagreen", &darkseagreen},
            {"darkslateblue", &darkslateblue},
            {"darkslategray", &darkslategray},
            {"darkslategrey", &darkslategrey},
            {"darkturquoise", &darkturquoise},
            {"darkviolet", &darkviolet},
            {"deeppink", &deeppink},
            {"deepskyblue", &deepskyblue},
            {"dimgray", &dimgray},
            {"dimgrey", &dimgrey},
            {"dodgerblue", &dodgerblue},
            {"firebrick", &firebrick},
            {"floralwhite", &floralwhite},
            {"forestgreen", &forestgreen},
            {"fuchsia", &fuchsia},
            {"gainsboro", &gainsboro},
            {"ghostwhite", &ghostwhite},
            {"gold", &gold},
            {"goldenrod", &goldenrod},
            {"gray", &gray},
            {"green", &green},
            {"greenyellow", &greenyellow},
            {"grey", &grey},
            {"honeydew", &honeydew},
            {"hotpink", &hotpink},
            {"indianred", &indianred},
            {"indigo", &indigo},
            {"ivory", &ivory},
            {"khaki", &khaki},
            {"lavender", &lavender},
            {"lavenderblush", &lavenderblush},
            {"lawngreen", &lawngreen},
            {"lemonchiffon", &lemonchiffon},
            {"lightblue", &lightblue},
            {"lightcoral", &lightcoral},
            {"lightcyan", &lightcyan},
            {"lightgoldenrodyellow", &lightgoldenrodyellow},
            {"lightgray", &lightgray},
            {"lightgreen", &lightgreen},
            {"lightgrey", &lightgrey},
            {"lightpink", &lightpink},
            {"lightsalmon", &lightsalmon},
            {"lightseagreen", &lightseagreen},
            {"lightskyblue", &lightskyblue},
            {"lightslategray", &lightslategray},
            {"lightslategrey", &lightslategrey},
            {"lightsteelblue", &lightsteelblue},
            {"lightyellow", &lightyellow},
            {"lime", &lime},
            {"limegreen", &limegreen},
            {"linen", &linen},
            {"magenta", &magenta},
            {"maroon", &maroon},
            {"mediumaquamarine", &mediumaquamarine},
            {"mediumblue", &mediumblue},
            {"mediumorchid", &mediumorchid},
            {"mediumpurple", &mediumpurple},
            {"mediumseagreen", &mediumseagreen},
            {"mediumslateblue", &mediumslateblue},
            {"mediumspringgreen", &mediumspringgreen},
            {"mediumturquoise", &mediumturquoise},
            {"mediumvioletred", &mediumvioletred},
            {"midnightblue", &midnightblue},
            {"mintcream", &mintcream},
            {"mistyrose", &mistyrose},
            {"moccasin", &moccasin},
            {"navajowhite", &navajowhite},
            {"navy", &navy},
            {"oldlace", &oldlace},
            {"olive", &olive},
            {"olivedrab", &olivedrab},
            {"orange", &orange},
            {"orangered", &orangered},
            {"orchid", &orchid},
            {"palegoldenrod", &palegoldenrod},
            {"palegreen", &palegreen},
            {"paleturquoise", &paleturquoise},
            {"palevioletred", &palevioletred},
            {"papayawhip", &papayawhip},
            {"peachpuff", &peachpuff},
            {"peru", &peru},
            {"pink", &pink},
            {"plum", &plum},
            {"powderblue", &powderblue},
            {"purple", &purple},
            {"rebeccapurple", &rebeccapurple},
            {"red", &red},
            {"rosybrown", &rosybrown},
            {"royalblue", &royalblue},
            {"saddlebrown", &saddlebrown},
            {"salmon", &salmon},
            {"sandybrown", &sandybrown},
            {"seagreen", &seagreen},
            {"seashell", &seashell},
            {"sienna", &sienna},
            {"silver", &silver},
            {"skyblue", &skyblue},
            {"slateblue", &slateblue},
            {"slategray", &slategray},
            {"slategrey", &slategrey},
            {"snow", &snow},
            {"springgreen", &springgreen},
            {"steelblue", &steelblue},
            {"tan", &tan},
            {"teal", &teal},
            {"thistle", &thistle},
            {"tomato", &tomato},
            {"turquoise", &turquoise},
            {"violet", &violet},
            {"wheat", &wheat},
            {"white", &white},
            {"whitesmoke", &whitesmoke},
            {"yellow", &yellow},
            {"yellowgreen", &yellowgreen},
        }};
        const std::string_view trimmed = detail::trim(name);
        for (const auto& [n, value] : table) {
            if (detail::eq_ci(trimmed, n)) {
                return *value;
            }
        }
        return std::nullopt;
    }
};

// ---------------------------------------------------------------------------
// Material UI palette (the 2014 Material Design colors). Each family is a small
// struct of static constexpr Color shades — s50..s900 plus the a100/a200/a400/
// a700 accents — with `operator[](int)` for the numeric shades and `accent(int)`
// for the A-shades. MuiColors exposes one instance per family (e.g. `red`,
// usable as `red[500]`) alongside flat aliases (`red_500`, `red_a100`). The
// families are generated from a macro so the 19×14 hex table is written once.
// ---------------------------------------------------------------------------

#define COMMONS_MUI_FAMILY(Type,                                                                   \
                           inst,                                                                   \
                           H50,                                                                    \
                           H100,                                                                   \
                           H200,                                                                   \
                           H300,                                                                   \
                           H400,                                                                   \
                           H500,                                                                   \
                           H600,                                                                   \
                           H700,                                                                   \
                           H800,                                                                   \
                           H900,                                                                   \
                           HA100,                                                                  \
                           HA200,                                                                  \
                           HA400,                                                                  \
                           HA700)                                                                  \
    struct Type {                                                                                  \
        static constexpr Color s50 = Color::from_rgb_int(H50);                                     \
        static constexpr Color s100 = Color::from_rgb_int(H100);                                   \
        static constexpr Color s200 = Color::from_rgb_int(H200);                                   \
        static constexpr Color s300 = Color::from_rgb_int(H300);                                   \
        static constexpr Color s400 = Color::from_rgb_int(H400);                                   \
        static constexpr Color s500 = Color::from_rgb_int(H500);                                   \
        static constexpr Color s600 = Color::from_rgb_int(H600);                                   \
        static constexpr Color s700 = Color::from_rgb_int(H700);                                   \
        static constexpr Color s800 = Color::from_rgb_int(H800);                                   \
        static constexpr Color s900 = Color::from_rgb_int(H900);                                   \
        static constexpr Color a100 = Color::from_rgb_int(HA100);                                  \
        static constexpr Color a200 = Color::from_rgb_int(HA200);                                  \
        static constexpr Color a400 = Color::from_rgb_int(HA400);                                  \
        static constexpr Color a700 = Color::from_rgb_int(HA700);                                  \
        constexpr Color operator[](int shade) const {                                              \
            switch (shade) {                                                                       \
            case 50:                                                                               \
                return s50;                                                                        \
            case 100:                                                                              \
                return s100;                                                                       \
            case 200:                                                                              \
                return s200;                                                                       \
            case 300:                                                                              \
                return s300;                                                                       \
            case 400:                                                                              \
                return s400;                                                                       \
            case 500:                                                                              \
                return s500;                                                                       \
            case 600:                                                                              \
                return s600;                                                                       \
            case 700:                                                                              \
                return s700;                                                                       \
            case 800:                                                                              \
                return s800;                                                                       \
            case 900:                                                                              \
                return s900;                                                                       \
            default:                                                                               \
                throw std::out_of_range("commons: MUI shade out of range");                        \
            }                                                                                      \
        }                                                                                          \
        constexpr Color accent(int shade) const {                                                  \
            switch (shade) {                                                                       \
            case 100:                                                                              \
                return a100;                                                                       \
            case 200:                                                                              \
                return a200;                                                                       \
            case 400:                                                                              \
                return a400;                                                                       \
            case 700:                                                                              \
                return a700;                                                                       \
            default:                                                                               \
                throw std::out_of_range("commons: MUI accent shade out of range");                 \
            }                                                                                      \
        }                                                                                          \
    };                                                                                             \
    static constexpr Type inst{};                                                                  \
    static constexpr Color inst##_50 = Type::s50;                                                  \
    static constexpr Color inst##_100 = Type::s100;                                                \
    static constexpr Color inst##_200 = Type::s200;                                                \
    static constexpr Color inst##_300 = Type::s300;                                                \
    static constexpr Color inst##_400 = Type::s400;                                                \
    static constexpr Color inst##_500 = Type::s500;                                                \
    static constexpr Color inst##_600 = Type::s600;                                                \
    static constexpr Color inst##_700 = Type::s700;                                                \
    static constexpr Color inst##_800 = Type::s800;                                                \
    static constexpr Color inst##_900 = Type::s900;                                                \
    static constexpr Color inst##_a100 = Type::a100;                                               \
    static constexpr Color inst##_a200 = Type::a200;                                               \
    static constexpr Color inst##_a400 = Type::a400;                                               \
    static constexpr Color inst##_a700 = Type::a700

/// The Material UI palette (2014 Material Design). One nested family struct per
/// color (e.g. `MuiColors::Red`) with `s50`..`s900` shades and `a100`/`a200`/
/// `a400`/`a700` accents, plus flat aliases such as `red_500` / `red_a100`.
struct MuiColors {
    // clang-format off
    COMMONS_MUI_FAMILY(Red, red,
        0xffebee, 0xffcdd2, 0xef9a9a, 0xe57373, 0xef5350, 0xf44336, 0xe53935, 0xd32f2f, 0xc62828,
        0xb71c1c, 0xff8a80, 0xff5252, 0xff1744, 0xd50000);
    COMMONS_MUI_FAMILY(Pink, pink,
        0xfce4ec, 0xf8bbd0, 0xf48fb1, 0xf06292, 0xec407a, 0xe91e63, 0xd81b60, 0xc2185b, 0xad1457,
        0x880e4f, 0xff80ab, 0xff4081, 0xf50057, 0xc51162);
    COMMONS_MUI_FAMILY(Purple, purple,
        0xf3e5f5, 0xe1bee7, 0xce93d8, 0xba68c8, 0xab47bc, 0x9c27b0, 0x8e24aa, 0x7b1fa2, 0x6a1b9a,
        0x4a148c, 0xea80fc, 0xe040fb, 0xd500f9, 0xaa00ff);
    COMMONS_MUI_FAMILY(DeepPurple, deep_purple,
        0xede7f6, 0xd1c4e9, 0xb39ddb, 0x9575cd, 0x7e57c2, 0x673ab7, 0x5e35b1, 0x512da8, 0x4527a0,
        0x311b92, 0xb388ff, 0x7c4dff, 0x651fff, 0x6200ea);
    COMMONS_MUI_FAMILY(Indigo, indigo,
        0xe8eaf6, 0xc5cae9, 0x9fa8da, 0x7986cb, 0x5c6bc0, 0x3f51b5, 0x3949ab, 0x303f9f, 0x283593,
        0x1a237e, 0x8c9eff, 0x536dfe, 0x3d5afe, 0x304ffe);
    COMMONS_MUI_FAMILY(Blue, blue,
        0xe3f2fd, 0xbbdefb, 0x90caf9, 0x64b5f6, 0x42a5f5, 0x2196f3, 0x1e88e5, 0x1976d2, 0x1565c0,
        0x0d47a1, 0x82b1ff, 0x448aff, 0x2979ff, 0x2962ff);
    COMMONS_MUI_FAMILY(LightBlue, light_blue,
        0xe1f5fe, 0xb3e5fc, 0x81d4fa, 0x4fc3f7, 0x29b6f6, 0x03a9f4, 0x039be5, 0x0288d1, 0x0277bd,
        0x01579b, 0x80d8ff, 0x40c4ff, 0x00b0ff, 0x0091ea);
    COMMONS_MUI_FAMILY(Cyan, cyan,
        0xe0f7fa, 0xb2ebf2, 0x80deea, 0x4dd0e1, 0x26c6da, 0x00bcd4, 0x00acc1, 0x0097a7, 0x00838f,
        0x006064, 0x84ffff, 0x18ffff, 0x00e5ff, 0x00b8d4);
    COMMONS_MUI_FAMILY(Teal, teal,
        0xe0f2f1, 0xb2dfdb, 0x80cbc4, 0x4db6ac, 0x26a69a, 0x009688, 0x00897b, 0x00796b, 0x00695c,
        0x004d40, 0xa7ffeb, 0x64ffda, 0x1de9b6, 0x00bfa5);
    COMMONS_MUI_FAMILY(Green, green,
        0xe8f5e9, 0xc8e6c9, 0xa5d6a7, 0x81c784, 0x66bb6a, 0x4caf50, 0x43a047, 0x388e3c, 0x2e7d32,
        0x1b5e20, 0xb9f6ca, 0x69f0ae, 0x00e676, 0x00c853);
    COMMONS_MUI_FAMILY(LightGreen, light_green,
        0xf1f8e9, 0xdcedc8, 0xc5e1a5, 0xaed581, 0x9ccc65, 0x8bc34a, 0x7cb342, 0x689f38, 0x558b2f,
        0x33691e, 0xccff90, 0xb2ff59, 0x76ff03, 0x64dd17);
    COMMONS_MUI_FAMILY(Lime, lime,
        0xf9fbe7, 0xf0f4c3, 0xe6ee9c, 0xdce775, 0xd4e157, 0xcddc39, 0xc0ca33, 0xafb42b, 0x9e9d24,
        0x827717, 0xf4ff81, 0xeeff41, 0xc6ff00, 0xaeea00);
    COMMONS_MUI_FAMILY(Yellow, yellow,
        0xfffde7, 0xfff9c4, 0xfff59d, 0xfff176, 0xffee58, 0xffeb3b, 0xfdd835, 0xfbc02d, 0xf9a825,
        0xf57f17, 0xffff8d, 0xffff00, 0xffea00, 0xffd600);
    COMMONS_MUI_FAMILY(Amber, amber,
        0xfff8e1, 0xffecb3, 0xffe082, 0xffd54f, 0xffca28, 0xffc107, 0xffb300, 0xffa000, 0xff8f00,
        0xff6f00, 0xffe57f, 0xffd740, 0xffc400, 0xffab00);
    COMMONS_MUI_FAMILY(Orange, orange,
        0xfff3e0, 0xffe0b2, 0xffcc80, 0xffb74d, 0xffa726, 0xff9800, 0xfb8c00, 0xf57c00, 0xef6c00,
        0xe65100, 0xffd180, 0xffab40, 0xff9100, 0xff6d00);
    COMMONS_MUI_FAMILY(DeepOrange, deep_orange,
        0xfbe9e7, 0xffccbc, 0xffab91, 0xff8a65, 0xff7043, 0xff5722, 0xf4511e, 0xe64a19, 0xd84315,
        0xbf360c, 0xff9e80, 0xff6e40, 0xff3d00, 0xdd2c00);
    COMMONS_MUI_FAMILY(Brown, brown,
        0xefebe9, 0xd7ccc8, 0xbcaaa4, 0xa1887f, 0x8d6e63, 0x795548, 0x6d4c41, 0x5d4037, 0x4e342e,
        0x3e2723, 0xd7ccc8, 0xbcaaa4, 0x8d6e63, 0x5d4037);
    COMMONS_MUI_FAMILY(Grey, grey,
        0xfafafa, 0xf5f5f5, 0xeeeeee, 0xe0e0e0, 0xbdbdbd, 0x9e9e9e, 0x757575, 0x616161, 0x424242,
        0x212121, 0xf5f5f5, 0xeeeeee, 0xbdbdbd, 0x616161);
    COMMONS_MUI_FAMILY(BlueGrey, blue_grey,
        0xeceff1, 0xcfd8dc, 0xb0bec5, 0x90a4ae, 0x78909c, 0x607d8b, 0x546e7a, 0x455a64, 0x37474f,
        0x263238, 0xcfd8dc, 0xb0bec5, 0x78909c, 0x455a64);
    // clang-format on
};

#undef COMMONS_MUI_FAMILY

/// Top-level collection of named-color sets: the CSS named colors
/// (`comms::Colors::css::red`) and the Material UI palette
/// (`comms::Colors::mui::red_500`). Kept as a nesting point for future palettes.
struct Colors {
    using css = CssColors;
    using mui = MuiColors;
};

// ---------------------------------------------------------------------------
// Color::parse — defined here, now that CssColors is complete.
// ---------------------------------------------------------------------------

constexpr std::optional<Color> Color::parse(std::string_view s) {
    using namespace detail;
    s = trim(s);
    if (s.empty()) {
        return std::nullopt;
    }

    if (const auto hex = parse_hex(s)) {
        return hex;
    }

    if (const usize lp = find_char(s, '('); lp != std::string_view::npos && s.back() == ')') {
        const std::string_view fn = trim(s.substr(0, lp));
        const std::string_view inner = s.substr(lp + 1, s.size() - lp - 2);
        const bool is_rgb = eq_ci(fn, "rgb") || eq_ci(fn, "rgba");
        if (const bool is_hsl = eq_ci(fn, "hsl") || eq_ci(fn, "hsla"); is_rgb || is_hsl) {
            std::array<std::string_view, 4> toks{};
            const int n = split_components(inner, toks);
            if (n != 3 && n != 4) {
                return std::nullopt;
            }
            std::array<f64, 3> comp{};
            for (usize i = 0; i < 3; ++i) {
                const auto [value, percent, ok] = parse_num(toks[i]);
                if (!ok) {
                    return std::nullopt;
                }
                comp[i] = (is_rgb && percent) ? value / 100.0 * 255.0 : value;
            }
            f64 alpha = 1.0;
            if (n == 4) {
                const auto [value, percent, ok] = parse_num(toks[3]);
                if (!ok) {
                    return std::nullopt;
                }
                alpha = percent ? value / 100.0 : value;
            }
            if (is_rgb) {
                return Color(round_channel(comp[0]),
                             round_channel(comp[1]),
                             round_channel(comp[2]),
                             round_u8(alpha));
            }
            return from_hsl(Hsl{wrap_hue(comp[0]),
                                clamp_(comp[1] / 100.0, 0.0, 1.0),
                                clamp_(comp[2] / 100.0, 0.0, 1.0),
                                alpha});
        }
        return std::nullopt;
    }

    return CssColors::parse(s);
}

// ---------------------------------------------------------------------------
// Text output: to_string + std::ostream insertion. (std::format support is the
// std::formatter specializations below, outside namespace comms.)
// ---------------------------------------------------------------------------

/// `Color` as its canonical hex string (`#RRGGBB`, or `#RRGGBBAA` when not
/// opaque).
[[nodiscard]] inline std::string to_string(const Color& c) {
    return c.to_hex_string();
}

/// `Hsl` as `hsl(h, s, l)` — or `hsla(h, s, l, a)` when alpha != 1 — with
/// components printed at full round-trip precision.
[[nodiscard]] inline std::string to_string(const Hsl& v) {
    return v.a == 1.0 ? std::format("hsl({}, {}, {})", v.h, v.s, v.l)
                      : std::format("hsla({}, {}, {}, {})", v.h, v.s, v.l, v.a);
}

/// `Hsv` as `hsv(h, s, v)` — or `hsva(h, s, v, a)` when alpha != 1.
[[nodiscard]] inline std::string to_string(const Hsv& v) {
    return v.a == 1.0 ? std::format("hsv({}, {}, {})", v.h, v.s, v.v)
                      : std::format("hsva({}, {}, {}, {})", v.h, v.s, v.v, v.a);
}

inline std::ostream& operator<<(std::ostream& os, const Color& c) {
    return os << to_string(c);
}

inline std::ostream& operator<<(std::ostream& os, const Hsl& v) {
    return os << to_string(v);
}

inline std::ostream& operator<<(std::ostream& os, const Hsv& v) {
    return os << to_string(v);
}

}  // namespace comms

// ---------------------------------------------------------------------------
// std::format support. Specializations live in namespace std (the primary
// templates are visible), like nlohmann's adl_serializer route in json.hpp.
// ---------------------------------------------------------------------------

/// Formats `comms::Color`. Spec: `h` lowercase hex (default), `H` uppercase
/// hex, `r` CSS `rgb(...)`.
template <>
struct std::formatter<comms::Color> {
    char mode = 'h';

    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            mode = *it++;
            if (mode != 'h' && mode != 'H' && mode != 'r') {
                throw std::format_error("commons: invalid Color format spec (use h, H, or r)");
            }
        }
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: invalid Color format spec");
        }
        return it;
    }

    auto format(const comms::Color& c, std::format_context& ctx) const {
        if (mode == 'r') {
            return std::format_to(
                ctx.out(), "{}", c.is_opaque() ? c.to_css_rgb_string() : c.to_css_rgba_string());
        }
        std::string s = c.to_hex_string();
        if (mode == 'H') {
            for (char& ch : s) {
                if (ch >= 'a' && ch <= 'f') {
                    ch = static_cast<char>(ch - ('a' - 'A'));
                }
            }
        }
        return std::format_to(ctx.out(), "{}", s);
    }
};

// These spec-less formatters don't read any member state, but `std::formatter`
// requires `parse`/`format` to be non-static members — so silence the
// convert-to-static suggestion here.
// NOLINTBEGIN(readability-convert-member-functions-to-static)

/// Formats `comms::Hsl` as `comms::to_string` does. Takes no format spec.
template <>
struct std::formatter<comms::Hsl> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: Hsl takes no format spec");
        }
        return it;
    }

    auto format(const comms::Hsl& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", comms::to_string(v));
    }
};

/// Formats `comms::Hsv` as `comms::to_string` does. Takes no format spec.
template <>
struct std::formatter<comms::Hsv> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: Hsv takes no format spec");
        }
        return it;
    }

    auto format(const comms::Hsv& v, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", comms::to_string(v));
    }
};

// NOLINTEND(readability-convert-member-functions-to-static)
