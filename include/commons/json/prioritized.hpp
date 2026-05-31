#pragma once

/// @file
/// @brief `WithPriority<T>` / `PrioritizedSet<T>` ⇄ nlohmann/json (gated by
///        `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/prioritized.hpp>

#include <nlohmann/json.hpp>

#include <type_traits>
#include <utility>

namespace comms {

// WithPriority<T> / PrioritizedSet<T> ⇄ JSON ---------------------------------
// Gated on T being json-serializable so a non-serializable payload does not
// break this header. A WithPriority travels as {"priority":N,"value":<T>}
// (reusing T's own hooks for the value, the way DisplayInfo reuses Icon/Color);
// a PrioritizedSet travels as a JSON array in sorted (ascending-priority) order.

namespace detail {

template <typename T>
concept JsonSerializable = requires(::nlohmann::json& j, const T& v) { j = v; };

template <typename T>
concept JsonDeserializable = requires(const ::nlohmann::json& j, T& v) { j.get_to(v); };

}  // namespace detail

template <typename T>
    requires detail::JsonSerializable<T>
inline void to_json(::nlohmann::json& j, const WithPriority<T>& w) {
    j = ::nlohmann::json{{"priority", w.priority()}, {"value", w.value()}};
}

template <typename T>
    requires detail::JsonDeserializable<T>
inline void from_json(const ::nlohmann::json& j, WithPriority<T>& w) {
    j.at("value").get_to(w.value());
    w.set_priority(j.at("priority").template get<int>());
}

template <typename T>
    requires detail::JsonSerializable<T>
inline void to_json(::nlohmann::json& j, const PrioritizedSet<T>& s) {
    j = ::nlohmann::json::array();
    for (const auto& v : s) {
        j.push_back(v);  // already in sorted order
    }
}

// from_json only when the priority can be recovered from the element itself;
// otherwise the set is to_json-only (a plain T's priority is not persisted).
template <typename T>
    requires detail::JsonDeserializable<T> &&
             (std::is_base_of_v<Prioritized, T> || Prioritizable<T>)
inline void from_json(const ::nlohmann::json& j, PrioritizedSet<T>& s) {
    if (!j.is_array()) {
        throw ::nlohmann::detail::other_error::create(
            502, "commons: PrioritizedSet expects a JSON array", &j);
    }
    s.clear();
    for (const auto& elem : j) {
        T value = elem.template get<T>();
        const int p = get_priority(value);  // re-snapshot from the element
        s.insert(p, std::move(value));
    }
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
