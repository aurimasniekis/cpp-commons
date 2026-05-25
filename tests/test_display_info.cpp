#include <commons/display_info.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

using comms::Color;
using comms::DisplayInfo;
using comms::Icon;

// -- struct basics -----------------------------------------------------------

TEST(DisplayInfo, DefaultIsAllNullopt) {
    const DisplayInfo d;
    EXPECT_FALSE(d.name.has_value());
    EXPECT_FALSE(d.description.has_value());
    EXPECT_FALSE(d.icon.has_value());
    EXPECT_FALSE(d.color.has_value());
}

TEST(DisplayInfo, Equality) {
    const DisplayInfo full{
        .name = "Abacus",
        .description = "A counting frame",
        .icon = Icon::from("mdi:abacus"),
        .color = Color::rgb(0x63, 0x66, 0xF1),
    };
    EXPECT_EQ(full, full);

    DisplayInfo same = full;
    EXPECT_EQ(full, same);

    DisplayInfo partial{.name = "Abacus"};
    EXPECT_NE(full, partial);
    EXPECT_NE(partial, DisplayInfo{});

    same.color = Color::rgb(0, 0, 0);
    EXPECT_NE(full, same);
}

// -- intrusive member mechanism ----------------------------------------------

struct WithMember {
    [[maybe_unused]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "WithMember",
            .icon = Icon::from("mdi:cog"),
        };
        return info;
    }
};

static_assert(comms::Displayable<WithMember>);

TEST(DisplayInfo, MemberMechanism) {
    const auto& [name, description, icon, color] = comms::display_info<WithMember>();
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "WithMember");
    ASSERT_TRUE(icon.has_value());
    EXPECT_EQ(icon->value(), "mdi:cog");
    EXPECT_FALSE(description.has_value());
    EXPECT_FALSE(color.has_value());
}

// -- negative: a plain type is not Displayable -------------------------------

struct Plain {};

static_assert(!comms::Displayable<Plain>);

}  // namespace

// -- non-intrusive trait mechanism -------------------------------------------
// The specialization must sit in namespace comms, outside the anonymous one;
// WithTrait is therefore declared at file scope.

struct WithTrait {};

template <>
struct comms::HasDisplayInfo<WithTrait> {
    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "WithTrait",
            .color = Color::rgb(255, 0, 0),
        };
        return info;
    }
};

static_assert(comms::Displayable<WithTrait>);

namespace {

TEST(DisplayInfo, TraitMechanism) {
    const DisplayInfo& d = comms::display_info<WithTrait>();
    ASSERT_TRUE(d.name.has_value());
    EXPECT_EQ(*d.name, "WithTrait");
    ASSERT_TRUE(d.color.has_value());
    EXPECT_EQ(*d.color, Color::rgb(255, 0, 0));
}

}  // namespace
