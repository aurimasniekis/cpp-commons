#pragma once

/// @file
/// @brief A strong-typed identifier — `comms::Id<Tag, Repr>` — that wraps an
///        allowed representation in a phantom `Tag` so that ids of different
///        kinds cannot be mixed.
///
/// The allowed representations are deliberately narrow: the four unsigned
/// fixed-width integer types (`std::uint8_t` / `16` / `32` / `64`),
/// `std::string`, and — gated by `COMMONS_WITH_ULID` — `ulid::Ulid`. Any other
/// representation fails the `AllowedIdRepr` concept at the point of use.
///
/// A `Tag` is any type. If it exposes a `static constexpr std::string_view name`
/// member, `display_string(id)` prefixes the representation with that name
/// (`user/42`), and the `COMMONS_DEFINE_*_ID` macros generate exactly that
/// shape. Without a `name`, `display_string` falls back to the bare
/// representation.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// an `Id` travels as its inner `Repr`'s natural JSON (a number for the uint
/// reprs, a string for `std::string`, and — when ULID is also on — the ULID
/// string for `ulid::Ulid`).
///
/// Text output (always available): `to_string`, `operator<<`, and
/// `std::format` all emit the inner representation. The `std::formatter`
/// specialization inherits from `std::formatter<Repr>`, so any format spec
/// that the underlying type accepts (e.g. `"{:#x}"` for the uint reprs) works
/// transparently.

#include <commons/config.hpp>
#include <commons/types.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if COMMONS_WITH_ULID
#include <ulid/ulid.h>
#endif

namespace comms {

// -- repr gating -------------------------------------------------------------

// The trait names follow the STL's `std::is_*` lower_case convention so they
// read naturally alongside the standard type traits — overriding the project's
// CamelCase rule for these specific predicates.
// NOLINTBEGIN(readability-identifier-naming)

/// Trait selecting the representations allowed inside `Id<Tag, Repr>`. The
/// primary template denies; specializations below opt the allowed types in.
template <class T>
struct is_allowed_id_repr : std::false_type {};

template <>
struct is_allowed_id_repr<std::uint8_t> : std::true_type {};
template <>
struct is_allowed_id_repr<std::uint16_t> : std::true_type {};
template <>
struct is_allowed_id_repr<std::uint32_t> : std::true_type {};
template <>
struct is_allowed_id_repr<std::uint64_t> : std::true_type {};
template <>
struct is_allowed_id_repr<std::string> : std::true_type {};

#if COMMONS_WITH_ULID
template <>
struct is_allowed_id_repr<::ulid::Ulid> : std::true_type {};
#endif

// NOLINTEND(readability-identifier-naming)

/// Concept form of `is_allowed_id_repr`.
template <class T>
concept AllowedIdRepr = is_allowed_id_repr<std::remove_cvref_t<T>>::value;

/// A tag exposing a `static constexpr std::string_view name`, used by
/// `display_string` and the `COMMONS_DEFINE_*_ID` macros.
template <class Tag>
concept NamedIdTag = requires {
    { Tag::name } -> std::convertible_to<std::string_view>;
};

namespace detail {

template <class T>
concept StdToStringable = requires(const T& v) {
    { std::to_string(v) } -> std::convertible_to<std::string>;
};

}  // namespace detail

// -- the type ----------------------------------------------------------------

/// Strong-typed identifier: a `Repr` value tagged with a phantom `Tag`. Two
/// `Id`s with different tags are unrelated types even when the underlying
/// representation is identical.
template <class Tag, AllowedIdRepr Repr>
class Id {
public:
    using tag_type = Tag;
    using repr_type = Repr;

    /// Default-construct the underlying `Repr` when it is itself
    /// default-initializable; otherwise the constructor is deleted (the
    /// constrained-defaulted pattern keeps `Id` usable in containers that
    /// require value-initialization).
    Id()
        requires(!std::default_initializable<Repr>)
    = delete;
    Id()
        requires std::default_initializable<Repr>
    = default;

    /// Forward into the underlying `Repr`. Explicit so that an `Id` is never
    /// silently constructed from a raw value.
    template <class U>
        requires std::constructible_from<Repr, U&&> && (!std::is_same_v<std::remove_cvref_t<U>, Id>)
    explicit Id(U&& u) : value_(std::forward<U>(u)) {}

    /// Convenience for `StringId<Tag>`: build directly from a `string_view`.
    explicit Id(std::string_view sv)
        requires std::is_same_v<Repr, std::string>
        : value_(sv) {}

    [[nodiscard]] const Repr& value() const& noexcept {
        return value_;
    }
    [[nodiscard]] Repr& value() & noexcept {
        return value_;
    }
    [[nodiscard]] Repr&& value() && noexcept {
        return std::move(value_);
    }

