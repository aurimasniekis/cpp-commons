#include <commons/semver.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <format>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using comms::SemVer;

SemVer sv(const std::string_view s) {
    const auto v = SemVer::parse(s);
    EXPECT_TRUE(v.has_value()) << "expected '" << s << "' to parse";
    return v.value_or(SemVer{});
}

// -- parsing -----------------------------------------------------------------

TEST(SemVer, ParseFullTriple) {
    const auto v = SemVer::parse("1.2.3");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->major, 1u);
    EXPECT_EQ(v->minor, 2u);
    EXPECT_EQ(v->patch, 3u);
    EXPECT_TRUE(v->prerelease.empty());
    EXPECT_TRUE(v->build.empty());
}

TEST(SemVer, ParsePartialVersionsDefaultToZero) {
    const auto major_only = SemVer::parse("1");
    ASSERT_TRUE(major_only.has_value());
    EXPECT_EQ(major_only->major, 1u);
    EXPECT_EQ(major_only->minor, 0u);
    EXPECT_EQ(major_only->patch, 0u);

    const auto major_minor = SemVer::parse("1.2");
    ASSERT_TRUE(major_minor.has_value());
    EXPECT_EQ(major_minor->major, 1u);
    EXPECT_EQ(major_minor->minor, 2u);
    EXPECT_EQ(major_minor->patch, 0u);
}

TEST(SemVer, ParseStripsLeadingVPrefix) {
    EXPECT_EQ(sv("v1.2.3"), sv("1.2.3"));
    EXPECT_EQ(sv("V1.2.3"), sv("1.2.3"));
}

TEST(SemVer, ParsePrerelease) {
    const auto v = SemVer::parse("1.2.3-alpha.1");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->patch, 3u);
    EXPECT_EQ(v->prerelease, "alpha.1");
    EXPECT_TRUE(v->build.empty());
}

TEST(SemVer, ParseBuildMetadata) {
    const auto v = SemVer::parse("1.2.3+build.5");
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(v->prerelease.empty());
    EXPECT_EQ(v->build, "build.5");
}

TEST(SemVer, ParsePrereleaseAndBuild) {
    const auto v = SemVer::parse("1.2.3-pre.1+build.2");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->prerelease, "pre.1");
    EXPECT_EQ(v->build, "build.2");
}

TEST(SemVer, ParseBuildMayContainHyphen) {
    const auto v = SemVer::parse("1.2.3+build-7");
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(v->prerelease.empty());
    EXPECT_EQ(v->build, "build-7");
}

// -- parsing failures --------------------------------------------------------

TEST(SemVer, ParseRejectsEmpty) {
    EXPECT_FALSE(SemVer::parse("").has_value());
    EXPECT_FALSE(SemVer::parse("v").has_value());
}

TEST(SemVer, ParseRejectsNonDigitCore) {
    EXPECT_FALSE(SemVer::parse("1.x.3").has_value());
    EXPECT_FALSE(SemVer::parse("abc").has_value());
}

TEST(SemVer, ParseRejectsTooManyComponents) {
    EXPECT_FALSE(SemVer::parse("1.2.3.4").has_value());
}

TEST(SemVer, ParseRejectsTrailingDot) {
    EXPECT_FALSE(SemVer::parse("1.2.").has_value());
    EXPECT_FALSE(SemVer::parse("1.").has_value());
}

TEST(SemVer, ParseRejectsOverflow) {
    EXPECT_FALSE(SemVer::parse("4294967296.0.0").has_value());  // 2^32
}

TEST(SemVer, ParseRejectsLeadingZeroNumericPrerelease) {
    EXPECT_FALSE(SemVer::parse("1.2.3-01").has_value());
    // A leading zero is fine when the identifier is not purely numeric.
    EXPECT_TRUE(SemVer::parse("1.2.3-0a").has_value());
    EXPECT_TRUE(SemVer::parse("1.2.3-0").has_value());
}

TEST(SemVer, ParseRejectsEmptyIdentifier) {
    EXPECT_FALSE(SemVer::parse("1.2.3-alpha..1").has_value());
    EXPECT_FALSE(SemVer::parse("1.2.3-").has_value());
    EXPECT_FALSE(SemVer::parse("1.2.3+").has_value());
}

TEST(SemVer, ParseRejectsInvalidIdentifierChars) {
    EXPECT_FALSE(SemVer::parse("1.2.3-alpha_1").has_value());
    EXPECT_FALSE(SemVer::parse("1.2.3+build_1").has_value());
}

// -- to_string ---------------------------------------------------------------

TEST(SemVer, ToStringRoundTrips) {
    for (const std::string s :
         {"1.2.3", "0.0.1", "1.2.3-alpha.1", "1.2.3+build.5", "1.2.3-pre.1+build.2"}) {
        EXPECT_EQ(sv(s).to_string(), s);
    }
}

TEST(SemVer, ToStringFillsPartialComponents) {
    EXPECT_EQ(sv("1").to_string(), "1.0.0");
    EXPECT_EQ(sv("1.2").to_string(), "1.2.0");
}

// -- ordering (§11) ----------------------------------------------------------

TEST(SemVer, NumericOrdering) {
    EXPECT_LT(sv("1.0.0"), sv("2.0.0"));
    EXPECT_LT(sv("1.0.0"), sv("1.1.0"));
    EXPECT_LT(sv("1.1.0"), sv("1.1.1"));
}

