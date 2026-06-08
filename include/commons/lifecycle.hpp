#pragma once

/// @file
/// @brief The `comms::Lifecycle` family — the **status history** of a domain
///        object: a status, a chain of transitions, per-status reports, and a
///        thread-safe timeline that owns the history and notifies subscribers.
///
/// It follows the value-and-template style of `audit_record.hpp` / `id.hpp`
/// (not the registry/open-set style of `reason.hpp`): the status `T` is an
/// ordinary generic value type, not a polymorphic open set, so there is **no
/// registry / `kind()` / CRTP machinery**.
///
/// Types:
///   - `LifecycleStatus` — the built-in string-valued status.
///   - `StatusTransition<T>` — a self-contained transition event: the new
///     `status`, its `timestamp`, and the `previous` status + its timestamp
///     (each `std::optional`, absent for the first transition). Self-contained
///     rather than a raw-pointer linked list, so it stays valid across vector
///     reallocation.
///   - `StatusReport<T>` — a per-status period summary: time spent in this
///     status plus the total since the timeline start.
///   - `StatusTransitionTimeline<T>` — owns the transition history, answers
///     temporal queries, and notifies subscribers on each transition. Holds an
///     internal `std::mutex`, so it is **non-copyable / non-movable** (the small
///     value types above are unaffected).
///
/// `T` defaults to `LifecycleStatus` and must satisfy `StatusType` (default-
/// constructible, copyable, equality-comparable) — `LifecycleStatus` qualifies,
/// and a caller may use their own type instead.
///
/// JSON (in `commons/json/lifecycle.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// a `LifecycleStatus` is a plain JSON string; `StatusTransition` / `StatusReport`
/// are objects; a timeline is a JSON array of transitions. Timestamps and
/// durations encode as epoch / count **milliseconds** (like `audit_record.hpp`,
/// with the same sub-millisecond truncation caveat). Subscribers are transient
/// and never serialized.
///
/// The timeline is **unbounded**: every transition is kept for the life of the
/// timeline.

#include <commons/exception.hpp>

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace comms {

/// The clock backing lifecycle timestamps. Mirrors `comms::AuditClock` /
/// `comms::ReasonClock`.
using LifecycleClock = std::chrono::system_clock;

/// Thrown when a temporal/status operation is invalid for the current timeline
/// state — most notably querying the current/first status of an empty timeline.
/// Rooted at `comms::Exception`, like `comms::MetadataError`.
class LifecycleError : public Exception {
public:
    using Exception::Exception;
};

// -- LifecycleStatus ---------------------------------------------------------

/// The built-in string-valued status. A thin named value: two statuses are
/// equal iff their names are equal, and they order lexicographically by name.
class LifecycleStatus {
    std::string name_;

public:
    LifecycleStatus() = default;
    explicit LifecycleStatus(std::string name) : name_(std::move(name)) {}
    explicit LifecycleStatus(const std::string_view name) : name_(name) {}
    explicit LifecycleStatus(const char* name) : name_(name) {}

    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }
    /// Alias for `name()`.
    [[nodiscard]] const std::string& value() const noexcept {
        return name_;
    }
    [[nodiscard]] bool empty() const noexcept {
        return name_.empty();
    }

    [[nodiscard]] bool operator==(const LifecycleStatus&) const = default;
    [[nodiscard]] auto operator<=>(const LifecycleStatus&) const = default;
};

[[nodiscard]] inline std::string to_string(const LifecycleStatus& s) {
    return s.name();
}

inline std::ostream& operator<<(std::ostream& os, const LifecycleStatus& s) {
    return os << s.name();
}

// -- StatusType --------------------------------------------------------------

/// What a status `T` must satisfy to flow through the templated lifecycle types:
/// default-constructible (so the value types and JSON `get<>()` work), copyable,
/// and equality-comparable (for `contains` / per-status lookups).
template <class T>
concept StatusType =
    std::default_initializable<T> && std::copyable<T> && std::equality_comparable<T>;

// -- StatusTransition --------------------------------------------------------

