#include <commons/json/lifecycle.hpp>
#include <commons/lifecycle.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>

namespace {

using json = nlohmann::json;

using comms::LifecycleClock;
using comms::LifecycleStatus;
using comms::StatusReport;
using comms::StatusTransition;
using comms::StatusTransitionTimeline;

using namespace std::chrono_literals;

// Millisecond-aligned timestamps so the epoch-millis encoding round-trips
// exactly (no sub-ms truncation).
constexpr LifecycleClock::time_point t0{};  // epoch
constexpr auto t1 = t0 + 1000ms;
constexpr auto t2 = t1 + 3000ms;

std::int64_t millis(const LifecycleClock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

// -- LifecycleStatus ---------------------------------------------------------

TEST(LifecycleJson, StatusIsPlainString) {
    const LifecycleStatus s{"open"};
    const json j = s;
    EXPECT_TRUE(j.is_string());
    EXPECT_EQ(j.get<std::string>(), "open");
    EXPECT_EQ(j.get<LifecycleStatus>(), s);
}

// -- StatusTransition --------------------------------------------------------

TEST(LifecycleJson, TransitionWithoutPrevious) {
    const StatusTransition<> tr{std::nullopt, LifecycleStatus{"open"}, t0, std::nullopt};
    const json j = tr;
    EXPECT_EQ(j["status"], "open");
    EXPECT_EQ(j["timestamp"], millis(t0));
    EXPECT_FALSE(j.contains("previous"));
    EXPECT_FALSE(j.contains("previous_timestamp"));
    EXPECT_EQ(j.get<StatusTransition<>>(), tr);
}

TEST(LifecycleJson, TransitionWithPrevious) {
    const StatusTransition<> tr{LifecycleStatus{"open"}, LifecycleStatus{"closed"}, t1, t0};
    const json j = tr;
    EXPECT_EQ(j["status"], "closed");
    EXPECT_EQ(j["timestamp"], millis(t1));
    EXPECT_EQ(j["previous"], "open");
    EXPECT_EQ(j["previous_timestamp"], millis(t0));
    EXPECT_EQ(j.get<StatusTransition<>>(), tr);
}

// -- StatusReport ------------------------------------------------------------

TEST(LifecycleJson, ReportRoundTrip) {
    const StatusReport<> r{LifecycleStatus{"open"}, LifecycleStatus{"closed"}, 3000ms, 4000ms};
    const json j = r;
    EXPECT_EQ(j["status"], "closed");
    EXPECT_EQ(j["duration"], 3000);
    EXPECT_EQ(j["total_duration"], 4000);
    EXPECT_EQ(j["previous"], "open");
    EXPECT_EQ(j.get<StatusReport<>>(), r);

    const StatusReport<> first{std::nullopt, LifecycleStatus{"open"}, 0ms, 0ms};
    const json jf = first;
    EXPECT_FALSE(jf.contains("previous"));
    EXPECT_EQ(jf.get<StatusReport<>>(), first);
}

// -- Timeline ----------------------------------------------------------------

TEST(LifecycleJson, TimelineIsArrayRoundTrip) {
    StatusTransitionTimeline<> tl{LifecycleStatus{"open"}, t0};
    tl.transition_to(LifecycleStatus{"review"}, t1);
    tl.transition_to(LifecycleStatus{"closed"}, t2);

    const json j = tl;
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 3u);
    EXPECT_EQ(j[0]["status"], "open");
    EXPECT_EQ(j[2]["status"], "closed");

    StatusTransitionTimeline<> restored;
    j.get_to(restored);
    EXPECT_EQ(restored.size(), 3u);
    EXPECT_EQ(restored.first_status(), LifecycleStatus{"open"});
    EXPECT_EQ(restored.current_status(), LifecycleStatus{"closed"});

    // Transitions (statuses, timestamps, links) preserved verbatim.
    EXPECT_EQ(restored.transitions(), tl.transitions());
    EXPECT_EQ(restored.status_timestamp(LifecycleStatus{"review"}), t1);
    EXPECT_EQ(restored.duration_between(LifecycleStatus{"open"}, LifecycleStatus{"closed"}),
              4000ms);
}

TEST(LifecycleJson, EmptyTimelineRoundTrip) {
    const StatusTransitionTimeline<> tl;
    const json j = tl;
    EXPECT_TRUE(j.is_array());
    EXPECT_EQ(j.size(), 0u);

    StatusTransitionTimeline<> restored{LifecycleStatus{"stale"}, t0};
    j.get_to(restored);  // load clears and rebuilds
    EXPECT_TRUE(restored.empty());
}

}  // namespace
