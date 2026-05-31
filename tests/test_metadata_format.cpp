#include <commons/metadata.hpp>

#include <gtest/gtest.h>

#include <format>
#include <string>

using comms::md::Array;
using comms::md::Object;
using comms::md::Value;

TEST(Format, Scalars) {
    EXPECT_EQ(std::format("{}", Value{}), "null");
    EXPECT_EQ(std::format("{}", Value{true}), "true");
    EXPECT_EQ(std::format("{}", Value{42}), "42");
    EXPECT_EQ(std::format("{}", Value{"hi"}), "\"hi\"");
}

TEST(Format, Array) {
    EXPECT_EQ(std::format("{}", Value{Array{Value{1}, Value{2}}}), "[1,2]");
}

TEST(Format, Object) {
    EXPECT_EQ(std::format("{}", Value{Object{{"k", Value{1}}}}), "{\"k\":1}");
}

TEST(Format, NestedStructure) {
    Value v{Object{{"a", Value{Array{Value{1}, Value{Object{{"b", Value{2}}}}}}}}};
    EXPECT_EQ(std::format("{}", v), "{\"a\":[1,{\"b\":2}]}");
}

TEST(Format, NonEmptySpecThrows) {
    Value v{1};
    EXPECT_THROW((void)std::vformat("{:p}", std::make_format_args(v)), std::format_error);
}

TEST(Format, FormattersExistForObjectAndArray) {
    Object o{{"k", Value{1}}};
    Array a{Value{1}, Value{2}};
    EXPECT_EQ(std::format("{}", o), "{\"k\":1}");
    EXPECT_EQ(std::format("{}", a), "[1,2]");
}
