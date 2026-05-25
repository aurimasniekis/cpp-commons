#include <commons/literals.hpp>

#include <gtest/gtest.h>

namespace {

using comms::Color;
using comms::Icon;
using namespace comms::literals;

// -- compile-time guarantees -------------------------------------------------
// Both literals are consteval, so these are evaluated at compile time; a
// malformed literal would be a compile error rather than a runtime failure.

static_assert("#6366f1"_color == Color::rgb(0x63, 0x66, 0xF1));
static_assert("#fff"_color == Color::rgb(255, 255, 255));
static_assert("#6366f1ff"_color == Color::rgba(0x63, 0x66, 0xF1, 0xFF));
static_assert("6366f1"_color == Color::rgb(0x63, 0x66, 0xF1));  // leading '#' optional

static_assert("mdi:abacus"_icon == Icon::from("mdi:abacus"));
static_assert("mdi:abacus"_icon.set() == "mdi");
static_assert("mdi:abacus"_icon.name() == "abacus");

// -- the consteval result is usable at runtime -------------------------------

TEST(Literals, ColorLiteralUsableAtRuntime) {
    constexpr auto c = "#6366f1"_color;
    EXPECT_EQ(c, Color::rgb(0x63, 0x66, 0xF1));
}

TEST(Literals, IconLiteralUsableAtRuntime) {
    constexpr auto i = "mdi:cog"_icon;
    EXPECT_EQ(i.value(), "mdi:cog");
    EXPECT_EQ(i.set(), "mdi");
    EXPECT_EQ(i.name(), "cog");
}

}  // namespace
