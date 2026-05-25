#include <commons/color.hpp>
#include <commons/literals.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace {

using comms::Color;
using comms::Colors;
using comms::CssColors;
using comms::Hsl;
using comms::Hsv;

// -- compile-time guarantees -------------------------------------------------

using namespace comms::literals;

static_assert("#6366f1"_color == Color::rgb(0x63, 0x66, 0xF1));
static_assert("#fff"_color == Color::rgb(255, 255, 255));
static_assert(Color::parse_hex("#fff") == Color::rgb(255, 255, 255));
static_assert(Color::parse_hex("#6366f1") == Color::rgb(0x63, 0x66, 0xF1));
static_assert(Colors::css::red == Color::rgb(255, 0, 0));
static_assert(CssColors::parse("red") == CssColors::red);
static_assert(Color::from_argb_int(0xFF6366F1) == Color::from_rgb_int(0x6366F1));
static_assert(Color() == Color(0, 0, 0, 255));

// -- construction & packed integers ------------------------------------------

TEST(Color, DefaultIsOpaqueBlack) {
    constexpr Color c;
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.a, 255);
    EXPECT_TRUE(c.is_opaque());
}

TEST(Color, PackedIntRoundTrips) {
    constexpr Color c = Color::rgba(0x63, 0x66, 0xF1, 0x80);
    EXPECT_EQ(Color::from_rgb_int(c.to_rgb_int()), Color::rgb(0x63, 0x66, 0xF1));
    EXPECT_EQ(Color::from_rgba_int(c.to_rgba_int()), c);
    EXPECT_EQ(Color::from_argb_int(c.to_argb_int()), c);

    EXPECT_EQ(Color::from_rgb_int(0x6366F1), Color::rgb(0x63, 0x66, 0xF1));
    EXPECT_EQ(Color::from_rgba_int(0x6366F180), Color::rgba(0x63, 0x66, 0xF1, 0x80));
    EXPECT_EQ(Color::from_argb_int(0x806366F1), Color::rgba(0x63, 0x66, 0xF1, 0x80));
    // ARGB-opaque equals the plain RGB form.
    EXPECT_EQ(Color::from_argb_int(0xFF6366F1), Color::from_rgb_int(0x6366F1));
}

// -- hex / css string output -------------------------------------------------

TEST(Color, HexStringOutput) {
    constexpr Color c = Color::rgb(0x63, 0x66, 0xF1);
    EXPECT_EQ(c.to_hex_string(), "#6366f1");
    EXPECT_EQ(c.to_hex_rgb_string(), "#6366f1");
    EXPECT_EQ(c.to_hex_rgba_string(), "#6366f1ff");

    constexpr Color t = c.with_alpha(0x80);
    EXPECT_EQ(t.to_hex_string(), "#6366f180");
    EXPECT_TRUE(t.has_alpha());
}

TEST(Color, CssStringOutput) {
    constexpr Color c = Color::rgb(99, 102, 241);
    EXPECT_EQ(c.to_css_rgb_string(), "rgb(99 102 241)");
    EXPECT_EQ(c.to_css_rgba_string(), "rgb(99 102 241 / 1.0)");
    EXPECT_EQ(c.with_alpha(128).to_css_rgba_string(), "rgb(99 102 241 / 0.5)");
    EXPECT_EQ(c.with_alpha(0).to_css_rgba_string(), "rgb(99 102 241 / 0.0)");
}

// -- parsing -----------------------------------------------------------------

TEST(Color, ParseHexForms) {
    EXPECT_EQ(Color::parse_hex("#abc"), Color::rgb(0xAA, 0xBB, 0xCC));
    EXPECT_EQ(Color::parse_hex("#abcd"), Color::rgba(0xAA, 0xBB, 0xCC, 0xDD));
    EXPECT_EQ(Color::parse_hex("#6366f1"), Color::rgb(0x63, 0x66, 0xF1));
    EXPECT_EQ(Color::parse_hex("#6366f180"), Color::rgba(0x63, 0x66, 0xF1, 0x80));
    EXPECT_EQ(Color::parse_hex("6366f1"), Color::rgb(0x63, 0x66, 0xF1));  // no '#'
    EXPECT_EQ(Color::parse_hex("#GGG"), std::nullopt);
    EXPECT_EQ(Color::parse_hex("#12345"), std::nullopt);
    EXPECT_EQ(Color::parse_hex(""), std::nullopt);
}

