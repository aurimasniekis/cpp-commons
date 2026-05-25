#pragma once

/// @file
/// @brief Presentation metadata for a type/value — `name`, `description`,
///        `icon`, `color`, all optional — plus the trait/concept machinery to
///        attach and retrieve it.
///
/// `comms::DisplayInfo` is a tiny container of UI-oriented metadata about a
/// type. The `icon` and `color` fields reuse `comms::Icon` and `comms::Color`,
/// so they already carry Iconify ids and MUI/CSS palettes and serialize to JSON
/// for a frontend. Every field is optional; populate only what a given type
/// needs.
///
/// The intent is **static, compile-time-fixed** data attached to a type as a
/// trait: define it once (a `static const DisplayInfo` returned by reference)
/// and never mutate it at runtime.
///
/// Two ways to attach it, both supported:
///   - **Intrusive** — give the type a static member
///     `static const DisplayInfo& display_info()`.
///   - **Non-intrusive** — specialize `comms::HasDisplayInfo<T>` for types you
///     cannot edit (enums, third-party types).
///
/// The free `comms::display_info<T>()` dispatches to whichever is present, and
/// the `comms::Displayable<T>` concept reports whether either exists. A type
/// with neither is simply not `Displayable` (no hard error); calling
/// `display_info<T>()` on such a type is an expected compile error.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// `DisplayInfo` travels as a JSON object whose absent fields are omitted.

#include <commons/color.hpp>
#include <commons/icon.hpp>

#include <concepts>
#include <optional>
#include <string>

namespace comms {

/// Presentation metadata for a type/value. All fields optional; define once as
/// static, never mutate at runtime.
struct DisplayInfo {
    std::optional<std::string> name = std::nullopt;         ///< Human-readable label.
    std::optional<std::string> description = std::nullopt;  ///< Longer explanatory text.
    std::optional<Icon> icon = std::nullopt;                ///< Associated Iconify icon.
    std::optional<Color> color = std::nullopt;              ///< Associated display color.
    bool operator==(const DisplayInfo&) const = default;    ///< Field-wise equality.
};

namespace detail {
template <typename T>
concept HasMemberDisplayInfo = requires {
    { T::display_info() } -> std::same_as<const DisplayInfo&>;
};
}  // namespace detail

/// Customization point: the primary template forwards to a member
/// `T::display_info()` when present; specialize it for non-intrusive
/// attachment to types you cannot edit.
template <typename T>
struct HasDisplayInfo {
    [[nodiscard]] static const DisplayInfo& display_info()
        requires detail::HasMemberDisplayInfo<T>
    {
        return T::display_info();
    }
};

/// Retrieve the `DisplayInfo` for `T` via whichever mechanism is present.
/// Ill-formed for a type that is not `Displayable`.
template <typename T>
[[nodiscard]] const DisplayInfo& display_info() {
    return HasDisplayInfo<T>::display_info();
}

/// True when `T` has display metadata — via a member `display_info()` or a
/// `HasDisplayInfo<T>` specialization. `std::same_as<const DisplayInfo&>`
/// forces a reference return, ruling out a dangling reference to a by-value
/// result.
template <typename T>
concept Displayable = requires {
    { HasDisplayInfo<T>::display_info() } -> std::same_as<const DisplayInfo&>;
};

}  // namespace comms
