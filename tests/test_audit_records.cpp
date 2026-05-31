#include <commons/audit_record.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace {

comms::AuditRecord make_record(std::string username) {
    comms::AuditRecord r;
    r.username = std::move(username);
    return r;
}

TEST(AuditRecords, DefaultCapacityMatchesMacro) {
    const comms::AuditRecords log;
    EXPECT_EQ(log.capacity(), static_cast<std::size_t>(COMMONS_AUDIT_RECORDS_CAPACITY));
    EXPECT_TRUE(log.empty());
}

TEST(AuditRecords, PushDropsOldestBeyondCapacity) {
    comms::AuditRecords log{2};
    log.push(make_record("a"));
    log.push(make_record("b"));
    log.push(make_record("c"));

    ASSERT_EQ(log.size(), 2U);
    // Oldest ("a") dropped; newest two kept in FIFO order.
    EXPECT_EQ(log.front().username, "b");
    EXPECT_EQ(log.back().username, "c");
    EXPECT_EQ(log[0].username, "b");
    EXPECT_EQ(log.at(1).username, "c");
}

TEST(AuditRecords, SetCapacityShrinksDroppingOldest) {
    comms::AuditRecords log{5};
    log.push(make_record("a"));
    log.push(make_record("b"));
    log.push(make_record("c"));
    ASSERT_EQ(log.size(), 3U);

    log.set_capacity(1);
    EXPECT_EQ(log.capacity(), 1U);
    ASSERT_EQ(log.size(), 1U);
    EXPECT_EQ(log.front().username, "c");
}

TEST(AuditRecords, ZeroCapacityRetainsNothing) {
    comms::AuditRecords log{0};
    log.push(make_record("a"));
    EXPECT_TRUE(log.empty());
}

TEST(AuditRecords, ChangeAuditRecordsAliasBehavesTheSame) {
    comms::ChangeAuditRecords<int> log{2};

    comms::ChangeAuditRecord<int> r1;
    r1.username = "a";
    r1.after = 1;
    comms::ChangeAuditRecord<int> r2;
    r2.username = "b";
    r2.after = 2;
    comms::ChangeAuditRecord<int> r3;
    r3.username = "c";
    r3.after = 3;

    log.push(r1);
    log.push(r2);
    log.push(r3);

    ASSERT_EQ(log.size(), 2U);
    EXPECT_EQ(log.front().username, "b");
    EXPECT_EQ(log.back().username, "c");
    ASSERT_TRUE(log.back().after.has_value());
    EXPECT_EQ(*log.back().after, 3);
}

TEST(AuditRecords, ClearEmptiesLog) {
    comms::AuditRecords log{3};
    log.push(make_record("a"));
    log.clear();
    EXPECT_TRUE(log.empty());
}

}  // namespace
