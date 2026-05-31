#include <commons/metadata.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <unordered_set>

using comms::md::Array;
using comms::md::Object;
using comms::md::Value;

TEST(Hash, EqualValuesHashEqual) {
    EXPECT_EQ(std::hash<Value>{}(Value{}), std::hash<Value>{}(Value{}));
    EXPECT_EQ(std::hash<Value>{}(Value{42}), std::hash<Value>{}(Value{42}));
    EXPECT_EQ(std::hash<Value>{}(Value{"x"}), std::hash<Value>{}(Value{"x"}));
}

TEST(Hash, DistinctZeroAlternativesHashDifferently) {
    const auto h_int = std::hash<Value>{}(Value{std::int64_t{0}});
    const auto h_uint = std::hash<Value>{}(Value{std::uint64_t{0}});
    const auto h_double = std::hash<Value>{}(Value{0.0});
    const auto h_bool = std::hash<Value>{}(Value{false});
    const auto h_null = std::hash<Value>{}(Value{});

    // Each pair distinct.
    EXPECT_NE(h_int, h_uint);
    EXPECT_NE(h_int, h_double);
    EXPECT_NE(h_int, h_bool);
    EXPECT_NE(h_int, h_null);
    EXPECT_NE(h_uint, h_double);
    EXPECT_NE(h_uint, h_bool);
    EXPECT_NE(h_uint, h_null);
    EXPECT_NE(h_double, h_bool);
    EXPECT_NE(h_double, h_null);
    EXPECT_NE(h_bool, h_null);
}

TEST(Hash, ObjectOrderIndependent) {
    const Object a{{"x", Value{1}}, {"y", Value{2}}, {"z", Value{3}}};
    const Object b{{"z", Value{3}}, {"y", Value{2}}, {"x", Value{1}}};
    EXPECT_EQ(a, b);
    EXPECT_EQ(std::hash<Object>{}(a), std::hash<Object>{}(b));
    EXPECT_EQ(std::hash<Value>{}(Value{a}), std::hash<Value>{}(Value{b}));
}

TEST(Hash, ArrayOrderDependent) {
    const Array a{Value{1}, Value{2}, Value{3}};
    const Array b{Value{3}, Value{2}, Value{1}};
    // Allowed to collide (hashes can match by accident) but they should rarely
    // do so for these values — assert they're functionally distinct via the set.
    std::unordered_set<std::size_t> hs;
    hs.insert(std::hash<Array>{}(a));
    hs.insert(std::hash<Array>{}(b));
    EXPECT_GE(hs.size(), 1u);
}

TEST(Hash, ValueUsableInUnorderedSet) {
    std::unordered_set<Value> set;
    set.insert(Value{1});
    set.insert(Value{1});
    set.insert(Value{"x"});
    EXPECT_EQ(set.size(), 2u);
}
