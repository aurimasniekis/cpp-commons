#pragma once

/// @file
/// @brief `IOrigin` / `OriginPtr` ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/origin.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace comms {

// IOrigin ⇄ JSON object {"kind", ...fields} ----------------------------------
// The base library carries no JSON, so the origin hooks live here. The four
// built-in kinds round-trip their fields below; a custom origin kind supplies
// its own to_json/from_json and (de)serializes its concrete type directly.
// OriginPtr (a std::unique_ptr) is handled by an adl_serializer at the bottom.

inline void to_json(::nlohmann::json& j, const CoreOrigin& o) {
    j = ::nlohmann::json{{"kind", std::string{o.kind()}}};
}
inline void from_json(const ::nlohmann::json& /*j*/, CoreOrigin& /*o*/) {}

inline void to_json(::nlohmann::json& j, const InternalOrigin& o) {
    j = ::nlohmann::json{{"kind", std::string{o.kind()}}};
}
inline void from_json(const ::nlohmann::json& /*j*/, InternalOrigin& /*o*/) {}

inline void to_json(::nlohmann::json& j, const ExternalOrigin& o) {
    j = ::nlohmann::json{{"kind", std::string{o.kind()}}};
    if (!o.source.empty()) {
        j["source"] = o.source;
    }
}
inline void from_json(const ::nlohmann::json& j, ExternalOrigin& o) {
    if (const auto it = j.find("source"); it != j.end() && !it->is_null()) {
        it->get_to(o.source);
    }
}

inline void to_json(::nlohmann::json& j, const UnknownOrigin& o) {
    j = ::nlohmann::json{{"kind", std::string{o.kind()}}};
}
inline void from_json(const ::nlohmann::json& /*j*/, UnknownOrigin& /*o*/) {}

// Polymorphic write: dispatch on kind() to the matching built-in serializer; an
// unrecognized (custom) kind falls back to writing just its discriminator.
inline void to_json(::nlohmann::json& j, const IOrigin& o) {
    if (const auto k = o.kind(); k == CoreOrigin::KIND) {
        j = static_cast<const CoreOrigin&>(o);
    } else if (k == InternalOrigin::KIND) {
        j = static_cast<const InternalOrigin&>(o);
    } else if (k == ExternalOrigin::KIND) {
        j = static_cast<const ExternalOrigin&>(o);
    } else if (k == UnknownOrigin::KIND) {
        j = static_cast<const UnknownOrigin&>(o);
    } else {
        j = ::nlohmann::json{{"kind", std::string{k}}};
    }
}

}  // namespace comms

// comms::OriginPtr is a std::unique_ptr<IOrigin>, which lives in namespace `std`,
// so (like std::optional) ADL cannot find a to_json/from_json for it in namespace
// comms. Specialize nlohmann's serializer: a null pointer round-trips as JSON
// null; a held origin writes via its polymorphic to_json and reads back by
// resolving "kind" against the GlobalOriginRegistry (unknown kind throws).
template <>
struct nlohmann::adl_serializer<::comms::OriginPtr> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const ::comms::OriginPtr& o) {
        if (o) {
            j = *o;  // polymorphic to_json(const IOrigin&)
        } else {
            j = nullptr;
        }
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::OriginPtr& o) {
        if (j.is_null()) {
            o = nullptr;
            return;
        }
        if (!j.is_object()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: origin must be a JSON object", &j);
        }
        const auto it = j.find("kind");
        if (it == j.end()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: origin missing 'kind'", &j);
        }
        const auto kind = it->template get<std::string>();
        auto created = ::comms::GlobalOriginRegistry::instance().create(kind);
        if (!created) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: unknown origin kind '" + kind + "'", &j);
        }
        // Restore the built-in kinds' fields; a custom kind comes back field-less
        // (its owner (de)serializes the concrete type with its own hooks).
        if (kind == ::comms::ExternalOrigin::KIND) {
            j.get_to(static_cast<::comms::ExternalOrigin&>(*created));
        }
        o = std::move(created);
    }
};  // namespace nlohmann

#endif  // COMMONS_WITH_NLOHMANN_JSON
