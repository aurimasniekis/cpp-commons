#include <commons/audit_record.hpp>
#include <commons/config.hpp>
#include <commons/id.hpp>
#include <commons/identity.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

#if COMMONS_WITH_ULID
#include <ulid/ulid.h>
#endif

namespace {

COMMONS_DEFINE_UINT64_ID(OrderId, "order");
COMMONS_DEFINE_STRING_ID(TenantId, "tenant");
#if COMMONS_WITH_ULID
COMMONS_DEFINE_ULID_ID(EventId, "event");
#endif

TEST(AuditRecord, Defaults) {
    const comms::AuditRecord r;
    // identity defaults to NoIdentity (never null).
    ASSERT_NE(r.identity, nullptr);
    EXPECT_EQ(r.identity->kind(), "none");
    EXPECT_FALSE(r.ip.has_value());
    EXPECT_FALSE(r.user_agent.has_value());
    EXPECT_FALSE(r.session_id.has_value());
    EXPECT_TRUE(r.related_ids.empty());
    EXPECT_TRUE(r.metadata.empty());
    // timestamp defaults to now(), which is far past the clock epoch.
    EXPECT_GT(r.timestamp, comms::AuditClock::time_point{});
}

TEST(AuditRecord, DirectFieldAssignment) {
    comms::AuditRecord r;
    r.set_identity(comms::make_identity<comms::UserIdentity>("alice"));
    r.ip = "192.0.2.1";
    r.user_agent = "curl/8.0";
    ASSERT_NE(r.identity, nullptr);
    EXPECT_EQ(r.identity->kind(), "user");
    EXPECT_EQ(r.identity->value, "alice");
    ASSERT_TRUE(r.ip.has_value());
    EXPECT_EQ(*r.ip, "192.0.2.1");
    ASSERT_TRUE(r.user_agent.has_value());
    EXPECT_EQ(*r.user_agent, "curl/8.0");
}

TEST(AuditRecord, SetSessionIdCapturesString) {
    comms::AuditRecord r;
    r.set_session_id(OrderId{42U});
    ASSERT_TRUE(r.session_id.has_value());
    EXPECT_EQ(*r.session_id, "42");

    r.set_session_id(TenantId{"acme"});
    ASSERT_TRUE(r.session_id.has_value());
    EXPECT_EQ(*r.session_id, "acme");
}

#if COMMONS_WITH_ULID
TEST(AuditRecord, SetSessionIdFromUlid) {
    const std::string text = "01H8XGQZ8E4Q7M5GZP9X8R3D7K";
    const auto parsed = ::ulid::Ulid::from_string(text);
    ASSERT_TRUE(parsed.has_value());

    comms::AuditRecord r;
    r.set_session_id(EventId{*parsed});
    ASSERT_TRUE(r.session_id.has_value());
    EXPECT_EQ(*r.session_id, text);
}
#endif

TEST(AuditRecord, AddRelatedIdAndOverwrite) {
    comms::AuditRecord r;
    r.add_related_id("order", OrderId{7U});
    r.add_related_id("tenant", TenantId{"acme"});
    EXPECT_EQ(r.related_ids.size(), 2U);
    EXPECT_EQ(r.related_ids.at("order"), "7");
    EXPECT_EQ(r.related_ids.at("tenant"), "acme");

    // Re-adding a name overwrites (insert_or_assign).
    r.add_related_id("order", OrderId{99U});
    EXPECT_EQ(r.related_ids.size(), 2U);
    EXPECT_EQ(r.related_ids.at("order"), "99");
}

TEST(AuditRecord, Equality) {
    constexpr comms::AuditClock::time_point ts{std::chrono::milliseconds{1'700'000'000'000}};

    comms::AuditRecord a;
    a.set_identity(comms::make_identity<comms::UserIdentity>("alice"));
    a.timestamp = ts;
    a.ip = "192.0.2.1";

    comms::AuditRecord b = a;  // copy deep-clones the identity
    EXPECT_EQ(a, b);

    b.set_identity(comms::make_identity<comms::UserIdentity>("bob"));
    EXPECT_NE(a, b);

    b = a;
    b.ip = "192.0.2.2";
    EXPECT_NE(a, b);

    b = a;
    b.timestamp = comms::AuditClock::time_point{std::chrono::milliseconds{1}};
    EXPECT_NE(a, b);

    b = a;
    b.add_related_id("order", OrderId{1U});
    EXPECT_NE(a, b);
}

}  // namespace
