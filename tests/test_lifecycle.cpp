#include <commons/lifecycle.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using comms::LifecycleClock;
using comms::LifecycleError;
using comms::LifecycleStatus;
using comms::StatusReport;
using comms::StatusTransition;
using comms::StatusTransitionTimeline;

using namespace std::chrono_literals;

// Fixed reference time so durations are deterministic (no now()).
constexpr LifecycleClock::time_point t0{};  // epoch
constexpr auto t1 = t0 + 1000ms;
constexpr auto t2 = t1 + 2000ms;
constexpr auto t3 = t2 + 3000ms;

// -- LifecycleStatus ---------------------------------------------------------

TEST(LifecycleStatus, NameValueEmpty) {
    const LifecycleStatus s{"open"};
    EXPECT_EQ(s.name(), "open");
    EXPECT_EQ(s.value(), "open");
    EXPECT_FALSE(s.empty());
    EXPECT_TRUE(LifecycleStatus{}.empty());
}

TEST(LifecycleStatus, ConvenienceCtors) {
    constexpr std::string_view sv = "closed";
    EXPECT_EQ(LifecycleStatus{sv}.name(), "closed");
    EXPECT_EQ(LifecycleStatus{"done"}.name(), "done");
}

TEST(LifecycleStatus, EqualityAndOrdering) {
    EXPECT_EQ(LifecycleStatus{"a"}, LifecycleStatus{"a"});
    EXPECT_NE(LifecycleStatus{"a"}, LifecycleStatus{"b"});
    EXPECT_LT(LifecycleStatus{"a"}, LifecycleStatus{"b"});
}

TEST(LifecycleStatus, ToStringStreamFormat) {
    const LifecycleStatus s{"active"};
    EXPECT_EQ(comms::to_string(s), "active");
    std::ostringstream os;
    os << s;
    EXPECT_EQ(os.str(), "active");
    EXPECT_EQ(std::format("{}", s), "active");
    EXPECT_EQ(std::format("[{:>6}]", s), "[active]");
}

TEST(LifecycleStatus, HashUsableAsKey) {
    std::unordered_map<LifecycleStatus, int> m;
    m[LifecycleStatus{"x"}] = 1;
    m[LifecycleStatus{"y"}] = 2;
    EXPECT_EQ(m.at(LifecycleStatus{"x"}), 1);
    EXPECT_EQ(m.at(LifecycleStatus{"y"}), 2);
}

// -- StatusTransition --------------------------------------------------------

TEST(StatusTransition, DurationAndPrevious) {
    const StatusTransition<> first{std::nullopt, LifecycleStatus{"open"}, t0, std::nullopt};
    EXPECT_EQ(first.duration(), LifecycleClock::duration::zero());
    EXPECT_FALSE(first.previous_status().has_value());

    const StatusTransition<> second{LifecycleStatus{"open"}, LifecycleStatus{"closed"}, t1, t0};
    EXPECT_EQ(second.duration(), 1000ms);
    ASSERT_TRUE(second.previous_status().has_value());
    EXPECT_EQ(*second.previous_status(), LifecycleStatus{"open"});
}

// -- Timeline construction / transitions -------------------------------------

TEST(Timeline, DefaultEmpty) {
    const StatusTransitionTimeline<> tl;
    EXPECT_TRUE(tl.empty());
    EXPECT_EQ(tl.size(), 0u);
}

TEST(Timeline, InitialStatus) {
    const StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    EXPECT_FALSE(tl.empty());
    EXPECT_EQ(tl.size(), 1u);
    EXPECT_EQ(tl.current_status(), LifecycleStatus{"open"});
    EXPECT_EQ(tl.first_status(), LifecycleStatus{"open"});
    EXPECT_FALSE(tl.previous_status().has_value());
}

TEST(Timeline, TransitionToReturnsAndAdvances) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    const auto [previous, status, timestamp, previous_timestamp] =
        tl.transition_to(LifecycleStatus{"closed"}, t1);
    EXPECT_EQ(status, LifecycleStatus{"closed"});
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ(*previous, LifecycleStatus{"open"});
    EXPECT_EQ(timestamp, t1);
    ASSERT_TRUE(previous_timestamp.has_value());
    EXPECT_EQ(*previous_timestamp, t0);

    EXPECT_EQ(tl.current_status(), LifecycleStatus{"closed"});
    ASSERT_TRUE(tl.previous_status().has_value());
    EXPECT_EQ(*tl.previous_status(), LifecycleStatus{"open"});
    EXPECT_EQ(tl.size(), 2u);
}