TEST(Color, ParseFunctionalRgb) {
    EXPECT_EQ(Color::parse("rgb(99, 102, 241)"), Color::rgb(99, 102, 241));
    EXPECT_EQ(Color::parse("rgb(99 102 241)"), Color::rgb(99, 102, 241));
    EXPECT_EQ(Color::parse("rgba(99, 102, 241, 0.5)"), Color::rgba(99, 102, 241, 128));
    EXPECT_EQ(Color::parse("rgb(99 102 241 / 0.5)"), Color::rgba(99, 102, 241, 128));
    EXPECT_EQ(Color::parse("rgb(100%, 0%, 0%)"), Color::rgb(255, 0, 0));
}

TEST(Color, ParseFunctionalHsl) {
    EXPECT_EQ(Color::parse("hsl(0, 100%, 50%)"), Color::rgb(255, 0, 0));
    EXPECT_EQ(Color::parse("hsl(120 100% 50%)"), Color::rgb(0, 255, 0));
    EXPECT_EQ(Color::parse("hsl(240, 100%, 50%)"), Color::rgb(0, 0, 255));
    EXPECT_EQ(Color::parse("hsla(0, 100%, 50%, 0.5)"), Color::rgba(255, 0, 0, 128));
}

TEST(Color, ParseNamedAndInvalid) {
    EXPECT_EQ(Color::parse("red"), Color::rgb(255, 0, 0));
    EXPECT_EQ(Color::parse("ReBeCcApUrPlE"), CssColors::rebeccapurple);  // case-insensitive
    EXPECT_EQ(Color::parse("  blue  "), Color::rgb(0, 0, 255));          // trimmed
    EXPECT_EQ(Color::parse("transparent"), Color::rgba(0, 0, 0, 0));
    EXPECT_EQ(Color::parse("notacolor"), std::nullopt);
    EXPECT_EQ(Color::parse("rgb(1, 2)"), std::nullopt);
    EXPECT_EQ(Color::parse(""), std::nullopt);
}

// -- HSL / HSV round trips ----------------------------------------------------

TEST(Color, HslRoundTripExactForPrimaries) {
    constexpr std::array<Color, 6> colors = {
        Color::rgb(255, 0, 0),
        Color::rgb(0, 255, 0),
        Color::rgb(0, 0, 255),
        Color::rgb(0, 0, 0),
        Color::rgb(255, 255, 255),
        Color::rgb(128, 128, 128),
    };
    for (const Color c : colors) {
        EXPECT_EQ(Color::from_hsl(c.to_hsl()), c);
        EXPECT_EQ(Color::from_hsv(c.to_hsv()), c);
    }
}

TEST(Color, HslRoundTripArbitraryWithinOne) {
    for (int r = 0; r < 256; r += 17) {
        for (int g = 0; g < 256; g += 23) {
            for (int b = 0; b < 256; b += 29) {
                const Color c = Color::rgb(static_cast<comms::u8>(r),
                                           static_cast<comms::u8>(g),
                                           static_cast<comms::u8>(b));
                const Color back = Color::from_hsl(c.to_hsl());
                EXPECT_LE(std::abs(back.r - c.r), 1);
                EXPECT_LE(std::abs(back.g - c.g), 1);
                EXPECT_LE(std::abs(back.b - c.b), 1);
            }
        }
    }
}

TEST(Color, HueAccessor) {
    EXPECT_NEAR(Color::rgb(255, 0, 0).hue(), 0.0, 1e-9);
    EXPECT_NEAR(Color::rgb(0, 255, 0).hue(), 120.0, 1e-9);
    EXPECT_NEAR(Color::rgb(0, 0, 255).hue(), 240.0, 1e-9);
}

