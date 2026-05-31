#include <commons/commons.hpp>
#include <commons/json.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
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
    const json j = "+42";
    EXPECT_EQ(j.get<comms::i128>(), comms::i128{42});
    const json neg = "-42";
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
    constexpr std::optional<int> opt = 7;
    const json j = opt;
    EXPECT_TRUE(j.is_number());
    EXPECT_EQ(j.dump(), "7");
    EXPECT_EQ(j.get<std::optional<int>>(), opt);
}

TEST(Json, OptionalNulloptSerializesAsNull) {
    constexpr std::optional<int> opt = std::nullopt;
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
    constexpr std::optional<comms::Color> opt = comms::Color::parse("#6366f1");
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

// -- SemVer ------------------------------------------------------------------
// Travels as its canonical version string.

TEST(Json, SemVerRoundTrip) {
    const auto v = comms::SemVer::parse("1.2.3-rc.1+build.2").value();
    const json j = v;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "1.2.3-rc.1+build.2");
    EXPECT_EQ(j.get<comms::SemVer>(), v);
}

TEST(Json, SemVerInvalidThrows) {
    json j = "1.2.3-01";  // numeric prerelease with a leading zero
    EXPECT_THROW((void)j.get<comms::SemVer>(), nlohmann::json::other_error);
}

// -- VersionConstraint -------------------------------------------------------
// Travels as its raw range string.

TEST(Json, VersionConstraintRoundTrip) {
    const auto c = comms::VersionConstraint::parse(">=1.2.0 <2.0.0");
    const json j = c;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), ">=1.2.0 <2.0.0");
    EXPECT_EQ(j.get<comms::VersionConstraint>(), c);
}

TEST(Json, VersionConstraintInvalidThrows) {
    json j = "^x.y";
    EXPECT_THROW((void)j.get<comms::VersionConstraint>(), nlohmann::json::other_error);
}

// -- Id<Tag, Repr> -----------------------------------------------------------
// Travels as the inner Repr's natural JSON — number for the uint reprs, string
// for std::string, and (under COMMONS_WITH_ULID) the ULID string for ulid::Ulid.

COMMONS_DEFINE_UINT64_ID(JsonUserId, "json.user");
COMMONS_DEFINE_STRING_ID(JsonOrderId, "json.order");

TEST(Json, IdUint64TravelsAsJsonNumber) {
    const JsonUserId u{42u};
    const json j = u;
    EXPECT_TRUE(j.is_number_unsigned());
    EXPECT_EQ(j.get<std::uint64_t>(), 42u);
    EXPECT_EQ(j.get<JsonUserId>(), u);
}

TEST(Json, IdStringTravelsAsJsonString) {
    const JsonOrderId o{std::string{"o-1"}};
    const json j = o;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "o-1");
    EXPECT_EQ(j.get<JsonOrderId>(), o);
}

#if COMMONS_WITH_ULID

COMMONS_DEFINE_ULID_ID(JsonEventId, "json.event");

TEST(Json, IdUlidTravelsAsJsonString) {
    const auto parsed = ::ulid::Ulid::from_string("01H8XGQZ8E4Q7M5GZP9X8R3D7K");
    ASSERT_TRUE(parsed.has_value());
    const JsonEventId e{*parsed};
    const json j = e;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "01H8XGQZ8E4Q7M5GZP9X8R3D7K");
    EXPECT_EQ(j.get<JsonEventId>(), e);
}

#endif

// -- IOrigin / OriginPtr -----------------------------------------------------
// An origin travels as {"kind", ...fields}; OriginPtr null ⇄ JSON null; an
// unrecognized kind throws.

TEST(Json, OriginCoreRoundTrip) {
    const comms::OriginPtr o = std::make_unique<comms::CoreOrigin>();
    const json j = o;
    EXPECT_TRUE(j.is_object());
    EXPECT_EQ(j.at("kind").get<std::string>(), "core");

    const auto back = j.get<comms::OriginPtr>();
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->kind(), "core");
}

TEST(Json, OriginExternalRoundTripsSource) {
    const comms::OriginPtr o = std::make_unique<comms::ExternalOrigin>("npm");
    const json j = o;
    EXPECT_EQ(j.at("kind").get<std::string>(), "external");
    EXPECT_EQ(j.at("source").get<std::string>(), "npm");

    const auto back = j.get<comms::OriginPtr>();
    auto* ext = dynamic_cast<comms::ExternalOrigin*>(back.get());
    ASSERT_NE(ext, nullptr);
    EXPECT_EQ(ext->source, "npm");
}

