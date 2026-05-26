#include <commons/commons.hpp>
#include <commons/json.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace {

using json = nlohmann::json;

// Two flags defined + auto-registered for the FlagRef/FlagSet round-trip tests.
COMMONS_DEFINE_FLAG(JsonFlagOne, "json.one");
COMMONS_DEFINE_FLAG(JsonFlagTwo, "json.two");

TEST(Json, FixedStringRoundTrip) {
    comms::FixedString s{"hello"};
    const json j = s;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "hello");

    const auto back = j.get<comms::FixedString<6>>();
    EXPECT_TRUE(back == s);
}

TEST(Json, FixedStringTooLongThrows) {
    json j = "toolong";
    EXPECT_THROW((void)j.get<comms::FixedString<3>>(), nlohmann::json::other_error);
}

TEST(Json, FixedStringExactCapacityFits) {
    const json j = "abc";  // 3 chars, fits FixedString<4> (capacity 3)
    const auto s = j.get<comms::FixedString<4>>();
    EXPECT_EQ(s.view(), "abc");
}

#if defined(COMMONS_HAS_INT128)

TEST(Json, I128TravelsAsDecimalString) {
    comms::i128 v = -comms::i128{123456789012345678};
    const json j = v;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "-123456789012345678");
    EXPECT_EQ(j.get<comms::i128>(), v);
}

TEST(Json, I128MinRoundTrip) {
    const auto imin = static_cast<comms::i128>(static_cast<comms::u128>(1) << 127);
    const json j = imin;
    EXPECT_EQ(j.get<comms::i128>(), imin);
    EXPECT_EQ(j.get<std::string>(), "-170141183460469231731687303715884105728");
}

TEST(Json, U128MaxRoundTrip) {
    const auto umax = static_cast<comms::u128>(-1);
    const json j = umax;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "340282366920938463463374607431768211455");
    EXPECT_EQ(j.get<comms::u128>(), umax);
}

TEST(Json, U128Zero) {
    comms::u128 zero = 0;
    const json j = zero;
    EXPECT_EQ(j.get<std::string>(), "0");
    EXPECT_EQ(j.get<comms::u128>(), zero);
}

TEST(Json, I128InvalidStringThrows) {
    json j = "12x34";
    EXPECT_THROW((void)j.get<comms::i128>(), nlohmann::json::other_error);
}

TEST(Json, I128SignRoundTrips) {
    json j = "+42";
    EXPECT_EQ(j.get<comms::i128>(), comms::i128{42});
    json neg = "-42";
    EXPECT_EQ(neg.get<comms::i128>(), -comms::i128{42});
}

// One past i128_max (2^127) must not be accepted on the non-negative path.
TEST(Json, I128OverflowThrows) {
    json j = "170141183460469231731687303715884105728";  // 2^127
    EXPECT_THROW((void)j.get<comms::i128>(), nlohmann::json::other_error);
}

// One past u128_max (2^128) must be rejected rather than silently wrapping.
TEST(Json, U128OverflowThrows) {
    json j = "340282366920938463463374607431768211456";  // 2^128
    EXPECT_THROW((void)j.get<comms::u128>(), nlohmann::json::other_error);
}

TEST(Json, I128NonStringThrows) {
    json j = 42;  // a JSON number, not the expected decimal string
    EXPECT_THROW((void)j.get<comms::i128>(), nlohmann::json::other_error);
    EXPECT_THROW((void)j.get<comms::u128>(), nlohmann::json::other_error);
}

TEST(Json, I128EmptyAndSignOnlyThrow) {
    EXPECT_THROW((void)json("").get<comms::i128>(), nlohmann::json::other_error);
    EXPECT_THROW((void)json("-").get<comms::i128>(), nlohmann::json::other_error);
    EXPECT_THROW((void)json("").get<comms::u128>(), nlohmann::json::other_error);
}

#endif  // COMMONS_HAS_INT128

// Color travels as a hex string: #RRGGBB when opaque, #RRGGBBAA otherwise.
TEST(Json, ColorOpaqueRoundTripsAsHexString) {
    constexpr comms::Color c = comms::Color::rgb(0x63, 0x66, 0xF1);
    const json j = c;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "#6366f1");
    EXPECT_EQ(j.get<comms::Color>(), c);
}

TEST(Json, ColorWithAlphaUsesEightDigitHex) {
    constexpr comms::Color c = comms::Color::rgba(0x63, 0x66, 0xF1, 0x80);
    const json j = c;
    EXPECT_EQ(j.get<std::string>(), "#6366f180");
    EXPECT_EQ(j.get<comms::Color>(), c);
}