    [[nodiscard]] auto operator<=>(const Id&) const = default;
    [[nodiscard]] bool operator==(const Id&) const = default;

private:
    Repr value_{};
};

// -- aliases -----------------------------------------------------------------

template <class Tag>
using Uint8Id = Id<Tag, std::uint8_t>;
template <class Tag>
using Uint16Id = Id<Tag, std::uint16_t>;
template <class Tag>
using Uint32Id = Id<Tag, std::uint32_t>;
template <class Tag>
using Uint64Id = Id<Tag, std::uint64_t>;

template <class Tag>
using StringId = Id<Tag, std::string>;

#if COMMONS_WITH_ULID
template <class Tag>
using UlidId = Id<Tag, ::ulid::Ulid>;
#endif

// -- reflection --------------------------------------------------------------
// As with `is_allowed_id_repr`, these traits keep the STL `std::is_*` spelling.
// NOLINTBEGIN(readability-identifier-naming)

template <class T>
struct is_id : std::false_type {};

template <class Tag, class Repr>
struct is_id<Id<Tag, Repr>> : std::true_type {};

template <class T>
inline constexpr bool is_id_v = is_id<std::remove_cvref_t<T>>::value;

// NOLINTEND(readability-identifier-naming)

template <class T>
concept IdType = is_id_v<T>;

// -- free helpers ------------------------------------------------------------

/// Returns the underlying representation by const reference.
template <class Tag, class Repr>
[[nodiscard]] const Repr& to_underlying(const Id<Tag, Repr>& id) noexcept {
    return id.value();
}

/// `Id<Tag, std::string>` — return the value itself.
template <class Tag, class Repr>
    requires std::is_same_v<Repr, std::string>
[[nodiscard]] std::string to_string(const Id<Tag, Repr>& id) {
    return id.value();
}

/// `Id<Tag, uintN_t>` — delegate to `std::to_string`.
template <class Tag, class Repr>
    requires(!std::is_same_v<Repr, std::string>) && detail::StdToStringable<Repr>
[[nodiscard]] std::string to_string(const Id<Tag, Repr>& id) {
    return std::to_string(id.value());
}

#if COMMONS_WITH_ULID
/// `Id<Tag, ulid::Ulid>` — defer to ULID's canonical string form.
template <class Tag, class Repr>
    requires std::is_same_v<Repr, ::ulid::Ulid>
[[nodiscard]] std::string to_string(const Id<Tag, Repr>& id) {
    return id.value().string();
}
#endif

/// `Tag::name + "/" + to_string(id)` for a `NamedIdTag`, just `to_string(id)`
/// otherwise.
template <class Tag, class Repr>
[[nodiscard]] std::string display_string(const Id<Tag, Repr>& id) {
    if constexpr (NamedIdTag<Tag>) {
        std::string out{std::string_view{Tag::name}};
        out.push_back('/');
        out += to_string(id);
        return out;
    } else {
        return to_string(id);
    }
}

template <class Tag, class Repr>
std::ostream& operator<<(std::ostream& os, const Id<Tag, Repr>& id) {
    return os << to_string(id);
}

}  // namespace comms

// -- std::hash + std::formatter ---------------------------------------------
// Specializations live in namespace std (the primary templates are visible),
// matching the layout used by semver.hpp and the rest of commons.

/// Hash an `Id` through its underlying representation. Constrained on
/// `std::hash<Repr>` actually existing so that an exotic non-hashable `Repr`
/// is a substitution failure rather than a hard error.
template <class Tag, class Repr>
    requires requires(const Repr& r) {
        { std::hash<Repr>{}(r) } -> std::convertible_to<std::size_t>;
    }
struct std::hash<comms::Id<Tag, Repr>> {
    [[nodiscard]] std::size_t operator()(const comms::Id<Tag, Repr>& id) const noexcept {
        return std::hash<Repr>{}(id.value());
    }
};

/// Forward formatting to the underlying `Repr`'s formatter. Inheriting the
/// `Repr`'s parser means consumers get every format spec the wrapped type
/// accepts (e.g. `"{:#x}"` for the uint reprs).
template <class Tag, class Repr, class Char>
struct std::formatter<comms::Id<Tag, Repr>, Char> : std::formatter<Repr, Char> {
    template <class FormatContext>
    auto format(const comms::Id<Tag, Repr>& id, FormatContext& ctx) const {
        return std::formatter<Repr, Char>::format(id.value(), ctx);
    }
};

// -- macros ------------------------------------------------------------------
// Defined at file scope (after the std specializations) so they work from any
// namespace; the generated aliases fully qualify the commons types. `Name`
// appears as a `using`-declared identifier and inside a template argument, so
// it can't be parenthesized — silence the bugprone-macro-parentheses warning
// over the whole block.
// NOLINTBEGIN(bugprone-macro-parentheses)

/// Emit a tag type `Name##Tag` with `static constexpr std::string_view name`.
/// `[[maybe_unused]]` on `name` keeps the macro quiet at use sites that only
/// rely on it indirectly through `display_string` (never odr-used).
#define COMMONS_DEFINE_ID_TAG(Name, NameStr)                                                       \
    struct Name##Tag {                                                                             \
        [[maybe_unused]] static constexpr ::std::string_view name = NameStr;                       \
    }

#define COMMONS_DEFINE_UINT8_ID(Name, NameStr)                                                     \
    COMMONS_DEFINE_ID_TAG(Name, NameStr);                                                          \
    using Name = ::comms::Uint8Id<Name##Tag>

#define COMMONS_DEFINE_UINT16_ID(Name, NameStr)                                                    \
    COMMONS_DEFINE_ID_TAG(Name, NameStr);                                                          \
    using Name = ::comms::Uint16Id<Name##Tag>

#define COMMONS_DEFINE_UINT32_ID(Name, NameStr)                                                    \
    COMMONS_DEFINE_ID_TAG(Name, NameStr);                                                          \
    using Name = ::comms::Uint32Id<Name##Tag>

#define COMMONS_DEFINE_UINT64_ID(Name, NameStr)                                                    \
    COMMONS_DEFINE_ID_TAG(Name, NameStr);                                                          \
    using Name = ::comms::Uint64Id<Name##Tag>

#define COMMONS_DEFINE_STRING_ID(Name, NameStr)                                                    \
    COMMONS_DEFINE_ID_TAG(Name, NameStr);                                                          \
    using Name = ::comms::StringId<Name##Tag>

#if COMMONS_WITH_ULID
#define COMMONS_DEFINE_ULID_ID(Name, NameStr)                                                      \
    COMMONS_DEFINE_ID_TAG(Name, NameStr);                                                          \
    using Name = ::comms::UlidId<Name##Tag>
#endif

// NOLINTEND(bugprone-macro-parentheses)