// -- transforms / UI helpers --------------------------------------------------

TEST(Color, ChannelAndAlpha) {
    constexpr Color c = Color::rgb(10, 20, 30);
    EXPECT_EQ(c.with_red(99), Color::rgb(99, 20, 30));
    EXPECT_EQ(c.with_green(99), Color::rgb(10, 99, 30));
    EXPECT_EQ(c.with_blue(99), Color::rgb(10, 20, 99));
    EXPECT_EQ(c.with_alpha(128), Color::rgba(10, 20, 30, 128));
    EXPECT_EQ(c.fade(0.5), Color::rgba(10, 20, 30, 128));
    EXPECT_EQ(c.opacity(1.0), Color::rgba(10, 20, 30, 255));
}

TEST(Color, Invert) {
    EXPECT_EQ(Color::rgb(0, 0, 0).invert(), Color::rgb(255, 255, 255));
    EXPECT_EQ(Color::rgb(255, 128, 0).invert(), Color::rgb(0, 127, 255));
}

TEST(Color, Grayscale) {
    constexpr Color g = Color::rgb(255, 255, 255).grayscale();
    EXPECT_EQ(g, Color::rgb(255, 255, 255));
    constexpr Color k = Color::rgb(0, 0, 0).grayscale();
    EXPECT_EQ(k, Color::rgb(0, 0, 0));
    // Rec.601: pure red -> 0.299 * 255 ~= 76.
    EXPECT_EQ(Color::rgb(255, 0, 0).grayscale(), Color::rgb(76, 76, 76));
}

TEST(Color, Mix) {
    constexpr Color a = Color::rgb(0, 0, 0);
    constexpr Color b = Color::rgb(255, 255, 255);
    EXPECT_EQ(a.mix(b, 0.0), a);
    EXPECT_EQ(a.mix(b, 1.0), b);
    EXPECT_EQ(a.mix(b, 0.5), Color::rgb(128, 128, 128));
}

TEST(Color, BlendOver) {
    // Opaque source ignores the background.
    EXPECT_EQ(Color::rgb(255, 0, 0).blend_over(Color::rgb(0, 0, 255)), Color::rgb(255, 0, 0));
    // Fully transparent source yields the background.
    EXPECT_EQ(Color::rgba(255, 0, 0, 0).blend_over(Color::rgb(0, 0, 255)), Color::rgb(0, 0, 255));
    // Half-transparent red over blue.
    constexpr Color m = Color::rgba(255, 0, 0, 128).blend_over(Color::rgb(0, 0, 255));
    EXPECT_NEAR(m.r, 128, 1);
    EXPECT_EQ(m.g, 0);
    EXPECT_NEAR(m.b, 127, 1);
    EXPECT_EQ(m.a, 255);
}

TEST(Color, ComplementAndRotate) {
    constexpr Color red = Color::rgb(255, 0, 0);
    EXPECT_NEAR(red.complement().hue(), 180.0, 1e-6);
    EXPECT_NEAR(red.rotate_hue(120.0).hue(), 120.0, 1e-6);
}

TEST(Color, LightenDarken) {
    constexpr Color c = Color::rgb(100, 100, 100);
    EXPECT_GT(c.lighten(0.2).relative_luminance(), c.relative_luminance());
    EXPECT_LT(c.darken(0.2).relative_luminance(), c.relative_luminance());
}

// -- WCAG --------------------------------------------------------------------

TEST(Color, RelativeLuminance) {
    EXPECT_NEAR(Color::rgb(255, 255, 255).relative_luminance(), 1.0, 1e-6);
    EXPECT_NEAR(Color::rgb(0, 0, 0).relative_luminance(), 0.0, 1e-6);
}

TEST(Color, ContrastRatio) {
    constexpr Color black = Color::rgb(0, 0, 0);
    constexpr Color white = Color::rgb(255, 255, 255);
    EXPECT_NEAR(black.contrast_ratio(white), 21.0, 0.01);
    EXPECT_NEAR(white.contrast_ratio(black), 21.0, 0.01);  // symmetric
    EXPECT_NEAR(white.contrast_ratio(white), 1.0, 0.01);
}

