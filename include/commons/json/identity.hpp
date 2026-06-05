#pragma once

/// @file
/// @brief `IdentityPtr` ⇄ nlohmann/json (gated by `COMMONS_WITH_NLOHMANN_JSON`).
///
/// The per-field (de)serialization is the virtual `IIdentity::write_json` /
/// `read_json` hooks in `commons/identity.hpp` (gated there too), so a
/// sub-identity's extra fields round-trip by overriding them — this module only
/// wires up the pointer. The `abilities` array round-trips through
/// `adl_serializer<AbilityPtr>` (from `commons/json/ability.hpp`, included here).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/identity.hpp>
#include <commons/json/ability.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace comms {

// IIdentity ⇄ JSON object {"kind","value", ...fields}. The per-field work is the
// virtual IIdentity::write_json *in identity.hpp* (gated).
inline void to_json(::nlohmann::json& j, const IIdentity& i) {
    j = ::nlohmann::json::object();
    i.write_json(j);  // virtual: a sub-identity adds its own fields
}

}  // namespace comms

// comms::IdentityPtr is a std::unique_ptr<IIdentity> (in namespace std), so
// specialize nlohmann's serializer. A null pointer round-trips as JSON null; a
// held identity writes via its polymorphic to_json and reads back by resolving
// "kind" against the GlobalIdentityRegistry (unknown kind throws).
template <>
struct nlohmann::adl_serializer<::comms::IdentityPtr> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const ::comms::IdentityPtr& i) {
        if (i) {
            j = *i;  // to_json(const IIdentity&)
        } else {
            j = nullptr;
        }
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::IdentityPtr& i) {
        if (j.is_null()) {
            i = nullptr;
            return;
        }
        if (!j.is_object()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: identity must be a JSON object", &j);
        }
        const auto it = j.find("kind");
        if (it == j.end()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: identity missing 'kind'", &j);
        }
        const auto kind = it->template get<std::string>();
        auto created = ::comms::GlobalIdentityRegistry::instance().create(kind);
        if (!created) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: unknown identity kind '" + kind + "'", &j);
        }
        created->read_json(j);  // virtual: a sub-identity reads its own fields
        i = std::move(created);
    }
};  // namespace nlohmann

#endif  // COMMONS_WITH_NLOHMANN_JSON
