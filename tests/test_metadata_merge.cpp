#include <commons/metadata.hpp>

#include <gtest/gtest.h>

using comms::md::Array;
using comms::md::Object;
using comms::md::Value;

TEST(Merge, ScalarOverwrite) {
    Object dst{{"k", Value{1}}};
    const Object src{{"k", Value{2}}};
    dst.merge(src);
    EXPECT_EQ(dst["k"].as_int(), 2);
}

TEST(Merge, MissingKeyInserted) {
    Object dst{{"a", Value{1}}};
    const Object src{{"b", Value{2}}};
    dst.merge(src);
    EXPECT_EQ(dst["a"].as_int(), 1);
    EXPECT_EQ(dst["b"].as_int(), 2);
}

TEST(Merge, ObjectRecurses) {
    Object dst{{"sub", Value{Object{{"a", Value{1}}, {"b", Value{2}}}}}};
    const Object src{{"sub", Value{Object{{"b", Value{99}}, {"c", Value{3}}}}}};
    dst.merge(src);
    const Object& sub = dst.require_object("sub");
    EXPECT_EQ(sub.at("a").as_int(), 1);   // untouched
    EXPECT_EQ(sub.at("b").as_int(), 99);  // overwritten
    EXPECT_EQ(sub.at("c").as_int(), 3);   // inserted
}

TEST(Merge, ArrayReplaced) {
    Object dst{{"a", Value{Array{Value{1}, Value{2}, Value{3}}}}};
    const Object src{{"a", Value{Array{Value{9}}}}};
    dst.merge(src);
    const Array& a = dst.require_array("a");
    ASSERT_EQ(a.size(), 1u);
    EXPECT_EQ(a[0].as_int(), 9);
}

TEST(Merge, ScalarOverObjectOverwrites) {
    Object dst{{"k", Value{Object{{"x", Value{1}}}}}};
    const Object src{{"k", Value{5}}};
    dst.merge(src);
    EXPECT_TRUE(dst["k"].is_int());
    EXPECT_EQ(dst["k"].as_int(), 5);
}

TEST(Merge, ObjectOverScalarOverwrites) {
    Object dst{{"k", Value{5}}};
    const Object src{{"k", Value{Object{{"x", Value{1}}}}}};
    dst.merge(src);
    EXPECT_TRUE(dst["k"].is_object());
    EXPECT_EQ(dst.require_object("k").at("x").as_int(), 1);
}

TEST(Merge, FreeFunctionForwarder) {
    Object dst{{"a", Value{1}}};
    const Object src{{"b", Value{2}}};
    comms::md::merge(dst, src);
    EXPECT_TRUE(dst.contains("b"));
}