TEST(Color, ReadableTextColor) {
    EXPECT_EQ(Color::rgb(255, 255, 255).readable_text_color(), Color::rgb(0, 0, 0));
    EXPECT_EQ(Color::rgb(0, 0, 0).readable_text_color(), Color::rgb(255, 255, 255));
    EXPECT_TRUE(Color::rgb(255, 255, 255).is_light());
    EXPECT_TRUE(Color::rgb(0, 0, 0).is_dark());
    EXPECT_TRUE(Color::rgb(255, 255, 255).has_readable_contrast(Color::rgb(0, 0, 0)));
    EXPECT_FALSE(Color::rgb(255, 255, 255).has_readable_contrast(Color::rgb(240, 240, 240)));
}

// -- palettes ----------------------------------------------------------------

TEST(Color, Triadic) {
    constexpr Color base = Color::rgb(255, 0, 0);  // hue 0
    constexpr auto tri = base.triadic();
    EXPECT_EQ(tri[0], base);
    EXPECT_NEAR(tri[1].hue(), 120.0, 1e-6);
    EXPECT_NEAR(tri[2].hue(), 240.0, 1e-6);
}

TEST(Color, Analogous) {
    constexpr Color base = Color::rgb(0, 255, 0);  // hue 120
    constexpr auto an = base.analogous();          // default ±30
    // ±1° tolerance absorbs the u8 round-trip quantization off the pure hues.
    EXPECT_NEAR(an[0].hue(), 90.0, 1.0);
    EXPECT_EQ(an[1], base);
    EXPECT_NEAR(an[2].hue(), 150.0, 1.0);
}

TEST(Color, SplitComplementary) {
    constexpr Color base = Color::rgb(255, 0, 0);  // hue 0
    constexpr auto sc = base.split_complementary();
    // ±1° tolerance absorbs the u8 round-trip quantization off the pure hues.
    EXPECT_NEAR(sc[0].hue(), 150.0, 1.0);
    EXPECT_NEAR(sc[1].hue(), 210.0, 1.0);
}

// -- CSS named colors --------------------------------------------------------

TEST(CssNamed, MembersMatchExpectedRgb) {
    EXPECT_EQ(CssColors::red, Color::rgb(255, 0, 0));
    EXPECT_EQ(CssColors::lime, Color::rgb(0, 255, 0));
    EXPECT_EQ(CssColors::blue, Color::rgb(0, 0, 255));
    EXPECT_EQ(CssColors::rebeccapurple, Color::rgb(0x66, 0x33, 0x99));
    EXPECT_EQ(CssColors::transparent, Color::rgba(0, 0, 0, 0));
    EXPECT_EQ(Colors::css::white, Color::rgb(255, 255, 255));
}

TEST(CssNamed, ParseResolvesNames) {
    EXPECT_EQ(CssColors::parse("red"), CssColors::red);
    EXPECT_EQ(CssColors::parse("CornflowerBlue"), CssColors::cornflowerblue);
    EXPECT_EQ(CssColors::parse("does-not-exist"), std::nullopt);
}

// -- Material UI palette ------------------------------------------------------

static_assert(Colors::mui::red_500 == Color::from_rgb_int(0xf44336));
static_assert(Colors::mui::red[500] == Colors::mui::red_500);
static_assert(Colors::mui::deep_purple_a700 == Color::from_rgb_int(0x6200ea));
static_assert(Colors::mui::blue_grey_500 == Color::from_rgb_int(0x607d8b));
static_assert(Colors::mui::red.accent(100) == Colors::mui::red_a100);

