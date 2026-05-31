#pragma once

/// @file
/// @brief `Id<Tag, Repr>` ⇄ nlohmann/json (gated by `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/id.hpp>

#include <nlohmann/json.hpp>

namespace comms {

// Id<Tag, Repr> ⇄ inner Repr's natural JSON ----------------------------------
// Delegates straight to the wrapped representation's nlohmann handler — uint
// reprs travel as JSON numbers, `std::string` as a JSON string, and `ulid::Ulid`
// (when ULID is on) as the ULID-canonical string via its own to_json/from_json.

template <class Tag, class Repr>
inline void to_json(::nlohmann::json& j, const Id<Tag, Repr>& id) {
    j = id.value();
}

template <class Tag, class Repr>
inline void from_json(const ::nlohmann::json& j, Id<Tag, Repr>& id) {
    id = Id<Tag, Repr>{j.template get<Repr>()};
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
