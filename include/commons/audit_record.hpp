#pragma once

/// @file
/// @brief The `comms::AuditRecord` family — a shared "audit trail" facility:
///        *who* did *what*, *when*, *from where*.
///
/// Three value types live here:
///   - `AuditRecord` — the base record: an actor (`username`, the only required
///     field), a `timestamp` (a real `time_point` defaulting to `now()`,
///     mirroring `comms::IReason::created_at`), optional request context
///     (`ip`, `user_agent`, `session_id`), a `related_ids` map (name → id
///     string), and a free-form `metadata` bag (`comms::Metadata`, empty by
///     default).
///   - `ChangeAuditRecord<T>` — extends `AuditRecord` with the `before` / `after`
///     values of a change (each `std::optional<T>`: a create has no `before`, a
///     delete has no `after`).
///   - `AuditLog<Record>` — a capped (drop-oldest) collection, with the
///     `AuditRecords` and `ChangeAuditRecords<T>` aliases.
///
/// Ids are stored as **strings**: `comms::Id<Tag, Repr>` is a template with no
/// type-erased form, so a heterogeneous `map<string, Id>` is impossible
/// directly. The templated `set_session_id` / `add_related_id` helpers capture
/// `comms::to_string(id)` at the call site, so a caller passes a real `Id` and
/// the record keeps its rendered form.
///
/// Including `<commons/id.hpp>` for those helpers does **not** force ulid — the
/// `ulid::Ulid` repr stays gated by `COMMONS_WITH_ULID`.
///
/// JSON (in `commons/json/audit_record.hpp`, gated by
/// `COMMONS_WITH_NLOHMANN_JSON`): an `AuditRecord` is an object that always
/// carries `username` + `timestamp` (epoch milliseconds), with the optional
/// fields emitted only when present/non-empty; a `ChangeAuditRecord<T>` adds
/// `before` / `after`; an `AuditLog` is a JSON array of records. The millisecond
/// encoding truncates sub-millisecond `system_clock` ticks (like
/// `comms::IReason::created_at`), so use ms-aligned timestamps when an exact
/// round-trip matters.

#include <commons/id.hpp>
#include <commons/metadata.hpp>

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Configurable default capacity for AuditLog (value-override seam).
//
// Lives here rather than in commons/config.hpp (reserved for the boolean
// COMMONS_WITH_* integration gates), mirroring the COMMONS_PRIORITIZED_* macros
// in prioritized.hpp. A build system or consumer may predefine it before this
// header is first included; CMake/Meson emit a -D only when overridden.
// ---------------------------------------------------------------------------
#if !defined(COMMONS_AUDIT_RECORDS_CAPACITY)
#define COMMONS_AUDIT_RECORDS_CAPACITY 3
#endif

namespace comms {

/// The clock backing `AuditRecord::timestamp`. Mirrors `comms::ReasonClock`.
using AuditClock = std::chrono::system_clock;

/// A single audit-trail entry — who did what, when, from where.
struct AuditRecord {
    std::string username;                                  ///< Actor (required).
    AuditClock::time_point timestamp = AuditClock::now();  ///< When; JSON: epoch millis.
    std::optional<std::string> ip;                         ///< Source IP (optional).
    std::optional<std::string> user_agent;                 ///< Client user-agent (optional).
    std::optional<std::string> session_id;           ///< Session id; set via set_session_id().
    std::map<std::string, std::string> related_ids;  ///< name → id string.
    Metadata metadata;                               ///< Free-form context (empty by default).

    // `operator==` only, no `operator<=>`: `comms::Metadata` (`md::Object`) is
    // equality-only, so a defaulted `<=>` would be implicitly deleted — and
    // there is no meaningful total order over a heterogeneous record anyway.
    [[nodiscard]] bool operator==(const AuditRecord&) const = default;

    /// Record the session id from a strong-typed `Id`, captured as its string form.
    template <class Tag, class Repr>
    void set_session_id(const Id<Tag, Repr>& id) {
        session_id = to_string(id);
    }

    /// Associate a named id with this record (re-adding a name overwrites).
    template <class Tag, class Repr>
    void add_related_id(std::string name, const Id<Tag, Repr>& id) {
        related_ids.insert_or_assign(std::move(name), to_string(id));
    }
};

/// An `AuditRecord` carrying the before/after values of a change to a `T`.
///
/// Inherits every `AuditRecord` field plus the templated helpers. Used by value
/// (no polymorphism), so the public inheritance needs no virtual destructor.
template <class T>
struct ChangeAuditRecord : AuditRecord {
    std::optional<T> before;  ///< Prior value; absent on create.
    std::optional<T> after;   ///< New value; absent on delete.

    // Defaulted `operator==` compares the `AuditRecord` base subobject plus
    // `before`/`after` (valid when `T` is equality-comparable). No `<=>` for the
    // same reason as the base — `comms::Metadata` is equality-only.
    [[nodiscard]] bool operator==(const ChangeAuditRecord&) const = default;
};

/// A capped, insertion-ordered log of records. `push()` appends; once the size
/// exceeds `capacity()` the oldest (front) records are dropped (FIFO). Capacity
/// `0` retains nothing. Serializes to a JSON array of records (capacity is a
/// build/config concern, not stored).
template <class Record>
class AuditLog {
public:
    using value_type = Record;
    using const_iterator = typename std::vector<Record>::const_iterator;

    AuditLog() = default;
    explicit AuditLog(const std::size_t capacity) : capacity_(capacity) {}

    /// Append a record, then drop the oldest while the size exceeds the capacity.
    void push(Record r) {
        records_.push_back(std::move(r));
        trim();
    }

    /// Update the capacity, dropping the oldest records if the log is now over.
    void set_capacity(const std::size_t c) {
        capacity_ = c;
        trim();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    /// Direct access to the underlying record vector (insertion order).
    [[nodiscard]] const std::vector<Record>& records() const noexcept {
        return records_;
    }

    [[nodiscard]] const_iterator begin() const noexcept {
        return records_.begin();
    }
    [[nodiscard]] const_iterator end() const noexcept {
        return records_.end();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return records_.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return records_.empty();
    }
    [[nodiscard]] const Record& operator[](std::size_t i) const {
        return records_[i];
    }
    [[nodiscard]] const Record& at(std::size_t i) const {
        return records_.at(i);
    }
    [[nodiscard]] const Record& front() const {
        return records_.front();
    }
    [[nodiscard]] const Record& back() const {
        return records_.back();
    }
    void clear() noexcept {
        records_.clear();
    }

    [[nodiscard]] bool operator==(const AuditLog&) const = default;

private:
    void trim() {
        if (records_.size() > capacity_) {
            records_.erase(records_.begin(),
                           records_.begin() +
                               static_cast<std::ptrdiff_t>(records_.size() - capacity_));
        }
    }

    std::vector<Record> records_;
    std::size_t capacity_ = COMMONS_AUDIT_RECORDS_CAPACITY;
};

/// A capped log of plain `AuditRecord`s.
using AuditRecords = AuditLog<AuditRecord>;

/// A capped log of `ChangeAuditRecord<T>`s.
template <class T>
using ChangeAuditRecords = AuditLog<ChangeAuditRecord<T>>;

}  // namespace comms
