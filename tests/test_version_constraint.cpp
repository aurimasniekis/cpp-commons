#include <commons/semver.hpp>
#include <commons/version_constraint.hpp>

#include <gtest/gtest.h>

#include <format>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

using comms::SemVer;
using comms::VersionConstraint;

SemVer sv(const std::string_view s) {
    return SemVer::parse(s).value_or(SemVer{});
}

// -- any / exact -------------------------------------------------------------

TEST(VersionConstraint, AnyMatchesEverything) {
    const auto c = VersionConstraint::parse("*");
    EXPECT_TRUE(c.satisfies(sv("0.0.1")));
    EXPECT_TRUE(c.satisfies(sv("99.0.0")));
}

TEST(VersionConstraint, EmptyInputIsAny) {
    const auto c = VersionConstraint::parse("");
    EXPECT_TRUE(c.satisfies(sv("1.2.3")));
}

TEST(VersionConstraint, ExactMatch) {
    const auto c = VersionConstraint::parse("1.2.3");
    EXPECT_TRUE(c.satisfies(sv("1.2.3")));
    EXPECT_FALSE(c.satisfies(sv("1.2.4")));
    EXPECT_FALSE(c.satisfies(sv("1.2.3-rc.1")));
}

// -- comparisons -------------------------------------------------------------

TEST(VersionConstraint, GreaterEqual) {
    const auto c = VersionConstraint::parse(">=1.2.0");
    EXPECT_TRUE(c.satisfies(sv("1.2.0")));
    EXPECT_TRUE(c.satisfies(sv("2.0.0")));
    EXPECT_FALSE(c.satisfies(sv("1.1.9")));
}

TEST(VersionConstraint, StrictlyGreater) {
    const auto c = VersionConstraint::parse(">1.2.0");
    EXPECT_FALSE(c.satisfies(sv("1.2.0")));
    EXPECT_TRUE(c.satisfies(sv("1.2.1")));
}

TEST(VersionConstraint, LessEqual) {
    const auto c = VersionConstraint::parse("<=1.2.0");
    EXPECT_TRUE(c.satisfies(sv("1.2.0")));
    EXPECT_TRUE(c.satisfies(sv("1.0.0")));
    EXPECT_FALSE(c.satisfies(sv("1.2.1")));
}

TEST(VersionConstraint, StrictlyLess) {
    const auto c = VersionConstraint::parse("<1.2.0");
    EXPECT_FALSE(c.satisfies(sv("1.2.0")));
    EXPECT_TRUE(c.satisfies(sv("1.1.9")));
}

TEST(VersionConstraint, NotEqual) {
    const auto c = VersionConstraint::parse("!=1.2.0");
    EXPECT_FALSE(c.satisfies(sv("1.2.0")));
    EXPECT_TRUE(c.satisfies(sv("1.2.1")));
}

// -- caret -------------------------------------------------------------------

TEST(VersionConstraint, CaretMajor) {
    const auto c = VersionConstraint::parse("^1.2.3");  // >=1.2.3 <2.0.0
    EXPECT_FALSE(c.satisfies(sv("1.2.2")));
    EXPECT_TRUE(c.satisfies(sv("1.2.3")));
    EXPECT_TRUE(c.satisfies(sv("1.9.9")));
    EXPECT_FALSE(c.satisfies(sv("2.0.0")));
}

TEST(VersionConstraint, CaretZeroMajorPinsMinor) {
    const auto c = VersionConstraint::parse("^0.2.3");  // >=0.2.3 <0.3.0
    EXPECT_TRUE(c.satisfies(sv("0.2.3")));
    EXPECT_TRUE(c.satisfies(sv("0.2.9")));
    EXPECT_FALSE(c.satisfies(sv("0.3.0")));
    EXPECT_FALSE(c.satisfies(sv("0.2.2")));
}

TEST(VersionConstraint, CaretZeroMinorPinsPatch) {
    const auto c = VersionConstraint::parse("^0.0.3");  // ==0.0.3
    EXPECT_TRUE(c.satisfies(sv("0.0.3")));
    EXPECT_FALSE(c.satisfies(sv("0.0.4")));
    EXPECT_FALSE(c.satisfies(sv("0.0.2")));
}

// -- tilde -------------------------------------------------------------------

TEST(VersionConstraint, Tilde) {
    const auto c = VersionConstraint::parse("~1.2.3");  // >=1.2.3 <1.3.0
    EXPECT_FALSE(c.satisfies(sv("1.2.2")));
    EXPECT_TRUE(c.satisfies(sv("1.2.3")));
    EXPECT_TRUE(c.satisfies(sv("1.2.9")));
    EXPECT_FALSE(c.satisfies(sv("1.3.0")));
}

// -- intersection ------------------------------------------------------------

TEST(VersionConstraint, Intersection) {
    const auto c = VersionConstraint::parse(">=1.2.0 <2.0.0");
    EXPECT_FALSE(c.satisfies(sv("1.1.0")));
    EXPECT_TRUE(c.satisfies(sv("1.2.0")));
    EXPECT_TRUE(c.satisfies(sv("1.9.9")));
    EXPECT_FALSE(c.satisfies(sv("2.0.0")));
}

// -- raw / to_string ---------------------------------------------------------

TEST(VersionConstraint, RawAndToStringPreserveInput) {
    const auto c = VersionConstraint::parse(">=1.2.0 <2.0.0");
    EXPECT_EQ(c.raw(), ">=1.2.0 <2.0.0");
    EXPECT_EQ(c.to_string(), ">=1.2.0 <2.0.0");
}

// -- malformed input throws --------------------------------------------------

TEST(VersionConstraint, ParseThrowsOnInvalidVersion) {
    EXPECT_THROW((void)VersionConstraint::parse("^x.y"), std::invalid_argument);
    EXPECT_THROW((void)VersionConstraint::parse(">=1.2.0 <abc"), std::invalid_argument);
}

// -- equality / hashing ------------------------------------------------------

TEST(VersionConstraint, EqualityByRawString) {
    EXPECT_EQ(VersionConstraint::parse("^1.2.3"), VersionConstraint::parse("^1.2.3"));
    EXPECT_FALSE(VersionConstraint::parse("^1.2.3") == VersionConstraint::parse("~1.2.3"));
}

TEST(VersionConstraint, HashMatchesRaw) {
    constexpr std::hash<VersionConstraint> h;
    EXPECT_EQ(h(VersionConstraint::parse("^1.2.3")), h(VersionConstraint::parse("^1.2.3")));

    std::unordered_set<VersionConstraint> set;
    set.insert(VersionConstraint::parse("^1.2.3"));
    set.insert(VersionConstraint::parse("^1.2.3"));
    set.insert(VersionConstraint::parse("~1.2.3"));
    EXPECT_EQ(set.size(), 2u);
}

// -- text output -------------------------------------------------------------

TEST(VersionConstraint, OstreamInsertion) {
    std::ostringstream os;
    os << VersionConstraint::parse(">=1.2.0 <2.0.0");
    EXPECT_EQ(os.str(), ">=1.2.0 <2.0.0");
}

TEST(VersionConstraint, StdFormatMatchesRaw) {
    const auto c = VersionConstraint::parse("^1.2.3");
    EXPECT_EQ(std::format("{}", c), c.raw());
}

}  // namespace
