#include <commons/config.hpp>
#include <commons/id.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#if COMMONS_WITH_ULID
#include <ulid/ulid.h>
#endif

namespace {

// Bare tag (no `name`) — exercises the `display_string` fallback that returns
// the bare repr.
struct BareTag {};

// Named tag (has `name`) — `display_string` prefixes it as "events/...".
struct EventsTag {
    [[maybe_unused]] static constexpr std::string_view name = "events";
};

using BareId64 = comms::Uint64Id<BareTag>;
using EventsId64 = comms::Uint64Id<EventsTag>;
using EventsId8 = comms::Uint8Id<EventsTag>;
using EventsIdString = comms::StringId<EventsTag>;

// Macro-generated ids — `UserId`/`OrderId` get their own `*Tag` types with a
// `name` member, and `UserId` is `Uint64Id<UserIdTag>`.
COMMONS_DEFINE_UINT64_ID(UserId, "user");
COMMONS_DEFINE_STRING_ID(OrderId, "order");

// -- construction -----------------------------------------------------------

TEST(Id, ExplicitConstructionFromValue) {
    const BareId64 id{42u};
    EXPECT_EQ(id.value(), 42u);
}

TEST(Id, DefaultConstructionForDefaultInitializableRepr) {
    constexpr BareId64 id{};
    EXPECT_EQ(id.value(), 0u);

    const EventsIdString empty{};
    EXPECT_TRUE(empty.value().empty());
}

TEST(Id, StringIdAcceptsStringView) {
    constexpr std::string_view sv = "abc";
    const EventsIdString id{sv};
    EXPECT_EQ(id.value(), "abc");
}

TEST(Id, ConstructorIsExplicit) {
    static_assert(!std::is_convertible_v<std::uint64_t, BareId64>);
    static_assert(!std::is_convertible_v<std::string, EventsIdString>);
}

// -- value access -----------------------------------------------------------

TEST(Id, ValueAccessorRefQualified) {
    BareId64 id{7u};
    const BareId64& cref = id;
    EXPECT_EQ(cref.value(), 7u);  // const &

    id.value() = 9u;
    EXPECT_EQ(id.value(), 9u);  // &

    EventsIdString s{std::string{"hello"}};
    const auto moved = std::move(s).value();  // &&
    EXPECT_EQ(moved, "hello");
}

TEST(Id, ToUnderlyingReturnsConstRef) {
    const BareId64 id{17u};
    const auto& u = comms::to_underlying(id);
    EXPECT_EQ(u, 17u);
    static_assert(std::is_same_v<decltype(comms::to_underlying(id)), const std::uint64_t&>);
}

// -- comparison -------------------------------------------------------------

TEST(Id, EqualityAndOrdering) {
    EXPECT_EQ(BareId64{3u}, BareId64{3u});
    EXPECT_NE(BareId64{3u}, BareId64{4u});
    EXPECT_LT(BareId64{3u}, BareId64{4u});
    EXPECT_GE(BareId64{4u}, BareId64{4u});

    EXPECT_LT(EventsIdString{"a"}, EventsIdString{"b"});
}

// Different tags produce unrelated types — `BareId64` and `EventsId64` should
// not be comparable.
TEST(Id, DifferentTagsAreUnrelatedTypes) {
    static_assert(!std::is_same_v<BareId64, EventsId64>);
    static_assert(!std::equality_comparable_with<BareId64, EventsId64>);
}

// -- to_string + display_string ---------------------------------------------

TEST(Id, ToStringUintDelegatesToStdToString) {
    EXPECT_EQ(comms::to_string(BareId64{42u}), "42");
    EXPECT_EQ(comms::to_string(EventsId8{std::uint8_t{255}}), "255");
}

TEST(Id, ToStringStringEchoesValue) {
    EXPECT_EQ(comms::to_string(EventsIdString{"abc"}), "abc");
}

TEST(Id, DisplayStringFallsBackToBareReprWithoutName) {
    EXPECT_EQ(comms::display_string(BareId64{42u}), "42");
}

TEST(Id, DisplayStringPrefixesWithTagName) {
    EXPECT_EQ(comms::display_string(EventsId64{42u}), "events/42");
    EXPECT_EQ(comms::display_string(EventsIdString{"x"}), "events/x");
}

// -- operator<< + std::format -----------------------------------------------

TEST(Id, OperatorOstreamWritesToString) {
    std::ostringstream out;
    out << BareId64{42u};
    EXPECT_EQ(out.str(), "42");
}

TEST(Id, StdFormatDefaultSpec) {
    EXPECT_EQ(std::format("{}", BareId64{42u}), "42");
    EXPECT_EQ(std::format("{}", EventsIdString{"hello"}), "hello");
}

TEST(Id, StdFormatInheritsReprFormatter) {
    // The inherited formatter routes through `std::formatter<uint64_t>`, so any
    // spec the underlying type accepts works transparently.
    EXPECT_EQ(std::format("{:#x}", BareId64{0xABu}), "0xab");
    EXPECT_EQ(std::format("{:5}", BareId64{7u}), "    7");
}

// -- std::hash --------------------------------------------------------------

TEST(Id, HashAgreesWithReprHash) {
    constexpr std::uint64_t k = 12345;
    EXPECT_EQ(std::hash<BareId64>{}(BareId64{k}), std::hash<std::uint64_t>{}(k));

    constexpr std::string s = "abc";
    EXPECT_EQ(std::hash<EventsIdString>{}(EventsIdString{s}), std::hash<std::string>{}(s));
}

// -- reflection -------------------------------------------------------------

TEST(Id, IsIdReflection) {
    static_assert(comms::is_id_v<BareId64>);
    static_assert(comms::is_id_v<EventsIdString>);
    static_assert(!comms::is_id_v<std::uint64_t>);
    static_assert(!comms::is_id_v<std::string>);
    static_assert(comms::IdType<UserId>);
    static_assert(!comms::IdType<std::uint64_t>);
}

// -- macros -----------------------------------------------------------------

TEST(Id, DefineUint64IdMacro) {
    const UserId u{99u};
    EXPECT_EQ(comms::to_string(u), "99");
    EXPECT_EQ(comms::display_string(u), "user/99");
    EXPECT_EQ(UserIdTag::name, "user");
    static_assert(std::is_same_v<UserId, comms::Uint64Id<UserIdTag>>);
}

TEST(Id, DefineStringIdMacro) {
    const OrderId o{std::string{"o-1"}};
    EXPECT_EQ(comms::to_string(o), "o-1");
    EXPECT_EQ(comms::display_string(o), "order/o-1");
    static_assert(std::is_same_v<OrderId, comms::StringId<OrderIdTag>>);
}

// -- ULID-gated assertions --------------------------------------------------

#if COMMONS_WITH_ULID

COMMONS_DEFINE_ULID_ID(EventId, "event");

TEST(Id, UlidParseToStringRoundTrip) {
    // A known-valid 26-char Crockford-base32 ULID.
    const std::string text = "01H8XGQZ8E4Q7M5GZP9X8R3D7K";
    const auto parsed = ::ulid::Ulid::from_string(text);
    ASSERT_TRUE(parsed.has_value());

    const EventId id{*parsed};
    EXPECT_EQ(comms::to_string(id), text);
    EXPECT_EQ(std::format("{}", id), text);
}

TEST(Id, UlidDisplayStringPrefixesWithTagName) {
    const auto u = ::ulid::Ulid::from_string("01H8XGQZ8E4Q7M5GZP9X8R3D7K").value_or(::ulid::Ulid{});
    const EventId id{u};
    EXPECT_EQ(comms::display_string(id), "event/" + u.string());
}

#endif

}  // namespace