TEST(Json, OriginPtrNullIsJsonNull) {
    constexpr comms::OriginPtr o;  // null
    const json j = o;
    EXPECT_TRUE(j.is_null());
    EXPECT_EQ(j.get<comms::OriginPtr>(), nullptr);
}

TEST(Json, OriginPtrUnknownKindThrows) {
    const auto j = json{{"kind", "mystery"}};
    EXPECT_THROW((void)j.get<comms::OriginPtr>(), nlohmann::json::other_error);
}

TEST(Json, OriginPtrMissingKindThrows) {
    const json j = json::object();
    EXPECT_THROW((void)j.get<comms::OriginPtr>(), nlohmann::json::other_error);
}

// -- Reason / FailureReason --------------------------------------------------

// A sub-reason with an extra field, round-tripped through ReasonPtr by
// overriding the virtual write_json/read_json hooks (gated in reason.hpp).
class RetryAfterReason final : public comms::FailureReasonKind<"retry_after", RetryAfterReason> {
public:
    using comms::FailureReasonKind<"retry_after", RetryAfterReason>::ReasonKind;
    int retry_after_ms = 0;

    void write_json(nlohmann::json& j) const override {
        comms::IReason::write_json(j);  // kind/code/message/created_at
        j["retry_after_ms"] = retry_after_ms;
    }
    void read_json(const nlohmann::json& j) override {
        comms::IReason::read_json(j);
        if (const auto it = j.find("retry_after_ms"); it != j.end() && !it->is_null()) {
            it->get_to(retry_after_ms);
        }
    }
};
COMMONS_REGISTER_REASON(RetryAfterReason);

TEST(Json, SubReasonCustomFieldRoundTripsThroughPointers) {
    auto r = comms::make_failure_reason<RetryAfterReason>(429, "slow down");
    static_cast<RetryAfterReason&>(*r).retry_after_ms = 1500;

    // Serialize through FailureReasonPtr — the virtual write_json adds the field.
    const json j = r;
    EXPECT_EQ(j.at("kind").get<std::string>(), "retry_after");
    EXPECT_EQ(j.at("retry_after_ms").get<int>(), 1500);

    // Read back through the base ReasonPtr: the registry rebuilds the concrete
    // kind and read_json restores the custom field.
    const auto back = j.get<comms::ReasonPtr>();
    ASSERT_NE(back, nullptr);
    auto* typed = dynamic_cast<RetryAfterReason*>(back.get());
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->code, 429);
    EXPECT_EQ(typed->message, "slow down");
    EXPECT_EQ(typed->retry_after_ms, 1500);
}

TEST(Json, ReasonPtrRoundTripsFields) {
    auto r = comms::make_reason(418, "teapot");
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(r->created_at.time_since_epoch())
            .count();
    const json j = r;
    EXPECT_EQ(j.at("kind").get<std::string>(), "generic");
    EXPECT_EQ(j.at("code").get<int>(), 418);
    EXPECT_EQ(j.at("message").get<std::string>(), "teapot");
    EXPECT_EQ(j.at("created_at").get<std::int64_t>(), ms);

    const auto back = j.get<comms::ReasonPtr>();
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->kind(), "generic");
    EXPECT_EQ(back->code, 418);
    EXPECT_EQ(back->message, "teapot");
    // The wire format is epoch-milliseconds, so the timestamp round-trips to
    // millisecond precision (sub-millisecond ticks are truncated).
    const auto truncated = comms::ReasonClock::time_point{
        std::chrono::duration_cast<std::chrono::milliseconds>(r->created_at.time_since_epoch())};
    EXPECT_EQ(back->created_at, truncated);
}

TEST(Json, ReasonMetadataOmittedWhenEmpty) {
    // The default (empty) metadata keeps the reason's compact wire shape.
    const auto r = comms::make_reason(1, "x");
    const json j = r;
    EXPECT_FALSE(j.contains("metadata"));
}

