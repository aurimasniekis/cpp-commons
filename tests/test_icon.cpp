#include <commons/icon.hpp>
#include <commons/icons.hpp>

#include <gtest/gtest.h>

#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using comms::Icon;
using comms::Icons;

// -- compile-time guarantees -------------------------------------------------

static_assert(Icon::from("mdi:abacus").value() == "mdi:abacus");
static_assert(Icon::from("mdi", "abacus").value() == "mdi:abacus");
static_assert(Icon::from("mdi:abacus").set() == "mdi");
static_assert(Icon::from("mdi:abacus").name() == "abacus");
static_assert(Icon::from("mdi", "abacus") == Icon::from("mdi:abacus"));
static_assert(Icons::mdi::abacus == Icon::from("mdi:abacus"));
static_assert(Icon{}.empty());
static_assert(!Icon::from("mdi:abacus").empty());

// -- factories ---------------------------------------------------------------

TEST(Icon, FromWholeValue) {
    constexpr Icon i = Icon::from("mdi:abacus");
    EXPECT_EQ(i.value(), "mdi:abacus");
    EXPECT_EQ(i.set(), "mdi");
    EXPECT_EQ(i.name(), "abacus");
}

TEST(Icon, FromSetAndNameJoinsWithColon) {
    constexpr Icon i = Icon::from("mdi", "ab-testing");
    EXPECT_EQ(i.value(), "mdi:ab-testing");
    EXPECT_EQ(i.set(), "mdi");
    EXPECT_EQ(i.name(), "ab-testing");
}

TEST(Icon, FromSetAndNameMatchesFromValue) {
    EXPECT_EQ(Icon::from("custom", "thing"), Icon::from("custom:thing"));
}

TEST(Icon, DefaultIsEmpty) {
    constexpr Icon i;
    EXPECT_TRUE(i.empty());
    EXPECT_EQ(i.value(), "");
    EXPECT_EQ(i.set(), "");
    EXPECT_EQ(i.name(), "");
}

// -- parse (non-throwing) ----------------------------------------------------

TEST(Icon, ParseValid) {
    constexpr auto i = Icon::parse("mdi:abacus");
    ASSERT_TRUE(i.has_value());
    EXPECT_EQ(i->value(), "mdi:abacus");
}

TEST(Icon, ParseRejectsMissingColon) {
    EXPECT_FALSE(Icon::parse("abacus").has_value());
}

TEST(Icon, ParseRejectsEmptySet) {
    EXPECT_FALSE(Icon::parse(":abacus").has_value());
}

TEST(Icon, ParseRejectsEmptyName) {
    EXPECT_FALSE(Icon::parse("mdi:").has_value());
}

TEST(Icon, ParseRejectsMultipleColons) {
    EXPECT_FALSE(Icon::parse("mdi:abacus:extra").has_value());
}

TEST(Icon, ParseRejectsOversize) {
    const std::string big = "mdi:" + std::string(Icon::capacity, 'x');
    EXPECT_FALSE(Icon::parse(big).has_value());
}

TEST(Icon, ParseAcceptsExactCapacity) {
    // "ab:" is 3 chars; pad the name so the whole value is exactly `capacity`.
    const std::string value = "ab:" + std::string(Icon::capacity - 3, 'x');
    ASSERT_EQ(value.size(), Icon::capacity);
    const auto i = Icon::parse(value);
    ASSERT_TRUE(i.has_value());
    EXPECT_EQ(i->value(), value);
}

// -- throwing factories ------------------------------------------------------

TEST(Icon, FromThrowsOnInvalid) {
    EXPECT_THROW((void)Icon::from("no-colon-here"), std::invalid_argument);
    EXPECT_THROW((void)Icon::from("mdi", ""), std::invalid_argument);
    EXPECT_THROW((void)Icon::from("", "abacus"), std::invalid_argument);
}

TEST(Icon, FromThrowsOnOversize) {
    const std::string big = "mdi:" + std::string(Icon::capacity, 'x');
    EXPECT_THROW((void)Icon::from(big), std::length_error);
    EXPECT_THROW((void)Icon::from("mdi", std::string(Icon::capacity, 'x')), std::length_error);
}

// -- equality ----------------------------------------------------------------

TEST(Icon, Equality) {
    EXPECT_EQ(Icon::from("mdi:abacus"), Icon::from("mdi:abacus"));
    EXPECT_NE(Icon::from("mdi:abacus"), Icon::from("mdi:account"));
    EXPECT_NE(Icon::from("mdi:abacus"), Icon::from("fa:abacus"));
}

// -- text output -------------------------------------------------------------

TEST(Icon, ToString) {
    EXPECT_EQ(Icon::from("mdi:abacus").to_string(), "mdi:abacus");
    EXPECT_EQ(comms::to_string(Icon::from("mdi:abacus")), "mdi:abacus");
}

TEST(Icon, OstreamInsertion) {
    std::ostringstream os;
    os << Icon::from("mdi:abacus");
    EXPECT_EQ(os.str(), "mdi:abacus");
}

TEST(Icon, StdFormat) {
    EXPECT_EQ(std::format("{}", Icon::from("mdi:abacus")), "mdi:abacus");
}

// -- predefined catalog ------------------------------------------------------

TEST(Icon, PredefinedMdi) {
    EXPECT_EQ(Icons::mdi::abacus.value(), "mdi:abacus");
    EXPECT_EQ(Icons::mdi::ab_testing.value(), "mdi:ab-testing");
    EXPECT_EQ(Icons::mdi::abacus.set(), "mdi");
}

TEST(Icon, PredefinedKeywordMangledMembers) {
    // `delete`, `export`, `switch` are C++ keywords, so the members carry a
    // trailing underscore but the icon name stays intact.
    EXPECT_EQ(Icons::mdi::delete_.value(), "mdi:delete");
    EXPECT_EQ(Icons::mdi::export_.value(), "mdi:export");
    EXPECT_EQ(Icons::mdi::switch_.value(), "mdi:switch");
    EXPECT_EQ(Icons::mdi::delete_.name(), "delete");
}

}  // namespace