TEST(Json, ColorAcceptsNamedAndFunctionalStrings) {
    EXPECT_EQ(json("red").get<comms::Color>(), comms::Color::rgb(255, 0, 0));
    EXPECT_EQ(json("rgb(0 0 255)").get<comms::Color>(), comms::Color::rgb(0, 0, 255));
}

TEST(Json, ColorInvalidStringThrows) {
    json j = "notacolor";
    EXPECT_THROW((void)j.get<comms::Color>(), nlohmann::json::other_error);
}

// Icon travels as its canonical `set:name` string.
TEST(Json, IconRoundTripsAsString) {
    constexpr comms::Icon i = comms::Icon::from("mdi:abacus");
    const json j = i;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "mdi:abacus");
    EXPECT_EQ(j.get<comms::Icon>(), i);
}

TEST(Json, IconInvalidStringThrows) {
    json j = "no-colon";
    EXPECT_THROW((void)j.get<comms::Icon>(), nlohmann::json::other_error);
}

TEST(Json, HslRoundTripsAsObject) {
    constexpr comms::Hsl h{231.0, 0.84, 0.67, 1.0};
    json j = h;
    EXPECT_TRUE(j.is_object());
    EXPECT_DOUBLE_EQ(j.at("h").get<comms::f64>(), 231.0);
    EXPECT_DOUBLE_EQ(j.at("s").get<comms::f64>(), 0.84);
    EXPECT_DOUBLE_EQ(j.at("l").get<comms::f64>(), 0.67);
    EXPECT_DOUBLE_EQ(j.at("a").get<comms::f64>(), 1.0);
    EXPECT_TRUE(j.get<comms::Hsl>() == h);
}

TEST(Json, HsvRoundTripsAsObject) {
    constexpr comms::Hsv v{120.0, 0.5, 0.25, 0.5};
    json j = v;
    EXPECT_TRUE(j.is_object());
    EXPECT_DOUBLE_EQ(j.at("v").get<comms::f64>(), 0.25);
    EXPECT_TRUE(j.get<comms::Hsv>() == v);
}

// DisplayInfo travels as an object; absent fields are omitted (omit-empty).
TEST(Json, DisplayInfoFullRoundTrip) {
    const comms::DisplayInfo di{
        .name = "Abacus",
        .description = "A counting frame",
        .icon = comms::Icon::from("mdi:abacus"),
        .color = comms::Color::rgb(0x63, 0x66, 0xF1),
    };
    json j = di;
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j.at("name").get<std::string>(), "Abacus");
    EXPECT_EQ(j.at("description").get<std::string>(), "A counting frame");
    EXPECT_EQ(j.at("icon").get<std::string>(), "mdi:abacus");
    EXPECT_EQ(j.at("color").get<std::string>(), "#6366f1");
    EXPECT_EQ(j.get<comms::DisplayInfo>(), di);
}

TEST(Json, DisplayInfoOmitsAbsentFields) {
    const comms::DisplayInfo di{.name = "Just a name"};
    json j = di;
    EXPECT_TRUE(j.is_object());
    EXPECT_TRUE(j.contains("name"));
    EXPECT_FALSE(j.contains("description"));
    EXPECT_FALSE(j.contains("icon"));
    EXPECT_FALSE(j.contains("color"));
    EXPECT_EQ(j.get<comms::DisplayInfo>(), di);
}

// The fixed-width builtin aliases need no custom hooks — nlohmann serializes
// the underlying arithmetic types natively.
TEST(Json, BuiltinAliasesUseNativeSerialization) {
    comms::i32 i = -7;
    comms::u64 u = 42;
    comms::f64 f = 1.5;
    EXPECT_EQ(json(i).get<comms::i32>(), i);
    EXPECT_EQ(json(u).get<comms::u64>(), u);
    EXPECT_DOUBLE_EQ(json(f).get<comms::f64>(), f);
    EXPECT_TRUE(json(i).is_number());
}

// Complex aliases travel as a two-element [real, imaginary] array.
TEST(Json, ComplexFloatRoundTrip) {
    comms::cf64 z{1.5, -2.5};
    json j = z;
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 2u);
    EXPECT_DOUBLE_EQ(j.at(0).get<comms::f64>(), 1.5);
    EXPECT_DOUBLE_EQ(j.at(1).get<comms::f64>(), -2.5);
    EXPECT_EQ(j.get<comms::cf64>(), z);
}

TEST(Json, ComplexIntRoundTrip) {
    comms::cs32 n{3, -4};
    const json j = n;
    EXPECT_EQ(j.dump(), "[3,-4]");
    EXPECT_EQ(j.get<comms::cs32>(), n);
}

// -- std::optional<T> --------------------------------------------------------
// nullopt ⇄ JSON null; a value travels via the wrapped T's own serializer.