/// A single, self-contained transition event. `previous` / `previous_timestamp`
/// are absent for the very first transition; otherwise they carry the prior
/// status and its timestamp so `duration()` works without a linked list.
template <StatusType T = LifecycleStatus>
struct StatusTransition {
    std::optional<T> previous;             ///< Prior status; nullopt for the first transition.
    T status{};                            ///< Status entered by this transition.
    LifecycleClock::time_point timestamp;  ///< When it was entered.
    std::optional<LifecycleClock::time_point> previous_timestamp;  ///< When the prior status began.

    /// Time spent in the *previous* status (this timestamp minus the previous
    /// one); zero for the first transition.
    [[nodiscard]] LifecycleClock::duration duration() const {
        return previous_timestamp ? timestamp - *previous_timestamp
                                  : LifecycleClock::duration::zero();
    }

    /// The prior status (nullopt for the first transition).
    [[nodiscard]] const std::optional<T>& previous_status() const {
        return previous;
    }

    [[nodiscard]] bool operator==(const StatusTransition&) const = default;
};

// -- StatusReport ------------------------------------------------------------

/// A per-status period summary: the time spent in `status`, and the total
/// elapsed since the timeline start.
template <StatusType T = LifecycleStatus>
struct StatusReport {
    std::optional<T> previous;                  ///< Prior status; nullopt for the first.
    T status{};                                 ///< The status this report covers.
    LifecycleClock::duration duration{};        ///< Time spent in this status.
    LifecycleClock::duration total_duration{};  ///< Elapsed since the timeline start.

    [[nodiscard]] bool operator==(const StatusReport&) const = default;
};

// -- StatusTransitionTimeline ------------------------------------------------

/// Owns the transition history of a single object; thread-safe.
///
/// Holds an internal `std::mutex`, so it is **non-copyable and non-movable**.
/// All accessors take a snapshot under the lock and return by value. Empty-
/// timeline status/transition accessors throw `LifecycleError`; the
/// `std::optional`-returning temporal queries return `std::nullopt`.
template <StatusType T = LifecycleStatus>
class StatusTransitionTimeline {
public:
    using value_type = StatusTransition<T>;
    using subscription_id = std::size_t;
    using listener = std::function<void(const StatusTransition<T>&)>;

    /// Empty timeline (no initial status). Enables JSON `get<>()`.
    StatusTransitionTimeline() = default;

    /// Start with `initial` as the first status, timestamped `now()`.
    explicit StatusTransitionTimeline(T initial)
        : StatusTransitionTimeline(std::move(initial), LifecycleClock::now()) {}

    /// Start with `initial` as the first status, timestamped `at`.
    StatusTransitionTimeline(T initial, LifecycleClock::time_point at) {
        transitions_.push_back(
            StatusTransition<T>{std::nullopt, std::move(initial), at, std::nullopt});
    }

    StatusTransitionTimeline(const StatusTransitionTimeline&) = delete;
    StatusTransitionTimeline& operator=(const StatusTransitionTimeline&) = delete;
    StatusTransitionTimeline(StatusTransitionTimeline&&) = delete;
    StatusTransitionTimeline& operator=(StatusTransitionTimeline&&) = delete;
    ~StatusTransitionTimeline() = default;

    // -- mutation ------------------------------------------------------------

    /// Transition to `status` timestamped `now()`. Returns the new transition.
    StatusTransition<T> transition_to(T status) {
        return transition_to(std::move(status), LifecycleClock::now());
    }

    /// Transition to `status` timestamped `at`. Returns the new transition. The
    /// new transition's `previous` / `previous_timestamp` come from the current
    /// back element (or empty if this is the first). Matching listeners are
    /// snapshotted under the lock and invoked **after** unlocking, so that a
    /// listener that re-enters the timeline cannot deadlock the non-recursive
    /// mutex.
    StatusTransition<T> transition_to(T status, LifecycleClock::time_point at) {
        StatusTransition<T> tr;
        std::vector<listener> to_notify;
        {
            std::scoped_lock lock(mutex_);
            if (transitions_.empty()) {
                tr = StatusTransition<T>{std::nullopt, std::move(status), at, std::nullopt};
            } else {
                const auto& back = transitions_.back();
                tr = StatusTransition<T>{back.status, std::move(status), at, back.timestamp};
            }
            transitions_.push_back(tr);
            for (const auto& s : subscribers_) {
                if (!s.filter || *s.filter == tr.status) {
                    to_notify.push_back(s.fn);
                }
            }
        }
        for (const auto& fn : to_notify) {
            fn(tr);
        }
        return tr;
    }

