#include <commons/fixed_string.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace {

using comms::FixedString;

TEST(FixedString, ConstructionAndView) {
    constexpr FixedString s{"order.created"};
    EXPECT_EQ(s.view(), "order.created");
    EXPECT_EQ(s.size(), 13u);
    EXPECT_FALSE(s.empty());
}

TEST(FixedString, ImplicitStringViewConversion) {
    constexpr FixedString s{"hello"};
    const std::string_view sv = s;  // implicit operator std::string_view
    EXPECT_EQ(sv, "hello");
}

TEST(FixedString, EmptyString) {
    constexpr FixedString s{""};
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_EQ(s.view(), "");
}

TEST(FixedString, DefaultConstructedIsEmpty) {
    constexpr FixedString<4> s;  // all '\0'
    EXPECT_EQ(s.view(), std::string_view("\0\0\0", 3));
}

TEST(FixedString, Equality) {
    constexpr FixedString a{"abc"};
    constexpr FixedString b{"abc"};
    constexpr FixedString c{"abd"};
    constexpr FixedString d{"abcd"};  // different N
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);  // differing length compares unequal
}

// Structural NTTP use: FixedString may parameterize a template directly.
template <FixedString Tag>
struct Tagged {
    [[nodiscard]] static constexpr std::string_view tag() {
        return Tag.view();
    }
};

TEST(FixedString, UsableAsNonTypeTemplateParameter) {
    EXPECT_EQ(Tagged<"event">::tag(), "event");
    static_assert(Tagged<"event">::tag() == std::string_view{"event"});
}

}  // namespace