TEST(SemVer, PrereleaseRanksBelowRelease) {
    EXPECT_LT(sv("1.0.0-alpha"), sv("1.0.0"));
    EXPECT_GT(sv("1.0.0"), sv("1.0.0-alpha"));
}

TEST(SemVer, NumericPrereleaseIdentifiersCompareNumerically) {
    // The bug the full spec fixes: naive string compare would order "10" < "2".
    EXPECT_LT(sv("1.0.0-alpha.2"), sv("1.0.0-alpha.10"));
    EXPECT_LT(sv("1.0.0-1"), sv("1.0.0-2"));
    EXPECT_LT(sv("1.0.0-2"), sv("1.0.0-11"));
}

TEST(SemVer, NumericRanksBelowAlphanumeric) {
    EXPECT_LT(sv("1.0.0-1"), sv("1.0.0-alpha"));
    EXPECT_GT(sv("1.0.0-alpha"), sv("1.0.0-1"));
}

TEST(SemVer, FewerFieldsRankLower) {
    EXPECT_LT(sv("1.0.0-alpha"), sv("1.0.0-alpha.1"));
}

TEST(SemVer, FullSpecChain) {
    // The canonical SemVer §11 example.
    EXPECT_LT(sv("1.0.0-alpha"), sv("1.0.0-alpha.1"));
    EXPECT_LT(sv("1.0.0-alpha.1"), sv("1.0.0-alpha.beta"));
    EXPECT_LT(sv("1.0.0-alpha.beta"), sv("1.0.0-beta"));
    EXPECT_LT(sv("1.0.0-beta"), sv("1.0.0-beta.2"));
    EXPECT_LT(sv("1.0.0-beta.2"), sv("1.0.0-beta.11"));
    EXPECT_LT(sv("1.0.0-beta.11"), sv("1.0.0-rc.1"));
    EXPECT_LT(sv("1.0.0-rc.1"), sv("1.0.0"));
}

// -- build metadata is ignored in comparison ---------------------------------

TEST(SemVer, BuildMetadataIgnoredInComparisonAndEquality) {
    EXPECT_EQ(sv("1.2.3+build.1"), sv("1.2.3+build.2"));
    EXPECT_EQ(sv("1.2.3"), sv("1.2.3+build.1"));
    EXPECT_TRUE(std::is_eq(sv("1.2.3+a") <=> sv("1.2.3+b")));
    // ...but it is preserved in the string form.
    EXPECT_EQ(sv("1.2.3+build.1").to_string(), "1.2.3+build.1");
    EXPECT_NE(sv("1.2.3+build.1").build, sv("1.2.3+build.2").build);
}

TEST(SemVer, StdSortAscending) {
    std::vector<SemVer> v{
        sv("1.0.0"),
        sv("1.0.0-rc.1"),
        sv("1.0.0-beta.11"),
        sv("1.0.0-beta.2"),
        sv("1.0.0-beta"),
        sv("1.0.0-alpha.beta"),
        sv("1.0.0-alpha.1"),
        sv("1.0.0-alpha"),
    };
    std::ranges::sort(v);
    std::vector<std::string> got;
    got.reserve(v.size());
    for (const auto& s : v) {
        got.push_back(s.to_string());
    }
    const std::vector<std::string> want{
        "1.0.0-alpha",
        "1.0.0-alpha.1",
        "1.0.0-alpha.beta",
        "1.0.0-beta",
        "1.0.0-beta.2",
        "1.0.0-beta.11",
        "1.0.0-rc.1",
        "1.0.0",
    };
    EXPECT_EQ(got, want);
}

// -- text output -------------------------------------------------------------

TEST(SemVer, ToStringFreeFunction) {
    EXPECT_EQ(comms::to_string(sv("1.2.3-rc.1")), "1.2.3-rc.1");
}

TEST(SemVer, OstreamInsertion) {
    std::ostringstream os;
    os << sv("1.2.3-rc.1+b.2");
    EXPECT_EQ(os.str(), "1.2.3-rc.1+b.2");
}

TEST(SemVer, StdFormatMatchesToString) {
    const auto v = sv("1.2.3-rc.1+b.2");
    EXPECT_EQ(std::format("{}", v), v.to_string());
}

// -- hashing -----------------------------------------------------------------

TEST(SemVer, HashEqualForEqualKeys) {
    constexpr std::hash<SemVer> h;
    EXPECT_EQ(h(sv("1.2.3")), h(sv("1.2.3")));
    // Build is excluded from equality, so it must be excluded from the hash.
    EXPECT_EQ(h(sv("1.2.3+a")), h(sv("1.2.3+b")));
    EXPECT_EQ(h(sv("1.2.3+a")), h(sv("1.2.3")));
}

TEST(SemVer, UnorderedSetUsesHashAndEquality) {
    std::unordered_set<SemVer> set;
    set.insert(sv("1.2.3"));
    set.insert(sv("1.2.3+build"));  // equal to the above → no new element
    set.insert(sv("1.2.4"));
    EXPECT_EQ(set.size(), 2u);
    EXPECT_TRUE(set.contains(sv("1.2.3")));
    EXPECT_TRUE(set.contains(sv("1.2.4")));
}

}  // namespace
