#include <commons/metadata.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <variant>

using comms::md::Array;
using comms::md::Object;
using comms::md::Value;

TEST(ValueCtor, DefaultIsNull) {
    const Value v;
    EXPECT_TRUE(v.is_null());
}

TEST(ValueCtor, NullptrIsNull) {
    const Value v{nullptr};
    EXPECT_TRUE(v.is_null());
}

TEST(ValueCtor, BoolStaysBool) {
    Value t{true};
    Value f{false};
    EXPECT_TRUE(t.is_bool());
    EXPECT_TRUE(f.is_bool());
    EXPECT_FALSE(t.is_int());
    EXPECT_FALSE(t.is_uint());
    EXPECT_EQ(t.as_bool(), true);
    EXPECT_EQ(f.as_bool(), false);
}

TEST(ValueCtor, SignedIntsRouteToInt64) {
    Value v_int{42};
    Value v_neg{-7};
    Value v_short{static_cast<short>(3)};
    EXPECT_TRUE(v_int.is_int());
    EXPECT_TRUE(v_neg.is_int());
    EXPECT_TRUE(v_short.is_int());
    EXPECT_EQ(v_int.as_int(), 42);
    EXPECT_EQ(v_neg.as_int(), -7);
}

TEST(ValueCtor, UnsignedIntsRouteToUint64) {
    Value v{static_cast<std::uint32_t>(123)};
    Value v64{std::uint64_t{1ULL << 40}};
    EXPECT_TRUE(v.is_uint());
    EXPECT_TRUE(v64.is_uint());
    EXPECT_EQ(v.as_uint(), 123u);
    EXPECT_EQ(v64.as_uint(), (1ULL << 40));
}

TEST(ValueCtor, FloatStaysFloat) {
    Value f{1.5f};
    EXPECT_TRUE(f.is_float());
    EXPECT_FALSE(f.is_double());
    EXPECT_TRUE(f.is_number());
    EXPECT_FLOAT_EQ(f.as_float(), 1.5F);
}

TEST(ValueCtor, DoubleStaysDouble) {
    Value d{2.25};
    EXPECT_TRUE(d.is_double());
    EXPECT_FALSE(d.is_float());
    EXPECT_TRUE(d.is_number());
    EXPECT_DOUBLE_EQ(d.as_double(), 2.25);
}

TEST(ValueAccessors, AsDoubleWidensFromFloat) {
    Value f{1.5F};
    EXPECT_DOUBLE_EQ(f.as_double(), 1.5);
}

TEST(ValueAccessors, AsFloatStrictThrowsOnDouble) {
    Value d{2.25};
    EXPECT_THROW((void)d.as_float(), std::bad_variant_access);
}

TEST(ValueAccessors, AsFloatIfFamily) {
    Value f{3.5F};
    Value d{3.5};
    EXPECT_NE(f.as_float_if(), nullptr);
    EXPECT_EQ(d.as_float_if(), nullptr);
    EXPECT_EQ(f.as_double_if(), nullptr);
    EXPECT_NE(d.as_double_if(), nullptr);
}

TEST(ValueCtor, StringFlavors) {
    Value a{std::string{"abc"}};
    Value b{std::string_view{"abc"}};
    Value c{"abc"};
    EXPECT_TRUE(a.is_string());
    EXPECT_TRUE(b.is_string());
    EXPECT_TRUE(c.is_string());
    EXPECT_EQ(a.as_string(), "abc");
    EXPECT_EQ(b.as_string(), "abc");
    EXPECT_EQ(c.as_string(), "abc");
    // const char* must NOT decay to bool.
    EXPECT_FALSE(c.is_bool());
}

TEST(ValueCtor, ArrayAndObject) {
    Value arr{Array{Value{1}, Value{2}, Value{3}}};
    Value obj{Object{{"a", Value{1}}, {"b", Value{"x"}}}};
    EXPECT_TRUE(arr.is_array());
    EXPECT_TRUE(obj.is_object());
    EXPECT_EQ(arr.as_array().size(), 3u);
    EXPECT_EQ(obj.as_object().size(), 2u);
}

TEST(ValuePredicates, IsNumber) {
    EXPECT_TRUE(Value{1}.is_number());
    EXPECT_TRUE(Value{1u}.is_number());
    EXPECT_TRUE(Value{1.0}.is_number());
    EXPECT_FALSE(Value{}.is_number());
    EXPECT_FALSE(Value{true}.is_number());
    EXPECT_FALSE(Value{"x"}.is_number());
}

TEST(ValueAccessors, StrictThrowsOnWrongType) {
    Value v{"x"};
    EXPECT_THROW((void)v.as_bool(), std::bad_variant_access);
    EXPECT_THROW((void)v.as_int(), std::bad_variant_access);
    EXPECT_THROW((void)v.as_uint(), std::bad_variant_access);
}

TEST(ValueAccessors, AsDoubleWidensFromInt) {
    Value i{std::int64_t{42}};
    Value u{std::uint64_t{99}};
    Value d{3.5};
    EXPECT_DOUBLE_EQ(i.as_double(), 42.0);
    EXPECT_DOUBLE_EQ(u.as_double(), 99.0);
    EXPECT_DOUBLE_EQ(d.as_double(), 3.5);
}

TEST(ValueAccessors, AsDoubleThrowsOnNonNumber) {
    Value s{"abc"};
    EXPECT_THROW((void)s.as_double(), comms::md::TypeError);
}

TEST(ValueAccessors, PointerOverloadsReturnNullOnMismatch) {
    Value v{"hello"};
    EXPECT_EQ(v.as_bool_if(), nullptr);
    EXPECT_EQ(v.as_int_if(), nullptr);
    const std::string* p = v.as_string_if();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, "hello");
}

TEST(ValueAccessors, GetIfTemplate) {
    Value v{std::int64_t{42}};
    EXPECT_NE(v.get_if<std::int64_t>(), nullptr);
    EXPECT_EQ(v.get_if<double>(), nullptr);
}

TEST(ValueAccessors, ValueOr) {
    Value v{std::int64_t{5}};
    EXPECT_EQ(v.value_or<std::int64_t>(99), 5);
    Value missing;
    EXPECT_EQ(missing.value_or<std::int64_t>(99), 99);
}

TEST(ValueFactory, FreeFunctions) {
    EXPECT_TRUE(comms::md::null().is_null());
    EXPECT_TRUE(comms::md::boolean(true).is_bool());
    EXPECT_TRUE(comms::md::number(7).is_int());
    EXPECT_TRUE(comms::md::number(7u).is_uint());
    EXPECT_TRUE(comms::md::number(7.5).is_double());
    EXPECT_TRUE(comms::md::string("x").is_string());
    EXPECT_TRUE(comms::md::string(std::string_view{"y"}).is_string());

    Array a = comms::md::array({Value{1}, Value{2}});
    EXPECT_EQ(a.size(), 2u);

    Object o = comms::md::object({{"k", Value{"v"}}});
    EXPECT_EQ(o.size(), 1u);
}