TEST(Timeline, TransitionFromEmpty) {
    StatusTransitionTimeline<> tl;
    const auto tr = tl.transition_to(LifecycleStatus{"open"}, t0);
    EXPECT_FALSE(tr.previous.has_value());
    EXPECT_FALSE(tr.previous_timestamp.has_value());
    EXPECT_EQ(tl.current_status(), LifecycleStatus{"open"});
}

// -- Empty-timeline behavior -------------------------------------------------

TEST(Timeline, EmptyThrowsOnStatusAccess) {
    const StatusTransitionTimeline<> tl;
    EXPECT_THROW((void)tl.current_status(), LifecycleError);
    EXPECT_THROW((void)tl.first_status(), LifecycleError);
    EXPECT_THROW((void)tl.current_transition(), LifecycleError);
    EXPECT_THROW((void)tl.first_transition(), LifecycleError);
    // LifecycleError is a comms::Exception.
    EXPECT_THROW((void)tl.current_status(), comms::Exception);
}

TEST(Timeline, EmptyOptionalQueriesReturnNullopt) {
    const StatusTransitionTimeline<> tl;
    EXPECT_FALSE(tl.previous_status().has_value());
    EXPECT_FALSE(tl.status_duration(LifecycleStatus{"open"}).has_value());
    EXPECT_FALSE(tl.status_timestamp(LifecycleStatus{"open"}).has_value());
    EXPECT_FALSE(tl.current_status_duration().has_value());
    EXPECT_FALSE(tl.duration_between(LifecycleStatus{"a"}, LifecycleStatus{"b"}).has_value());
    EXPECT_FALSE(tl.total_duration().has_value());
    EXPECT_TRUE(tl.status_reports().empty());
}

// -- Temporal queries with fixed timestamps ----------------------------------

TEST(Timeline, ContainsAndTimestamps) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    tl.transition_to(LifecycleStatus{"review"}, t1);
    tl.transition_to(LifecycleStatus{"closed"}, t2);

    EXPECT_TRUE(tl.contains(LifecycleStatus{"open"}));
    EXPECT_TRUE(tl.contains(LifecycleStatus{"review"}));
    EXPECT_TRUE(tl.contains(LifecycleStatus{"closed"}));
    EXPECT_FALSE(tl.contains(LifecycleStatus{"missing"}));

    EXPECT_EQ(tl.status_timestamp(LifecycleStatus{"open"}), t0);
    EXPECT_EQ(tl.status_timestamp(LifecycleStatus{"review"}), t1);
    EXPECT_FALSE(tl.status_timestamp(LifecycleStatus{"missing"}).has_value());
}

TEST(Timeline, StatusDurationForPastAndCurrent) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    tl.transition_to(LifecycleStatus{"review"}, t1);  // open: t0..t1 = 1000ms
    tl.transition_to(LifecycleStatus{"closed"}, t2);  // review: t1..t2 = 2000ms

    EXPECT_EQ(tl.status_duration(LifecycleStatus{"open"}), 1000ms);
    EXPECT_EQ(tl.status_duration(LifecycleStatus{"review"}), 2000ms);
    // "closed" is current -> measured to now(), strictly positive.
    const auto cur = tl.status_duration(LifecycleStatus{"closed"});
    ASSERT_TRUE(cur.has_value());
    EXPECT_GE(*cur, LifecycleClock::duration::zero());
    EXPECT_FALSE(tl.status_duration(LifecycleStatus{"missing"}).has_value());
}

TEST(Timeline, DurationBetween) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    tl.transition_to(LifecycleStatus{"review"}, t1);
    tl.transition_to(LifecycleStatus{"closed"}, t2);

    EXPECT_EQ(tl.duration_between(LifecycleStatus{"open"}, LifecycleStatus{"closed"}), 3000ms);
    EXPECT_EQ(tl.duration_between(LifecycleStatus{"review"}, LifecycleStatus{"closed"}), 2000ms);
    EXPECT_FALSE(
        tl.duration_between(LifecycleStatus{"open"}, LifecycleStatus{"missing"}).has_value());
}

TEST(Timeline, CurrentStatusAndTotalDuration) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    tl.transition_to(LifecycleStatus{"closed"}, t1);
    EXPECT_TRUE(tl.current_status_duration().has_value());
    EXPECT_TRUE(tl.total_duration().has_value());
    EXPECT_GE(*tl.total_duration(), 1000ms);  // at least t0..t1, plus now() tail
}

