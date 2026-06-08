// Tour of the comms::Lifecycle family: the status history of an object — the
// string-valued LifecycleStatus, a chain of StatusTransition<T> events, per-status
// StatusReport<T> summaries, and the thread-safe StatusTransitionTimeline<T> that
// owns the history and notifies subscribers. JSON-free, so it builds in the base
// library. Fixed (non-now()) timestamps are used for deterministic durations.

#include <commons/lifecycle.hpp>

#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

int main() {
    namespace c = comms;

    // Fixed reference times so the printed durations are reproducible.
    constexpr c::LifecycleClock::time_point t0{};  // epoch
    constexpr auto t1 = t0 + 1000ms;
    constexpr auto t2 = t1 + 2000ms;

    // Start a timeline at an initial status (timestamped t0), then transition.
    // The default status type is LifecycleStatus (a thin named string value).
    c::StatusTransitionTimeline<> tl{c::LifecycleStatus{"open"}, t0};

    // Subscribe before transitioning: a global listener fires on every change,
    // and a status-filtered listener fires only for transitions into "closed".
    tl.subscribe([](const c::StatusTransition<>& tr) {
        std::cout << "  [any]    -> " << tr.status << "\n";
    });
    tl.subscribe(c::LifecycleStatus{"closed"},
                 [](const c::StatusTransition<>&) { std::cout << "  [closed] fired\n"; });

    std::cout << "transitions:\n";
    tl.transition_to(c::LifecycleStatus{"review"}, t1);
    tl.transition_to(c::LifecycleStatus{"closed"}, t2);

    // Status queries (these throw comms::LifecycleError on an empty timeline).
    std::cout << "current      : " << tl.current_status() << "\n";
    std::cout << "first        : " << tl.first_status() << "\n";
    std::cout << "previous     : " << tl.previous_status().value_or(c::LifecycleStatus{"(none)"})
              << "\n";
    std::cout << "contains open: " << std::boolalpha << tl.contains(c::LifecycleStatus{"open"})
              << "\n";

    // Temporal queries return std::optional; durations print as milliseconds.
    const auto ms = [](auto d) { return std::chrono::duration_cast<std::chrono::milliseconds>(d); };
    std::cout << "in 'open'    : " << ms(*tl.status_duration(c::LifecycleStatus{"open"})) << "\n";
    std::cout << "open->closed : "
              << ms(*tl.duration_between(c::LifecycleStatus{"open"}, c::LifecycleStatus{"closed"}))
              << "\n";

    // One report per transition: previous, status, time in it, total since start.
    std::cout << "reports:\n";
    for (const auto& r : tl.status_reports()) {
        std::cout << "  " << r.status << ": " << ms(r.duration) << " (total "
                  << ms(r.total_duration) << ")\n";
    }

    // T is generic — any default-constructible, copyable, equality-comparable
    // type works as a status, e.g. a plain enum.
    enum class Phase { Created, Running, Done };
    c::StatusTransitionTimeline<Phase> phases{Phase::Created, t0};
    phases.transition_to(Phase::Running, t1);
    phases.transition_to(Phase::Done, t2);
    std::cout << "custom status: " << phases.size() << " phases, current is "
              << static_cast<int>(phases.current_status()) << "\n";

    return 0;
}
