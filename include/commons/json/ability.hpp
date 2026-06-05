#pragma once

/// @file
/// @brief `AbilityPtr` ⇄ nlohmann/json (gated by `COMMONS_WITH_NLOHMANN_JSON`).
///
/// The per-field (de)serialization is the virtual `IAbility::write_json` /
/// `read_json` hooks in `commons/ability.hpp` (gated there too), so a
/// sub-ability's extra fields round-trip by overriding them — this module only
/// wires up the pointer (and the free `to_json(const IAbility&)` that forwards
/// to the hook).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/ability.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace comms {

// IAbility ⇄ JSON object {"kind","value", ...fields}. The actual per-field work
// is the virtual IAbility::write_json *in ability.hpp* (gated), so a
// sub-ability's extra fields round-trip through AbilityPtr by overriding it.
inline void to_json(::nlohmann::json& j, const IAbility& a) {
    j = ::nlohmann::json::object();
    a.write_json(j);  // virtual: a sub-ability adds its own fields
}

}  // namespace comms

// comms::AbilityPtr is a std::unique_ptr<IAbility> (in namespace std), so ADL
// cannot find a to_json/from_json for it in namespace comms — specialize
// nlohmann's serializer. A null pointer round-trips as JSON null; a held ability
// writes via its polymorphic to_json and reads back by resolving "kind" against
// the GlobalAbilityRegistry (unknown kind throws), then restoring its fields.
template <>
struct nlohmann::adl_serializer<::comms::AbilityPtr> {
    template <typename BasicJsonType>
    static void to_json(BasicJsonType& j, const ::comms::AbilityPtr& a) {
        if (a) {
            j = *a;  // to_json(const IAbility&)
        } else {
            j = nullptr;
        }
    }

    template <typename BasicJsonType>
    static void from_json(const BasicJsonType& j, ::comms::AbilityPtr& a) {
        if (j.is_null()) {
            a = nullptr;
            return;
        }
        if (!j.is_object()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: ability must be a JSON object", &j);
        }
        const auto it = j.find("kind");
        if (it == j.end()) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: ability missing 'kind'", &j);
        }
        const auto kind = it->template get<std::string>();
        auto created = ::comms::GlobalAbilityRegistry::instance().create(kind);
        if (!created) {
            throw ::nlohmann::detail::other_error::create(
                502, "commons: unknown ability kind '" + kind + "'", &j);
        }
        created->read_json(j);  // virtual: a sub-ability reads its own fields
        a = std::move(created);
    }
};  // namespace nlohmann

#endif  // COMMONS_WITH_NLOHMANN_JSON
