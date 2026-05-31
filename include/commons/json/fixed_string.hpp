#pragma once

/// @file
/// @brief `FixedString<N>` ⇄ nlohmann/json (gated by `COMMONS_WITH_NLOHMANN_JSON`).

#include <commons/config.hpp>

#if COMMONS_WITH_NLOHMANN_JSON

#include <commons/fixed_string.hpp>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace comms {

// FixedString<N> ⇄ JSON string ----------------------------------------------

template <std::size_t N>
inline void to_json(::nlohmann::json& j, const FixedString<N>& s) {
    j = std::string{s.view()};
}

template <std::size_t N>
inline void from_json(const ::nlohmann::json& j, FixedString<N>& s) {
    const auto str = j.template get<std::string>();
    if (str.size() > FixedString<N>::size()) {
        throw ::nlohmann::detail::other_error::create(
            502,
            "commons: string of length " + std::to_string(str.size()) +
                " does not fit FixedString<" + std::to_string(N) + "> (capacity " +
                std::to_string(FixedString<N>::size()) + ")",
            &j);
    }
    std::size_t i = 0;
    for (; i < str.size(); ++i) {
        s.value[i] = str[i];
    }
    for (; i < N; ++i) {
        s.value[i] = '\0';
    }
}

}  // namespace comms

#endif  // COMMONS_WITH_NLOHMANN_JSON