    // -- status queries ------------------------------------------------------

    /// The first status. Throws `LifecycleError` if empty.
    [[nodiscard]] T first_status() const {
        std::scoped_lock lock(mutex_);
        require_non_empty();
        return transitions_.front().status;
    }

    /// The current (latest) status. Throws `LifecycleError` if empty.
    [[nodiscard]] T current_status() const {
        std::scoped_lock lock(mutex_);
        require_non_empty();
        return transitions_.back().status;
    }

    /// The status before the current one, or nullopt if there is none.
    [[nodiscard]] std::optional<T> previous_status() const {
        std::scoped_lock lock(mutex_);
        if (transitions_.empty()) {
            return std::nullopt;
        }
        return transitions_.back().previous;
    }

    /// Whether `status` ever appears in the timeline.
    [[nodiscard]] bool contains(const T& status) const {
        std::scoped_lock lock(mutex_);
        return std::ranges::any_of(transitions_,
                                   [&](const auto& tr) { return tr.status == status; });
    }

    // -- transition access ---------------------------------------------------

    /// The first transition. Throws `LifecycleError` if empty.
    [[nodiscard]] StatusTransition<T> first_transition() const {
        std::scoped_lock lock(mutex_);
        require_non_empty();
        return transitions_.front();
    }

    /// The current (latest) transition. Throws `LifecycleError` if empty.
    [[nodiscard]] StatusTransition<T> current_transition() const {
        std::scoped_lock lock(mutex_);
        require_non_empty();
        return transitions_.back();
    }

    /// A snapshot copy of every transition, in order.
    [[nodiscard]] std::vector<StatusTransition<T>> transitions() const {
        std::scoped_lock lock(mutex_);
        return transitions_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::scoped_lock lock(mutex_);
        return transitions_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::scoped_lock lock(mutex_);
        return transitions_.empty();
    }

    // -- temporal queries ----------------------------------------------------

    /// Time spent in `status`: from its first occurrence to the next transition,
    /// or to `now()` if it is the current status. Nullopt if `status` is absent.
    [[nodiscard]] std::optional<LifecycleClock::duration> status_duration(const T& status) const {
        std::scoped_lock lock(mutex_);
        for (std::size_t i = 0; i < transitions_.size(); ++i) {
            if (transitions_[i].status == status) {
                const auto start = transitions_[i].timestamp;
                const auto end = (i + 1 < transitions_.size()) ? transitions_[i + 1].timestamp
                                                               : LifecycleClock::now();
                return end - start;
            }
        }
        return std::nullopt;
    }

    /// The timestamp at which `status` was first entered, or nullopt if absent.
    [[nodiscard]] std::optional<LifecycleClock::time_point>
    status_timestamp(const T& status) const {
        std::scoped_lock lock(mutex_);
        for (const auto& tr : transitions_) {
            if (tr.status == status) {
                return tr.timestamp;
            }
        }
        return std::nullopt;
    }

    /// Time spent in the current status so far (its timestamp to `now()`).
    /// Nullopt if the timeline is empty.
    [[nodiscard]] std::optional<LifecycleClock::duration> current_status_duration() const {
        std::scoped_lock lock(mutex_);
        if (transitions_.empty()) {
            return std::nullopt;
        }
        return LifecycleClock::now() - transitions_.back().timestamp;
    }

    /// Elapsed time from the first occurrence of `from` to the first occurrence
    /// of `to`. Nullopt if either is absent.
    [[nodiscard]] std::optional<LifecycleClock::duration> duration_between(const T& from,
                                                                           const T& to) const {
        std::scoped_lock lock(mutex_);
        std::optional<LifecycleClock::time_point> from_at;
        std::optional<LifecycleClock::time_point> to_at;
        for (const auto& tr : transitions_) {
            if (!from_at && tr.status == from) {
                from_at = tr.timestamp;
            }
            if (!to_at && tr.status == to) {
                to_at = tr.timestamp;
            }
        }
        if (!from_at || !to_at) {
            return std::nullopt;
        }
        return *to_at - *from_at;
    }

