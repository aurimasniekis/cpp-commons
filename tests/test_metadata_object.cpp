#include <commons/metadata.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using comms::md::Array;
using comms::md::Metadata;
using comms::md::Object;
using comms::md::Value;

TEST(ObjectMap, EmptyOnConstruction) {
    const Object o;
    EXPECT_TRUE(o.empty());
    EXPECT_EQ(o.size(), 0u);
}

TEST(ObjectMap, InitializerListCtor) {
    const Object o{{"a", Value{1}}, {"b", Value{"x"}}};
    EXPECT_EQ(o.size(), 2u);
    EXPECT_TRUE(o.contains("a"));
    EXPECT_TRUE(o.contains("b"));
}

TEST(ObjectMap, OperatorBracketInserts) {
    Object o;
    o["k"] = Value{42};
    EXPECT_TRUE(o.contains("k"));
    EXPECT_EQ(o["k"].as_int(), 42);
}

TEST(ObjectMap, OperatorBracketHeterogeneous) {
    Object o;
    constexpr std::string_view sv = "k";
    o[sv] = Value{1};
    EXPECT_TRUE(o.contains("k"));
}

TEST(ObjectMap, AtThrowsOnMissing) {
    Object o;
    EXPECT_THROW((void)o.at("missing"), std::out_of_range);
}

TEST(ObjectMap, AtRetrieves) {
    Object o{{"k", Value{1}}};
    EXPECT_EQ(o.at("k").as_int(), 1);
}

TEST(ObjectMap, FindReturnsIterator) {
    Object o{{"k", Value{1}}};
    const auto it = o.find("k");
    ASSERT_NE(it, o.end());
    EXPECT_EQ(it->first, "k");
    EXPECT_EQ(o.find("nope"), o.end());
}

TEST(ObjectMap, InsertOrAssign) {
    Object o;
    auto [it1, ins1] = o.insert_or_assign(std::string{"k"}, Value{1});
    EXPECT_TRUE(ins1);
    auto [it2, ins2] = o.insert_or_assign(std::string{"k"}, Value{2});
    EXPECT_FALSE(ins2);
    EXPECT_EQ(o["k"].as_int(), 2);
    (void)it1;
    (void)it2;
}

TEST(ObjectMap, Emplace) {
    Object o;
    auto [it, ins] = o.emplace(std::string{"k"}, Value{1});
    EXPECT_TRUE(ins);
    EXPECT_EQ(it->second.as_int(), 1);
}

TEST(ObjectMap, EraseByKey) {
    Object o{{"a", Value{1}}, {"b", Value{2}}};
    EXPECT_EQ(o.erase("a"), 1u);
    EXPECT_EQ(o.erase("a"), 0u);
    EXPECT_FALSE(o.contains("a"));
    EXPECT_TRUE(o.contains("b"));
}

TEST(ObjectMap, EraseByIterator) {
    Object o{{"a", Value{1}}, {"b", Value{2}}};
    const auto it = o.find("a");
    ASSERT_NE(it, o.end());
    o.erase(it);
    EXPECT_FALSE(o.contains("a"));
}

TEST(ObjectMap, ClearAndReserve) {
    Object o;
    o.reserve(16);
    o["a"] = Value{1};
    EXPECT_FALSE(o.empty());
    o.clear();
    EXPECT_TRUE(o.empty());
}

TEST(ObjectMap, RangeForIteration) {
    Object o{{"a", Value{1}}, {"b", Value{2}}};
    int count = 0;
    for (const auto& [k, v] : o) {
        (void)k;
        (void)v;
        ++count;
    }
    EXPECT_EQ(count, 2);
}

TEST(MetadataAlias, IsObject) {
    Metadata m;
    m["k"] = Value{"v"};
    EXPECT_TRUE(m.contains("k"));
}

TEST(MetadataAlias, RootAliasIsObject) {
    // comms::Metadata (root) aliases comms::md::Object too.
    comms::Metadata m;
    m["k"] = Value{"v"};
    EXPECT_TRUE(m.contains("k"));
}

TEST(ObjectMap, RawEscapeHatch) {
    Object o{{"a", Value{1}}};
    const auto& raw = o.raw();
    EXPECT_EQ(raw.size(), 1u);
}

TEST(InitListAssign, ArrayFromBareElements) {
    Object o;
    o["tags"] = {"alpha", "beta", "gamma"};
    ASSERT_TRUE(o["tags"].is_array());
    const Array& a = o["tags"].as_array();
    ASSERT_EQ(a.size(), 3u);
    EXPECT_EQ(a[0].as_string(), "alpha");
    EXPECT_EQ(a[2].as_string(), "gamma");
}

TEST(InitListAssign, ArrayMixedTypes) {
    Object o;
    o["mix"] = {1, "two", true, 3.5};
    ASSERT_TRUE(o["mix"].is_array());
    const Array& a = o["mix"].as_array();
    ASSERT_EQ(a.size(), 4u);
    EXPECT_TRUE(a[0].is_int());
    EXPECT_TRUE(a[1].is_string());
    EXPECT_TRUE(a[2].is_bool());
    EXPECT_TRUE(a[3].is_double());
}

TEST(InitListAssign, ObjectFromPairElements) {
    Object o;
    o["sub"] = {{"name", "widget"}, {"qty", 7}};
    ASSERT_TRUE(o["sub"].is_object());
    const Object& sub = o["sub"].as_object();
    EXPECT_EQ(sub.at("name").as_string(), "widget");
    EXPECT_EQ(sub.at("qty").as_int(), 7);
}

TEST(InitListAssign, ObjectNested) {
    // Nested Objects parse via braces alone. Nested *arrays* still need
    // an explicit comms::md::Array{} wrap, because braces-of-Values would
    // collide with the scalar Value{x} meaning.
    Object o;
    o["root"] = {{"a", {{"b", 1}, {"c", 2}}}, {"x", Array{1, 2, 3}}};
    ASSERT_TRUE(o["root"].is_object());
    const Object& root = o["root"].as_object();
    EXPECT_EQ(root.require_object("a").at("b").as_int(), 1);
    EXPECT_EQ(root.require_array("x").size(), 3u);
}