TEST(Json, OptionalWithValueSerializesAsInnerValue) {
    const std::optional<int> opt = 7;
    const json j = opt;
    EXPECT_TRUE(j.is_number());
    EXPECT_EQ(j.dump(), "7");
    EXPECT_EQ(j.get<std::optional<int>>(), opt);
}

TEST(Json, OptionalNulloptSerializesAsNull) {
    const std::optional<int> opt = std::nullopt;
    const json j = opt;
    EXPECT_TRUE(j.is_null());
    EXPECT_EQ(j.get<std::optional<int>>(), std::nullopt);
}

TEST(Json, OptionalNullJsonDeserializesToNullopt) {
    const json j = nullptr;
    EXPECT_FALSE(j.get<std::optional<std::string>>().has_value());
}

// The inner T reuses its own hooks — here a Commons type with a string mapping.
TEST(Json, OptionalReusesInnerTypeHooks) {
    const std::optional<comms::Color> opt = comms::Color::parse("#6366f1");
    const json j = opt;
    EXPECT_EQ(j.get<std::string>(), "#6366f1");
    EXPECT_EQ(j.get<std::optional<comms::Color>>(), opt);
}

// -- Flag / FlagSet ----------------------------------------------------------
// Flags are resolved by name against the GlobalFlagRegistry on read-back.

// A FlagRef travels as its name string.
TEST(Json, FlagRefRoundTripsAsNameString) {
    const auto ref = comms::FlagRef::of<JsonFlagOne>();
    const json j = ref;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "json.one");
    EXPECT_EQ(j.get<comms::FlagRef>(), ref);
}

TEST(Json, FlagRefUnknownNameThrows) {
    json j = "json.nope";
    EXPECT_THROW((void)j.get<comms::FlagRef>(), nlohmann::json::other_error);
}

// A FlagSet travels as a JSON array of names.
TEST(Json, FlagSetRoundTripsAsArrayOfNames) {
    comms::FlagSet s;
    s.insert<JsonFlagOne>();
    s.insert<JsonFlagTwo>();

    const json j = s;
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.dump(), R"(["json.one","json.two"])");

    const auto back = j.get<comms::FlagSet>();
    EXPECT_EQ(back, s);
}

TEST(Json, FlagSetWithUnknownNameThrows) {
    json j = json::array({"json.one", "json.nope"});
    EXPECT_THROW((void)j.get<comms::FlagSet>(), nlohmann::json::other_error);
}

// -- WithPriority / PrioritizedSet -------------------------------------------

// A Prioritized-derived, serializable element for the PrioritizedSet round-trip.
// Identity (and the JSON form) is its z-order, which also is its priority.
struct Layer : comms::Prioritized {
    int z = 0;
    Layer() = default;
    explicit Layer(const int z_) noexcept : z(z_) {}
    [[nodiscard]] int priority() const noexcept override {
        return z;
    }
    bool operator==(const Layer& o) const noexcept {
        return z == o.z;
    }
};

[[maybe_unused]] void to_json(json& j, const Layer& l) {
    j = l.z;
}
[[maybe_unused]] void from_json(const json& j, Layer& l) {
    l = Layer{j.get<int>()};
}

// WithPriority travels as {"priority":N,"value":<T>}.
TEST(Json, WithPriorityRoundTrip) {
    const auto w = comms::with_priority(3, 42);  // WithPriority<int> (composition)
    json j = w;
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j.at("priority").get<int>(), 3);
    EXPECT_EQ(j.at("value").get<int>(), 42);

    const auto back = j.get<comms::WithPriority<int>>();
    EXPECT_EQ(back.priority(), 3);
    EXPECT_EQ(back.value(), 42);
}

// The value reuses T's own hooks (here, Color's hex-string mapping).
TEST(Json, WithPriorityReusesValueHooks) {
    const auto w = comms::with_priority(1, comms::Color::rgb(0x63, 0x66, 0xF1));
    json j = w;
    EXPECT_EQ(j.at("value").get<std::string>(), "#6366f1");
    EXPECT_EQ(j.at("priority").get<int>(), 1);
}

// A PrioritizedSet travels as a JSON array in sorted (ascending-priority) order.
TEST(Json, PrioritizedSetSerializesAsOrderedArray) {
    comms::PrioritizedSet<Layer> s;
    s.insert(Layer{5});
    s.insert(Layer{1});
    s.insert(Layer{3});

    const json j = s;
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.dump(), "[1,3,5]");  // sorted by priority (== z)

    const auto back = j.get<comms::PrioritizedSet<Layer>>();
    EXPECT_EQ(back, s);
}

TEST(Json, PrioritizedSetFromNonArrayThrows) {
    json j = json::object();
    EXPECT_THROW((void)j.get<comms::PrioritizedSet<Layer>>(), nlohmann::json::other_error);
}

}  // namespace
