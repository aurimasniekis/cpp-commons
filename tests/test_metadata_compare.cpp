#include <commons/metadata.hpp>

#include <gtest/gtest.h>

#include <cstdint>

using comms::md::Array;
using comms::md::Object;
using comms::md::Value;

TEST(Compare, SameTypeEquality) {
    EXPECT_EQ(Value{}, Value{});
    EXPECT_EQ(Value{true}, Value{true});
    EXPECT_NE(Value{true}, Value{false});
    EXPECT_EQ(Value{1}, Value{1});
    EXPECT_NE(Value{1}, Value{2});
    EXPECT_EQ(Value{"abc"}, Value{"abc"});
    EXPECT_NE(Value{"abc"}, Value{"xyz"});
}

TEST(Compare, DifferentTypesAreUnequal) {
    EXPECT_NE(Value{}, Value{0});
    EXPECT_NE(Value{0}, Value{false});
    EXPECT_NE(Value{0}, Value{0.0});
    EXPECT_NE(Value{std::int64_t{0}}, Value{std::uint64_t{0}});
}

TEST(Compare, NestedArrayEquality) {
    Value a{Array{Value{1}, Value{2}, Value{3}}};
    Value b{Array{Value{1}, Value{2}, Value{3}}};
    Value c{Array{Value{1}, Value{2}, Value{4}}};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(Compare, ObjectEqualityOrderIndependent) {
    const Object a{{"x", Value{1}}, {"y", Value{2}}};
    const Object b{{"y", Value{2}}, {"x", Value{1}}};
    EXPECT_EQ(a, b);
    EXPECT_EQ(Value{a}, Value{b});
}

TEST(Compare, NestedObjectEquality) {
    const Object a{{"sub", Value{Object{{"k", Value{42}}}}}};
    const Object b{{"sub", Value{Object{{"k", Value{42}}}}}};
    EXPECT_EQ(a, b);
}
