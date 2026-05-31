#include <commons/audit_record.hpp>
#include <commons/id.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

COMMONS_DEFINE_UINT64_ID(AccountId, "account");

struct Money {
    [[maybe_unused]] int cents = 0;
    [[maybe_unused]] std::string currency;
    [[nodiscard]] bool operator==(const Money&) const = default;
};

TEST(ChangeAuditRecord, InheritsBaseFieldsAndHelpers) {
    comms::ChangeAuditRecord<int> r;
    r.username = "alice";
    r.set_session_id(AccountId{5U});
    r.add_related_id("account", AccountId{5U});

    EXPECT_EQ(r.username, "alice");
    ASSERT_TRUE(r.session_id.has_value());
    EXPECT_EQ(*r.session_id, "5");
    EXPECT_EQ(r.related_ids.at("account"), "5");

    // before/after default to nullopt.
    EXPECT_FALSE(r.before.has_value());
    EXPECT_FALSE(r.after.has_value());
}

TEST(ChangeAuditRecord, CreateUpdateDelete) {
    // Create: after set, before absent.
    comms::ChangeAuditRecord<int> create;
    create.after = 10;
    EXPECT_FALSE(create.before.has_value());
    ASSERT_TRUE(create.after.has_value());
    EXPECT_EQ(*create.after, 10);

    // Update: both set.
    comms::ChangeAuditRecord<int> update;
    update.before = 10;
    update.after = 20;
    ASSERT_TRUE(update.before.has_value());
    ASSERT_TRUE(update.after.has_value());
    EXPECT_EQ(*update.before, 10);
    EXPECT_EQ(*update.after, 20);

    // Delete: before set, after absent.
    comms::ChangeAuditRecord<int> del;
    del.before = 20;
    ASSERT_TRUE(del.before.has_value());
    EXPECT_FALSE(del.after.has_value());
}

TEST(ChangeAuditRecord, EqualityOverBaseAndValues) {
    comms::ChangeAuditRecord<Money> a;
    a.username = "alice";
    a.before = Money{100, "USD"};
    a.after = Money{200, "USD"};

    comms::ChangeAuditRecord<Money> b = a;
    EXPECT_EQ(a, b);

    // Differ in a base field.
    b.username = "bob";
    EXPECT_NE(a, b);

    // Differ in after.
    b = a;
    b.after = Money{300, "USD"};
    EXPECT_NE(a, b);

    // Differ in before (present vs absent).
    b = a;
    b.before.reset();
    EXPECT_NE(a, b);
}

}  // namespace