// -- Reports -----------------------------------------------------------------

TEST(Timeline, StatusReports) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    tl.transition_to(LifecycleStatus{"review"}, t1);
    tl.transition_to(LifecycleStatus{"closed"}, t3);

    const auto reports = tl.status_reports();
    ASSERT_EQ(reports.size(), 3u);

    EXPECT_FALSE(reports[0].previous.has_value());
    EXPECT_EQ(reports[0].status, LifecycleStatus{"open"});
    EXPECT_EQ(reports[0].duration, LifecycleClock::duration::zero());
    EXPECT_EQ(reports[0].total_duration, LifecycleClock::duration::zero());

    ASSERT_TRUE(reports[1].previous.has_value());
    EXPECT_EQ(*reports[1].previous, LifecycleStatus{"open"});
    EXPECT_EQ(reports[1].status, LifecycleStatus{"review"});
    EXPECT_EQ(reports[1].duration, 1000ms);        // t0..t1
    EXPECT_EQ(reports[1].total_duration, 1000ms);  // since t0

    EXPECT_EQ(*reports[2].previous, LifecycleStatus{"review"});
    EXPECT_EQ(reports[2].status, LifecycleStatus{"closed"});
    EXPECT_EQ(reports[2].duration, 5000ms);        // t1..t3
    EXPECT_EQ(reports[2].total_duration, 6000ms);  // t0..t3
}

// -- Subscriptions -----------------------------------------------------------

TEST(Timeline, GlobalSubscriberFiresOnEvery) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    std::vector<LifecycleStatus> seen;
    const auto id = tl.subscribe([&](const StatusTransition<>& tr) { seen.push_back(tr.status); });
    EXPECT_EQ(tl.subscriber_count(), 1u);

    tl.transition_to(LifecycleStatus{"review"}, t1);
    tl.transition_to(LifecycleStatus{"closed"}, t2);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], LifecycleStatus{"review"});
    EXPECT_EQ(seen[1], LifecycleStatus{"closed"});

    EXPECT_TRUE(tl.unsubscribe(id));
    EXPECT_FALSE(tl.unsubscribe(id));  // already gone
    tl.transition_to(LifecycleStatus{"reopened"}, t3);
    EXPECT_EQ(seen.size(), 2u);  // no more deliveries
}

TEST(Timeline, StatusFilteredSubscriber) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    int closed_hits = 0;
    tl.subscribe(LifecycleStatus{"closed"}, [&](const StatusTransition<>&) { ++closed_hits; });

    tl.transition_to(LifecycleStatus{"review"}, t1);  // not "closed"
    tl.transition_to(LifecycleStatus{"closed"}, t2);  // matches
    tl.transition_to(LifecycleStatus{"closed"}, t3);  // matches again
    EXPECT_EQ(closed_hits, 2);
}

TEST(Timeline, UnsubscribeAll) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    int hits = 0;
    tl.subscribe([&](const StatusTransition<>&) { ++hits; });
    tl.subscribe([&](const StatusTransition<>&) { ++hits; });
    EXPECT_EQ(tl.subscriber_count(), 2u);

    tl.unsubscribe_all();
    EXPECT_EQ(tl.subscriber_count(), 0u);
    tl.transition_to(LifecycleStatus{"closed"}, t1);
    EXPECT_EQ(hits, 0);
}

// -- Custom status type ------------------------------------------------------

enum class Phase { Created, Running, Done };

TEST(Timeline, CustomStatusType) {
    static_assert(comms::StatusType<Phase>);
    StatusTransitionTimeline<Phase> tl{Phase::Created, t0};
    tl.transition_to(Phase::Running, t1);
    tl.transition_to(Phase::Done, t2);

    EXPECT_EQ(tl.current_status(), Phase::Done);
    EXPECT_TRUE(tl.contains(Phase::Running));
    EXPECT_EQ(tl.status_duration(Phase::Running), 2000ms);                // t1..t2
    EXPECT_EQ(tl.duration_between(Phase::Created, Phase::Done), 3000ms);  // t0..t2

    const auto reports = tl.status_reports();
    ASSERT_EQ(reports.size(), 3u);
    EXPECT_EQ(reports[2].status, Phase::Done);
}

}  // namespace