TEST(Json, ReasonMetadataRoundTrips) {
    auto r = comms::make_reason(429, "slow down");
    r->metadata["attempt"] = comms::md::Value{3};
    r->metadata["endpoint"] = comms::md::Value{"/v1/items"};

    const json j = r;
    ASSERT_TRUE(j.contains("metadata"));
    EXPECT_EQ(j.at("metadata").at("attempt").get<int>(), 3);
    EXPECT_EQ(j.at("metadata").at("endpoint").get<std::string>(), "/v1/items");

    const auto back = j.get<comms::ReasonPtr>();
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->metadata.require("attempt").as_int(), 3);
    EXPECT_EQ(back->metadata.require_string("endpoint"), "/v1/items");
}

TEST(Json, FailureReasonPtrRoundTrips) {
    const comms::FailureReasonPtr r =
        comms::make_failure_reason<comms::UnknownFailureReason>(7, "boom");
    const json j = r;
    EXPECT_EQ(j.at("kind").get<std::string>(), "unknown_failure");

    const auto back = j.get<comms::FailureReasonPtr>();
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->kind(), "unknown_failure");
    EXPECT_EQ(back->code, 7);
}

TEST(Json, FailureReasonPtrRejectsPlainReasonKind) {
    // "generic" is a reason but not a failure reason: deserializing into a
    // FailureReasonPtr must fail.
    const auto j = json{{"kind", "generic"}, {"code", 0}, {"message", "x"}};
    EXPECT_THROW((void)j.get<comms::FailureReasonPtr>(), nlohmann::json::other_error);
}

TEST(Json, ReasonPtrNullIsJsonNull) {
    constexpr comms::ReasonPtr r;  // null
    const json j = r;
    EXPECT_TRUE(j.is_null());
    EXPECT_EQ(j.get<comms::ReasonPtr>(), nullptr);
}

TEST(Json, ReasonPtrUnknownKindThrows) {
    const auto j = json{{"kind", "mystery"}};
    EXPECT_THROW((void)j.get<comms::ReasonPtr>(), nlohmann::json::other_error);
}

TEST(Json, ReasonPtrMissingKindThrows) {
    const json j = json::object();
    EXPECT_THROW((void)j.get<comms::ReasonPtr>(), nlohmann::json::other_error);
}

// -- AuditRecord ------------------------------------------------------------

COMMONS_DEFINE_UINT64_ID(JsonAuditOrderId, "audit.order");
COMMONS_DEFINE_STRING_ID(JsonAuditTenantId, "audit.tenant");

// A ms-aligned timestamp so the millisecond JSON encoding round-trips exactly.
comms::AuditClock::time_point audit_ts() {
    return comms::AuditClock::time_point{std::chrono::milliseconds{1'700'000'000'123}};
}

TEST(Json, AuditRecordFullRoundTrip) {
    comms::AuditRecord r;
    r.username = "alice";
    r.timestamp = audit_ts();
    r.ip = "192.0.2.1";
    r.user_agent = "curl/8.0";
    r.set_session_id(JsonAuditOrderId{42U});
    r.add_related_id("order", JsonAuditOrderId{7U});
    r.metadata["attempt"] = comms::md::Value{3};

    const json j = r;
    EXPECT_EQ(j.at("username").get<std::string>(), "alice");
    EXPECT_EQ(j.at("session_id").get<std::string>(), "42");

    const auto back = j.get<comms::AuditRecord>();
    EXPECT_EQ(back, r);
}

TEST(Json, AuditRecordOmitsAbsentFields) {
    comms::AuditRecord r;
    r.username = "bob";
    r.timestamp = audit_ts();

    const json j = r;
    EXPECT_TRUE(j.contains("username"));
    EXPECT_TRUE(j.contains("timestamp"));
    EXPECT_FALSE(j.contains("ip"));
    EXPECT_FALSE(j.contains("user_agent"));
    EXPECT_FALSE(j.contains("session_id"));
    EXPECT_FALSE(j.contains("related_ids"));
    EXPECT_FALSE(j.contains("metadata"));

    const auto back = j.get<comms::AuditRecord>();
    EXPECT_EQ(back, r);
}

TEST(Json, AuditRecordTimestampMillis) {
    comms::AuditRecord r;
    r.username = "carol";
    r.timestamp = audit_ts();

    const json j = r;
    EXPECT_EQ(j.at("timestamp").get<std::int64_t>(), 1'700'000'000'123);
}

