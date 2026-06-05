#pragma once

/// @file
/// @brief The `comms::AuditRecord` family ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).
///
///   - `AuditRecord` ⇄ object: always `identity` (a `{"kind", ...}` object) +
///     `timestamp` (epoch milliseconds, like `comms::IReason::created_at`);
///     `ip`/`user_agent`/`session_id` only when present, and `related_ids` /
///     `metadata` only when non-empty. The millisecond encoding truncates
///     sub-millisecond `system_clock` ticks.
///   - `ChangeAuditRecord<T>` ⇄ the base fields plus `before` / `after` (only
///     when present). A function template, instantiated only for a `T` that is
///     itself json-serializable.
///   - `AuditLog<Record>` (the `AuditRecords` / `ChangeAuditRecords<T>` aliases)
///     ⇄ a JSON array of records. Capacity is a build/config concern and is
///     **not** serialized; `from_json` rebuilds via `push()`, so the cap is
///     enforced on load — a log read back under a smaller cap keeps the newest N
///     (a within-capacity log round-trips exactly).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/audit_record.hpp>
#include <commons/json/identity.hpp>
#include <commons/json/metadata.hpp>
#include <commons/json/optional.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

namespace comms {

// AuditRecord ---------------------------------------------------------------

inline void to_json(::nlohmann::json& j, const AuditRecord& r) {
    j = ::nlohmann::json::object();
    j["identity"] = r.identity;  // IdentityPtr via adl_serializer<IdentityPtr>; always present
    j["timestamp"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(r.timestamp.time_since_epoch())
            .count();
    if (r.ip) {
        j["ip"] = *r.ip;
    }
    if (r.user_agent) {
        j["user_agent"] = *r.user_agent;
    }
    if (r.session_id) {
        j["session_id"] = *r.session_id;
    }
    if (!r.related_ids.empty()) {
        j["related_ids"] = r.related_ids;  // map<string,string> ⇄ JSON object (native)
    }
    if (!r.metadata.empty()) {
        j["metadata"] = r.metadata;  // comms::md::Object ⇄ json via ADL
    }
}

inline void from_json(const ::nlohmann::json& j, AuditRecord& r) {
    r = AuditRecord{};
    // Absent/null identity keeps the NoIdentity default — a record is never null.
    if (const auto it = j.find("identity"); it != j.end() && !it->is_null()) {
        it->get_to(r.identity);
    }
    if (const auto it = j.find("timestamp"); it != j.end() && !it->is_null()) {
        r.timestamp =
            AuditClock::time_point{std::chrono::milliseconds{it->template get<std::int64_t>()}};
    }
    if (const auto it = j.find("ip"); it != j.end() && !it->is_null()) {
        r.ip = it->template get<std::string>();
    }
    if (const auto it = j.find("user_agent"); it != j.end() && !it->is_null()) {
        r.user_agent = it->template get<std::string>();
    }
    if (const auto it = j.find("session_id"); it != j.end() && !it->is_null()) {
        r.session_id = it->template get<std::string>();
    }
    if (const auto it = j.find("related_ids"); it != j.end() && !it->is_null()) {
        it->get_to(r.related_ids);
    }
    if (const auto it = j.find("metadata"); it != j.end() && !it->is_null()) {
        it->get_to(r.metadata);  // comms::md::Object ⇄ json via ADL
    }
}

// ChangeAuditRecord<T> ------------------------------------------------------

template <class T>
void to_json(::nlohmann::json& j, const ChangeAuditRecord<T>& r) {
    to_json(j, static_cast<const AuditRecord&>(r));  // base fields first
    if (r.before) {
        j["before"] = *r.before;
    }
    if (r.after) {
        j["after"] = *r.after;
    }
}

template <class T>
void from_json(const ::nlohmann::json& j, ChangeAuditRecord<T>& r) {
    from_json(j, static_cast<AuditRecord&>(r));
    if (const auto it = j.find("before"); it != j.end() && !it->is_null()) {
        r.before = it->template get<T>();
    }
    if (const auto it = j.find("after"); it != j.end() && !it->is_null()) {
        r.after = it->template get<T>();
    }
}

// AuditLog<Record> ----------------------------------------------------------

template <class Record>
void to_json(::nlohmann::json& j, const AuditLog<Record>& log) {
    j = ::nlohmann::json::array();
    for (const auto& r : log.records()) {
        j.push_back(r);
    }
}

template <class Record>
void from_json(const ::nlohmann::json& j, AuditLog<Record>& log) {
    log = AuditLog<Record>{};
    for (const auto& el : j) {
        log.push(el.template get<Record>());  // capacity enforced on push
    }
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
