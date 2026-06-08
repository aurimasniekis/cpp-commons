#pragma once

/// @file
/// @brief The `comms::Lifecycle` family ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).
///
///   - `LifecycleStatus` ⇄ a plain JSON **string** (the name).
///   - `StatusTransition<T>` ⇄ object: always `status` + `timestamp` (epoch
///     milliseconds); `previous` and `previous_timestamp` (ms) only when present.
///     A function template, instantiated only for a json-serializable `T`.
///   - `StatusReport<T>` ⇄ object: always `status` + `duration` (ms) +
///     `total_duration` (ms); `previous` only when present.
///   - `StatusTransitionTimeline<T>` ⇄ a JSON **array** of transitions (like
///     `AuditLog`). `to_json` snapshots via `transitions()`; `from_json` rebuilds
///     via `load_transitions()` (no replay, so subscribers are not notified and
///     links are taken verbatim from the array). Subscribers are transient and
///     never serialized.
///
/// The millisecond encoding truncates sub-millisecond `system_clock` ticks (like
/// `comms::AuditRecord::timestamp`), so use ms-aligned values when an exact
/// round-trip matters.

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/json/optional.hpp>
#include <commons/lifecycle.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

namespace comms {

// LifecycleStatus -----------------------------------------------------------

inline void to_json(::nlohmann::json& j, const LifecycleStatus& s) {
    j = s.name();
}

inline void from_json(const ::nlohmann::json& j, LifecycleStatus& s) {
    s = LifecycleStatus{j.get<std::string>()};
}

// StatusTransition<T> -------------------------------------------------------

template <StatusType T>
void to_json(::nlohmann::json& j, const StatusTransition<T>& tr) {
    j = ::nlohmann::json::object();
    j["status"] = tr.status;
    j["timestamp"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(tr.timestamp.time_since_epoch())
            .count();
    if (tr.previous) {
        j["previous"] = *tr.previous;
    }
    if (tr.previous_timestamp) {
        j["previous_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      tr.previous_timestamp->time_since_epoch())
                                      .count();
    }
}

template <StatusType T>
void from_json(const ::nlohmann::json& j, StatusTransition<T>& tr) {
    tr = StatusTransition<T>{};
    j.at("status").get_to(tr.status);
    tr.timestamp = LifecycleClock::time_point{
        std::chrono::milliseconds{j.at("timestamp").template get<std::int64_t>()}};
    if (const auto it = j.find("previous"); it != j.end() && !it->is_null()) {
        tr.previous = it->template get<T>();
    }
    if (const auto it = j.find("previous_timestamp"); it != j.end() && !it->is_null()) {
        tr.previous_timestamp =
            LifecycleClock::time_point{std::chrono::milliseconds{it->template get<std::int64_t>()}};
    }
}

// StatusReport<T> -----------------------------------------------------------

template <StatusType T>
void to_json(::nlohmann::json& j, const StatusReport<T>& r) {
    j = ::nlohmann::json::object();
    j["status"] = r.status;
    j["duration"] = std::chrono::duration_cast<std::chrono::milliseconds>(r.duration).count();
    j["total_duration"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(r.total_duration).count();
    if (r.previous) {
        j["previous"] = *r.previous;
    }
}

template <StatusType T>
void from_json(const ::nlohmann::json& j, StatusReport<T>& r) {
    r = StatusReport<T>{};
    j.at("status").get_to(r.status);
    r.duration = std::chrono::duration_cast<LifecycleClock::duration>(
        std::chrono::milliseconds{j.at("duration").template get<std::int64_t>()});
    r.total_duration = std::chrono::duration_cast<LifecycleClock::duration>(
        std::chrono::milliseconds{j.at("total_duration").template get<std::int64_t>()});
    if (const auto it = j.find("previous"); it != j.end() && !it->is_null()) {
        r.previous = it->template get<T>();
    }
}

// StatusTransitionTimeline<T> -----------------------------------------------

template <StatusType T>
void to_json(::nlohmann::json& j, const StatusTransitionTimeline<T>& tl) {
    j = ::nlohmann::json::array();
    for (const auto& tr : tl.transitions()) {
        j.push_back(tr);
    }
}

template <StatusType T>
void from_json(const ::nlohmann::json& j, StatusTransitionTimeline<T>& tl) {
    std::vector<StatusTransition<T>> trs;
    trs.reserve(j.size());
    for (const auto& el : j) {
        trs.push_back(el.template get<StatusTransition<T>>());
    }
    tl.load_transitions(std::move(trs));
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