    /// Elapsed time from the first transition to `now()`. Nullopt if empty.
    [[nodiscard]] std::optional<LifecycleClock::duration> total_duration() const {
        std::scoped_lock lock(mutex_);
        if (transitions_.empty()) {
            return std::nullopt;
        }
        return LifecycleClock::now() - transitions_.front().timestamp;
    }

    // -- reports -------------------------------------------------------------

    /// One report per transition: prior status, this status, the time spent in
    /// it (between the previous and this timestamp; zero for the first), and the
    /// total since the first timestamp.
    [[nodiscard]] std::vector<StatusReport<T>> status_reports() const {
        std::scoped_lock lock(mutex_);
        std::vector<StatusReport<T>> reports;
        if (transitions_.empty()) {
            return reports;
        }
        reports.reserve(transitions_.size());
        const auto start = transitions_.front().timestamp;
        for (const auto& tr : transitions_) {
            reports.push_back(
                StatusReport<T>{tr.previous, tr.status, tr.duration(), tr.timestamp - start});
        }
        return reports;
    }

    // -- subscriptions -------------------------------------------------------

    /// Subscribe to every transition. Returns an id usable with `unsubscribe`.
    subscription_id subscribe(listener fn) {
        std::scoped_lock lock(mutex_);
        const auto id = next_id_++;
        subscribers_.push_back(Subscriber{id, std::nullopt, std::move(fn)});
        return id;
    }

    /// Subscribe only to transitions *into* `status`.
    subscription_id subscribe(T status, listener fn) {
        std::scoped_lock lock(mutex_);
        const auto id = next_id_++;
        subscribers_.push_back(Subscriber{id, std::move(status), std::move(fn)});
        return id;
    }

    /// Remove the subscription with `id`; returns whether one was removed.
    bool unsubscribe(subscription_id id) {
        std::scoped_lock lock(mutex_);
        for (auto it = subscribers_.begin(); it != subscribers_.end(); ++it) {
            if (it->id == id) {
                subscribers_.erase(it);
                return true;
            }
        }
        return false;
    }

    /// Remove every subscription.
    void unsubscribe_all() {
        std::scoped_lock lock(mutex_);
        subscribers_.clear();
    }

    /// Number of active subscriptions.
    [[nodiscard]] std::size_t subscriber_count() const {
        std::scoped_lock lock(mutex_);
        return subscribers_.size();
    }

    // -- bulk load -----------------------------------------------------------

    /// Replace the entire transition history with `trs` (their stored
    /// `previous` / timestamps are kept verbatim). Primarily for deserialization
    /// — it restores the array directly rather than replaying `transition_to`,
    /// so subscribers are **not** notified and links are not recomputed.
    /// Subscriptions are left untouched.
    void load_transitions(std::vector<StatusTransition<T>> trs) {
        std::scoped_lock lock(mutex_);
        transitions_ = std::move(trs);
    }

private:
    struct Subscriber {
        subscription_id id;
        std::optional<T> filter;
        listener fn;
    };

    void require_non_empty() const {
        if (transitions_.empty()) {
            throw LifecycleError("StatusTransitionTimeline is empty");
        }
    }

    mutable std::mutex mutex_;
    std::vector<StatusTransition<T>> transitions_;
    std::vector<Subscriber> subscribers_;
    subscription_id next_id_ = 1;
};

}  // namespace comms

// -- std::hash + std::formatter for LifecycleStatus --------------------------
// Specializations live in namespace std (the primary templates are visible),
// matching the layout used by id.hpp / semver.hpp.

/// Hash a `LifecycleStatus` through its name so it works as a map key.
template <>
struct std::hash<comms::LifecycleStatus> {
    [[nodiscard]] std::size_t operator()(const comms::LifecycleStatus& s) const noexcept {
        return std::hash<std::string>{}(s.name());
    }
};

/// Format a `LifecycleStatus` as its name, reusing the string formatter so any
/// spec `std::string` accepts works transparently.
template <class Char>
struct std::formatter<comms::LifecycleStatus, Char> : std::formatter<std::string, Char> {
    template <class FormatContext>
    auto format(const comms::LifecycleStatus& s, FormatContext& ctx) const {
        return std::formatter<std::string, Char>::format(s.name(), ctx);
    }
};
