#include <commons/metadata.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>

using comms::md::Array;
using comms::md::Object;
using comms::md::Value;

namespace {

std::string to_str(const Value& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

}  // namespace

TEST(OStream, Scalars) {
    EXPECT_EQ(to_str(Value{}), "null");
    EXPECT_EQ(to_str(Value{true}), "true");
    EXPECT_EQ(to_str(Value{false}), "false");
    EXPECT_EQ(to_str(Value{std::int64_t{-7}}), "-7");
    EXPECT_EQ(to_str(Value{std::uint64_t{42}}), "42");
    EXPECT_EQ(to_str(Value{"hi"}), "\"hi\"");
}

TEST(OStream, StringEscaping) {
    EXPECT_EQ(to_str(Value{std::string{"a\"b"}}), "\"a\\\"b\"");
    EXPECT_EQ(to_str(Value{std::string{"a\\b"}}), "\"a\\\\b\"");
    EXPECT_EQ(to_str(Value{std::string{"\n"}}), "\"\\n\"");
    EXPECT_EQ(to_str(Value{std::string{"\t"}}), "\"\\t\"");
    EXPECT_EQ(to_str(Value{std::string{"\r"}}), "\"\\r\"");
    EXPECT_EQ(to_str(Value{std::string{"\b"}}), "\"\\b\"");
    EXPECT_EQ(to_str(Value{std::string{"\f"}}), "\"\\f\"");

    // 0x01 → 
    std::string s;
    s.push_back('\x01');
    EXPECT_EQ(to_str(Value{s}), "\"\\u0001\"");
}

TEST(OStream, Array) {
    const Value v{Array{Value{1}, Value{"x"}, Value{true}}};
    EXPECT_EQ(to_str(v), "[1,\"x\",true]");
}

TEST(OStream, Object) {
    const Object o{{"k", Value{1}}};
    std::ostringstream os;
    os << o;
    // Single-key object: order is deterministic.
    EXPECT_EQ(os.str(), "{\"k\":1}");
}

TEST(OStream, NestedJsonLike) {
    const Object o{{"a", Value{Object{{"b", Value{Array{Value{1}, Value{2}}}}}}}};
    std::ostringstream os;
    os << o;
    EXPECT_EQ(os.str(), "{\"a\":{\"b\":[1,2]}}");
}

TEST(OStream, DoublePrintsShortestRoundTrip) {
    // The literal 3.14 is the closest double to mathematical 3.14. The shortest
    // decimal that recovers exactly that double when parsed is "3.14" — that's
    // what std::to_chars produces and what we expect here.
    EXPECT_EQ(to_str(Value{3.14}), "3.14");
    EXPECT_EQ(to_str(Value{-10.5}), "-10.5");
    EXPECT_EQ(to_str(Value{0.0}), "0");
}

TEST(OStream, FloatPrintsShortestRoundTrip) {
    EXPECT_EQ(to_str(Value{1.5F}), "1.5");
    EXPECT_EQ(to_str(Value{3.14F}), "3.14");
}
