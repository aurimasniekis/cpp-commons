#include <commons/metadata.hpp>

#include <gtest/gtest.h>

#include <string>

using comms::md::Array;
using comms::md::Object;
using comms::md::Value;

namespace {

Object sample() {
    return Object{
        {"a", Value{Object{{"b", Value{Object{{"c", Value{42}}}}}}}},
        {"items", Value{Array{Value{10}, Value{20}, Value{30}}}},
        {"nested", Value{Array{Value{Array{Value{"deep"}}}, Value{Object{{"k", Value{"v"}}}}}}},
        {"name", Value{"top"}},
    };
}

}  // namespace

TEST(Path, DotChain) {
    Object o = sample();
    const Value* p = o.find_path("a.b.c");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->as_int(), 42);
}

TEST(Path, ArrayIndex) {
    Object o = sample();
    const Value* p = o.find_path("items[0]");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->as_int(), 10);
    p = o.find_path("items[2]");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->as_int(), 30);
}

TEST(Path, MixedDotAndBracket) {
    Object o = sample();
    const Value* p = o.find_path("nested[1].k");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->as_string(), "v");
}

TEST(Path, NestedBrackets) {
    Object o = sample();
    const Value* p = o.find_path("nested[0][0]");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->as_string(), "deep");
}

TEST(Path, EmptyPathReturnsNullOnFind) {
    // Empty path means "root object itself" — find_path returns nullptr since
    // we can't return a Value& to the Object as a Value. contains_path is false.
    Object o = sample();
    EXPECT_EQ(o.find_path(""), nullptr);
    EXPECT_FALSE(o.contains_path(""));
}

TEST(Path, MissingKeyReturnsNull) {
    Object o = sample();
    EXPECT_EQ(o.find_path("a.nope"), nullptr);
    EXPECT_FALSE(o.contains_path("a.nope"));
}

TEST(Path, OutOfRangeIndexReturnsNull) {
    Object o = sample();
    EXPECT_EQ(o.find_path("items[99]"), nullptr);
}

TEST(Path, NonObjectIntermediateReportsMalformed) {
    // "name" is a string, not an object — descending into it is a type error.
    Object o = sample();
    EXPECT_EQ(o.find_path("name.x"), nullptr);
    EXPECT_THROW((void)o.require_path("name.x"), comms::md::TypeError);
}

TEST(Path, NonArrayIntermediateReportsMalformed) {
    // "name" is a string — bracket index on it is malformed.
    Object o = sample();
    EXPECT_EQ(o.find_path("name[0]"), nullptr);
    EXPECT_THROW((void)o.require_path("name[0]"), comms::md::TypeError);
}

TEST(Path, RequirePathMissing) {
    Object o = sample();
    EXPECT_THROW((void)o.require_path("a.nope"), comms::md::MissingKeyError);
}

TEST(Path, RequirePathFound) {
    Object o = sample();
    const Value& v = o.require_path("a.b.c");
    EXPECT_EQ(v.as_int(), 42);
}

TEST(Path, MalformedSyntax) {
    Object o = sample();
    EXPECT_EQ(o.find_path("items["), nullptr);
    EXPECT_EQ(o.find_path("items[abc]"), nullptr);
    EXPECT_EQ(o.find_path("..a"), nullptr);
    EXPECT_THROW((void)o.require_path("items["), comms::md::TypeError);
}

TEST(Path, ContainsPath) {
    const Object o = sample();
    EXPECT_TRUE(o.contains_path("a.b.c"));
    EXPECT_FALSE(o.contains_path("a.b.x"));
    EXPECT_TRUE(o.contains_path("items[1]"));
    EXPECT_FALSE(o.contains_path("items[5]"));
}

TEST(Path, MutableFindAllowsModification) {
    Object o = sample();
    Value* p = o.find_path("a.b.c");
    ASSERT_NE(p, nullptr);
    *p = Value{100};
    EXPECT_EQ(o.find_path("a.b.c")->as_int(), 100);
}
