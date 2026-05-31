#pragma once

/// @file
/// @brief `ReasonPtr` / `FailureReasonPtr` ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).
///
/// The per-field (de)serialization is the virtual `IReason::write_json` /
/// `read_json` hooks in `commons/reason.hpp` (gated there too), so a sub-reason's
/// extra fields round-trip by overriding them — this module only wires up the
/// pointers (and the free `to_json(const IReason&)` that forwards to the hook).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/reason.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace comms {

// IReason / IFailureReason ⇄ JSON object {"kind","code","message","created_at"} -
// The actual per-field (de)serialization is the virtual IReason::write_json /
// read_json *in reason.hpp* (gated), so a sub-reason's extra fields round-trip
// through ReasonPtr by overriding those hooks. This free to_json just forwards to
// write_json (it dispatches on the dynamic type), and the ReasonPtr /
// FailureReasonPtr adl_serializers at the bottom drive read_json after resolving
// `kind` through the registry.

inline void to_json(::nlohmann::json& j, const IReason& r) {
    j = ::nlohmann::json::object();
    r.write_json(j);  // virtual: a sub-reason adds its own fields
}

}  // namespace comms

// comms::ReasonPtr is a std::unique_ptr<IReason> (in namespace std), so ADL
// cannot find a to_json/from_json for it in namespace comms — specialize
// nlohmann's serializer. A null pointer round-trips as JSON null; a held reason
// writes via its polymorphic to_json and reads back by resolving "kind" against
// the GlobalReasonRegistry (unknown kind throws), then restoring its fields.
template <>
struct nlohmann::adl_serializer<::comms::ReasonPtr> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const ::comms::ReasonPtr& r) {
        if (r) {
            j = *r;  // to_json(const IReason&)
        } else {
            j = nullptr;
        }
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::ReasonPtr& r) {
        if (j.is_null()) {
            r = nullptr;
            return;
        }
        if (!j.is_object()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: reason must be a JSON object", &j);
        }
        const auto it = j.find("kind");
        if (it == j.end()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: reason missing 'kind'", &j);
        }
        const auto kind = it->template get<std::string>();
        auto created = ::comms::GlobalReasonRegistry::instance().create(kind);
        if (!created) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: unknown reason kind '" + kind + "'", &j);
        }
        created->read_json(j);  // virtual: a sub-reason reads its own fields
        r = std::move(created);
    }
};  // namespace nlohmann

// comms::FailureReasonPtr is a std::unique_ptr<IFailureReason>. It reuses the
// ReasonPtr path to resolve and fill the kind, then verifies the resolved kind
// is actually a failure reason (an IFailureReason) before rewrapping — a plain
// reason kind in a failure slot is an error.
template <>
struct nlohmann::adl_serializer<::comms::FailureReasonPtr> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const ::comms::FailureReasonPtr& r) {
        if (r) {
            j = static_cast<const ::comms::IReason&>(*r);  // to_json(const IReason&)
        } else {
            j = nullptr;
        }
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::FailureReasonPtr& r) {
        if (j.is_null()) {
            r = nullptr;
            return;
        }
        auto base = j.template get<::comms::ReasonPtr>();
        auto* failure = dynamic_cast<::comms::IFailureReason*>(base.get());
        if (failure == nullptr) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: reason kind is not a failure reason", &j);
        }
        base.release();
        r = ::comms::FailureReasonPtr{failure};
    }
};  // namespace nlohmann

#endif  // COMMONS_WITH_NLOHMANN_JSON