TEST(MuiPalette, FlatMembersMatchExpectedHex) {
    EXPECT_EQ(Colors::mui::red_50, Color::from_rgb_int(0xffebee));
    EXPECT_EQ(Colors::mui::red_500, Color::from_rgb_int(0xf44336));
    EXPECT_EQ(Colors::mui::red_900, Color::from_rgb_int(0xb71c1c));
    EXPECT_EQ(Colors::mui::red_a100, Color::from_rgb_int(0xff8a80));
    EXPECT_EQ(Colors::mui::red_a700, Color::from_rgb_int(0xd50000));
    EXPECT_EQ(Colors::mui::blue_grey_a200, Color::from_rgb_int(0xb0bec5));
    EXPECT_EQ(Colors::mui::deep_purple_500, Color::from_rgb_int(0x673ab7));
    EXPECT_EQ(Colors::mui::light_green_a400, Color::from_rgb_int(0x76ff03));
}

TEST(MuiPalette, IndexAndAccentAccessors) {
    EXPECT_EQ(Colors::mui::red[50], Colors::mui::red_50);
    EXPECT_EQ(Colors::mui::red[500], Colors::mui::red_500);
    EXPECT_EQ(Colors::mui::blue[700], Colors::mui::blue_700);
    EXPECT_EQ(Colors::mui::red.s500, Colors::mui::red_500);
    EXPECT_EQ(Colors::mui::red.accent(100), Colors::mui::red_a100);
    EXPECT_EQ(Colors::mui::red.accent(700), Colors::mui::red_a700);
}

TEST(MuiPalette, OutOfRangeShadeThrows) {
    EXPECT_THROW((void)Colors::mui::red[123], std::out_of_range);
    EXPECT_THROW((void)Colors::mui::red[1000], std::out_of_range);
    EXPECT_THROW((void)Colors::mui::red.accent(300), std::out_of_range);
}

// -- text output: to_string / ostream / std::format --------------------------

TEST(Color, ToStringAndOStream) {
    constexpr Color c = Color::rgb(0x63, 0x66, 0xf1);
    EXPECT_EQ(comms::to_string(c), "#6366f1");
    EXPECT_EQ(comms::to_string(c.with_alpha(0x80)), "#6366f180");

    std::ostringstream oss;
    oss << c;
    EXPECT_EQ(oss.str(), "#6366f1");
}

TEST(Color, StdFormat) {
    constexpr Color c = Color::rgb(0x63, 0x66, 0xf1);
    EXPECT_EQ(std::format("{}", c), "#6366f1");
    EXPECT_EQ(std::format("{:h}", c), "#6366f1");
    EXPECT_EQ(std::format("{:H}", c), "#6366F1");
    EXPECT_EQ(std::format("{:H}", c.with_alpha(0x80)), "#6366F180");
    EXPECT_EQ(std::format("{:r}", Color::rgb(99, 102, 241)), "rgb(99 102 241)");
    EXPECT_EQ(std::format("{:r}", Color::rgba(99, 102, 241, 128)), "rgb(99 102 241 / 0.5)");
}

TEST(HslHsv, ToStringOStreamAndFormat) {
    constexpr Hsl h{120.0, 0.5, 0.25, 1.0};
    EXPECT_EQ(comms::to_string(h), "hsl(120, 0.5, 0.25)");
    EXPECT_EQ(std::format("{}", h), "hsl(120, 0.5, 0.25)");
    EXPECT_EQ(comms::to_string(Hsl{120.0, 0.5, 0.25, 0.5}), "hsla(120, 0.5, 0.25, 0.5)");

    std::ostringstream oss;
    oss << Hsv{0.0, 1.0, 1.0, 1.0};
    EXPECT_EQ(oss.str(), "hsv(0, 1, 1)");
    EXPECT_EQ(std::format("{}", Hsv{0.0, 1.0, 1.0, 0.5}), "hsva(0, 1, 1, 0.5)");
}

// -- Hsl / Hsv structs --------------------------------------------------------

TEST(HslHsv, DefaultsAndEquality) {
    constexpr Hsl h;
    EXPECT_DOUBLE_EQ(h.h, 0.0);
    EXPECT_DOUBLE_EQ(h.a, 1.0);
    EXPECT_TRUE((Hsl{10, 0.5, 0.5, 1} == Hsl{10, 0.5, 0.5, 1}));
    EXPECT_FALSE((Hsv{10, 0.5, 0.5, 1} == Hsv{10, 0.5, 0.4, 1}));
}

}  // namespace