TEST(Json, AuditRecordRelatedIdsRoundTrip) {
    comms::AuditRecord r;
    r.username = "dave";
    r.timestamp = audit_ts();
    r.add_related_id("order", JsonAuditOrderId{7U});
    r.add_related_id("tenant", JsonAuditTenantId{"acme"});

    const json j = r;
    ASSERT_TRUE(j.contains("related_ids"));
    EXPECT_EQ(j.at("related_ids").at("order").get<std::string>(), "7");
    EXPECT_EQ(j.at("related_ids").at("tenant").get<std::string>(), "acme");

    const auto back = j.get<comms::AuditRecord>();
    EXPECT_EQ(back.related_ids, r.related_ids);
}

TEST(Json, AuditRecordMetadataRoundTrip) {
    comms::AuditRecord r;
    r.username = "erin";
    r.timestamp = audit_ts();
    r.metadata["endpoint"] = comms::md::Value{"/v1/items"};
    r.metadata["attempt"] = comms::md::Value{3};

    const json j = r;
    ASSERT_TRUE(j.contains("metadata"));

    const auto back = j.get<comms::AuditRecord>();
    EXPECT_EQ(back.metadata, r.metadata);
}

TEST(Json, AuditRecordNullFieldBecomesNullopt) {
    const auto j = json{{"username", "frank"}, {"timestamp", 1'700'000'000'123}, {"ip", nullptr}};
    const auto r = j.get<comms::AuditRecord>();
    EXPECT_EQ(r.username, "frank");
    EXPECT_FALSE(r.ip.has_value());
}

// -- ChangeAuditRecord ------------------------------------------------------

TEST(Json, ChangeAuditRecordRoundTripBothPresent) {
    comms::ChangeAuditRecord<int> r;
    r.username = "alice";
    r.timestamp = audit_ts();
    r.before = 10;
    r.after = 20;

    const json j = r;
    EXPECT_EQ(j.at("username").get<std::string>(), "alice");  // base field co-serializes
    EXPECT_EQ(j.at("before").get<int>(), 10);
    EXPECT_EQ(j.at("after").get<int>(), 20);

    const auto back = j.get<comms::ChangeAuditRecord<int>>();
    EXPECT_EQ(back, r);
}

TEST(Json, ChangeAuditRecordOmitsAbsentBeforeAfter) {
    comms::ChangeAuditRecord<int> r;  // create: only `after`
    r.username = "bob";
    r.timestamp = audit_ts();
    r.after = 5;

    const json j = r;
    EXPECT_FALSE(j.contains("before"));
    ASSERT_TRUE(j.contains("after"));

    const auto back = j.get<comms::ChangeAuditRecord<int>>();
    EXPECT_EQ(back, r);
}

TEST(Json, ChangeAuditRecordStructValueRoundTrip) {
    comms::ChangeAuditRecord<std::string> r;
    r.username = "carol";
    r.timestamp = audit_ts();
    r.before = std::string{"old"};
    r.after = std::string{"new"};

    const json j = r;
    const auto back = j.get<comms::ChangeAuditRecord<std::string>>();
    EXPECT_EQ(back, r);
}

// -- AuditLog ---------------------------------------------------------------

TEST(Json, AuditRecordsArrayRoundTrip) {
    comms::AuditRecords log{5};  // cap above the element count → exact round-trip
    comms::AuditRecord a;
    a.username = "a";
    a.timestamp = audit_ts();
    comms::AuditRecord b;
    b.username = "b";
    b.timestamp = audit_ts();
    log.push(a);
    log.push(b);

    const json j = log;
    ASSERT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 2U);

    const auto back = j.get<comms::AuditRecords>();
    EXPECT_EQ(back.records(), log.records());
}

TEST(Json, ChangeAuditRecordsArrayRoundTrip) {
    comms::ChangeAuditRecords<int> log{5};
    comms::ChangeAuditRecord<int> r;
    r.username = "a";
    r.timestamp = audit_ts();
    r.after = 1;
    log.push(r);

    const json j = log;
    ASSERT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 1U);

    const auto back = j.get<comms::ChangeAuditRecords<int>>();
    EXPECT_EQ(back.records(), log.records());
}

}  // namespace
