// Consumer-style test: include only the umbrella header, use the public API
// through the `comms` namespace, and assert the advertised version.

#include <commons/commons.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace {

TEST(Commons, UmbrellaExposesPublicApi) {
    constexpr comms::FixedString tag{"commons"};
    EXPECT_EQ(tag.view(), "commons");

    constexpr comms::i32 a = 2;
    constexpr comms::u64 b = 40;
    EXPECT_EQ(static_cast<comms::u64>(a) + b, 42u);

    constexpr comms::f64 ratio = 0.5;
    EXPECT_DOUBLE_EQ(ratio * 2.0, 1.0);
}

TEST(Commons, VersionConstants) {
    EXPECT_EQ(comms::version, std::string_view{"0.1.0"});
    EXPECT_EQ(comms::version_major, 0);
    EXPECT_EQ(comms::version_minor, 1);
    EXPECT_EQ(comms::version_patch, 0);
}

}  // namespace
