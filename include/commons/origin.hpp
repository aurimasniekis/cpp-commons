#pragma once

/// @file
/// @brief A polymorphic provenance envelope — `comms::IOrigin` — that records
///        *where* a definition came from, discriminated by a compile-time kind.
///
/// `comms::IOrigin` is an abstract base for an **open set** of provenance
/// sources. Each concrete origin identifies itself with a `kind()` string, but
/// that string is supplied as a compile-time `comms::FixedString` template
/// parameter rather than hand-written per subclass — the same pattern as
/// `IconifySet<FixedString Set>`. Derive a concrete origin from
/// `OriginKind<"yourkind", YourType>` and it gets `kind()`, a deep `clone()`, and
/// a polymorphic `info()` for free.
///
/// `info()` returns a `comms::DisplayInfo` (name/description/icon/color) sourced
/// from the concrete type's `static display_info()`, so every origin is also a
/// `comms::Displayable` and works with `comms::display_info<CoreOrigin>()`.
///
/// New kinds register themselves into the program-wide `GlobalOriginRegistry`
/// via `COMMONS_REGISTER_ORIGIN(Type)` — mirroring `GlobalFlagRegistry` /
/// `COMMONS_REGISTER_FLAG` — so a JSON `kind` discriminator can be turned back
/// into the right concrete type without consumers enumerating the set.
///
/// The built-in kinds are `CoreOrigin` (`"core"`), `InternalOrigin`
/// (`"internal"`), `ExternalOrigin` (`"external"`, carrying a `source` string),
/// and `UnknownOrigin` (`"unknown"`).
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// the base library has no forced dependency, so this header carries **no** JSON.
/// An origin travels as a JSON object `{"kind": …, …fields}`; the four built-in
/// kinds round-trip their fields, and a custom kind supplies its own
/// `to_json`/`from_json`.
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// all emit the `kind()` string.

#include <commons/display_info.hpp>
#include <commons/fixed_string.hpp>

#include <concepts>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace comms {

/// Abstract provenance envelope. Hold one through `OriginPtr`; query its `kind()`
/// discriminator, copy it with `clone()`, and read its description via `info()`.
class IOrigin {
public:
    virtual ~IOrigin() = default;

    /// The stable discriminator for this origin (e.g. `"core"`).
    [[nodiscard]] virtual std::string_view kind() const noexcept = 0;

    /// A deep, independent copy.
    [[nodiscard]] virtual std::unique_ptr<IOrigin> clone() const = 0;

    /// Presentation metadata for this origin's kind (the description), sourced
    /// from the concrete type's `static display_info()`.
    [[nodiscard]] virtual const DisplayInfo& info() const = 0;

protected:
    // Protected to prevent slicing through the interface; derived types remain
    // copyable/movable via their own (implicitly defined) operations.
    IOrigin() = default;
    IOrigin(const IOrigin&) = default;
    IOrigin(IOrigin&&) = default;
    IOrigin& operator=(const IOrigin&) = default;
    IOrigin& operator=(IOrigin&&) = default;
};

/// A heap-owned origin. The canonical way to carry an `IOrigin` by value.
using OriginPtr = std::unique_ptr<IOrigin>;

/// CRTP base wiring `kind()`, `clone()`, and `info()` from a compile-time kind
/// string and the concrete `Derived` type. Use it as
/// `class CoreOrigin final : public OriginKind<"core", CoreOrigin> { ... };`.
/// `Derived` must be copy-constructible (for `clone()`) and expose a
/// `static const DisplayInfo& display_info()` (for `info()`).
template <FixedString Kind, typename Derived>
class OriginKind : public IOrigin {
public:
    // KIND keeps the SCREAMING_CASE spelling of the reference's discriminator
    // constant rather than the project's lower_case style — it reads as the
    // type's identity tag and is the recognized convention for it.
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr std::string_view KIND = Kind.view();  ///< Compile-time discriminator.

    [[nodiscard]] std::string_view kind() const noexcept override {
        return KIND;
    }

    [[nodiscard]] std::unique_ptr<IOrigin> clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }

    [[nodiscard]] const DisplayInfo& info() const override {
        return Derived::display_info();
    }
};

// -- built-in kinds ----------------------------------------------------------

/// The definition was registered by the host core itself.
class CoreOrigin final : public OriginKind<"core", CoreOrigin> {
public:
    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Core",
            .description = "Registered by the host core itself.",
            .icon = Icon::from("mdi:home"),
        };
        return info;
    }
};

/// The definition was registered by an internal subsystem.
class InternalOrigin final : public OriginKind<"internal", InternalOrigin> {
public:
    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Internal",
            .description = "Registered by an internal subsystem.",
            .icon = Icon::from("mdi:cog"),
        };
        return info;
    }
};

/// The definition came from an external source, named by `source`.
class ExternalOrigin final : public OriginKind<"external", ExternalOrigin> {
public:
    std::string source;  ///< Free-form identifier of the external source.

    ExternalOrigin() = default;
    explicit ExternalOrigin(std::string src) : source(std::move(src)) {}

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "External",
            .description = "Registered by an external source.",
            .icon = Icon::from("mdi:web"),
        };
        return info;
    }
};

/// Provenance is unknown.
class UnknownOrigin final : public OriginKind<"unknown", UnknownOrigin> {
public:
    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Unknown",
            .description = "Provenance is unknown.",
            .icon = Icon::from("mdi:help-circle"),
        };
        return info;
    }
};

// -- registry ----------------------------------------------------------------

/// A program-wide registry mapping an origin `kind` string to a factory. A
/// Meyers singleton, like `GlobalFlagRegistry`, so the map is constructed before
/// any `inline` registrar runs. Used by the JSON `from_json` path to turn a
/// `kind` discriminator back into the right concrete origin.
class GlobalOriginRegistry {
public:
    /// Produces a fresh, default-constructed origin of one kind.
    using Factory = OriginPtr (*)();

private:
    std::map<std::string, Factory, std::less<>> factories_;
    GlobalOriginRegistry() = default;

public:
    GlobalOriginRegistry(const GlobalOriginRegistry&) = delete;
    GlobalOriginRegistry& operator=(const GlobalOriginRegistry&) = delete;
    GlobalOriginRegistry(GlobalOriginRegistry&&) = delete;
    GlobalOriginRegistry& operator=(GlobalOriginRegistry&&) = delete;
    ~GlobalOriginRegistry() = default;

    [[nodiscard]] static GlobalOriginRegistry& instance() noexcept {
        static GlobalOriginRegistry r;
        return r;
    }

    /// Register `factory` under `kind`. Returns `false` if the kind was already
    /// registered (the first registration wins).
    bool register_kind(const std::string_view kind, Factory factory) {
        return factories_.emplace(std::string{kind}, factory).second;
    }

    /// Create a fresh origin for `kind`, or `nullptr` if the kind is unknown.
    [[nodiscard]] OriginPtr create(const std::string_view kind) const {
        if (const auto it = factories_.find(kind); it != factories_.end()) {
            return it->second();
        }
        return nullptr;
    }

    /// Whether `kind` has a registered factory.
    [[nodiscard]] bool contains(const std::string_view kind) const {
        return factories_.contains(kind);
    }
};

/// A self-registering object: constructing one registers `T`'s factory into the
/// `GlobalOriginRegistry`. `COMMONS_REGISTER_ORIGIN` emits one as an `inline`
/// object so registration happens at static init. `T` must be
/// default-constructible and expose a `static constexpr std::string_view KIND`.
template <typename T>
struct OriginRegistrar {
    OriginRegistrar() noexcept {
        GlobalOriginRegistry::instance().register_kind(
            T::KIND, []() -> OriginPtr { return std::make_unique<T>(); });
    }
};

}  // namespace comms

/// Register an already-defined origin type `Ident` into the
/// `GlobalOriginRegistry`. Place it at namespace scope after the type.
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_REGISTER_ORIGIN(Ident)                                                             \
    inline const ::comms::OriginRegistrar<Ident> commons_origin_registrar_##Ident {}

namespace comms {

// The built-in kinds self-register so the JSON `from_json` dispatch resolves them.
COMMONS_REGISTER_ORIGIN(CoreOrigin);
COMMONS_REGISTER_ORIGIN(InternalOrigin);
COMMONS_REGISTER_ORIGIN(ExternalOrigin);
COMMONS_REGISTER_ORIGIN(UnknownOrigin);

// ---------------------------------------------------------------------------
// Text output: to_string + std::ostream insertion. (std::format support is the
// std::formatter specialization below, outside namespace comms.) Both accept any
// origin by upcast to the IOrigin base.
// ---------------------------------------------------------------------------

/// An origin as its `kind()` string.
[[nodiscard]] inline std::string to_string(const IOrigin& o) {
    return std::string{o.kind()};
}

inline std::ostream& operator<<(std::ostream& os, const IOrigin& o) {
    return os << o.kind();
}

}  // namespace comms

// ---------------------------------------------------------------------------
// std::format support. A partial specialization constrained to IOrigin-derived
// types so any concrete origin (or an IOrigin reference) formats, mirroring the
// adl_serializer route used in json.hpp.
// ---------------------------------------------------------------------------

// This spec-less formatter reads no member state, but `std::formatter` requires
// `parse`/`format` to be non-static members — so silence the convert-to-static
// suggestion here.
// NOLINTBEGIN(readability-convert-member-functions-to-static)

/// Formats any `comms::IOrigin` (and derived) as its `kind()` string. No spec.
template <typename T>
    requires std::derived_from<T, comms::IOrigin>
struct std::formatter<T> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: IOrigin takes no format spec");
        }
        return it;
    }

    auto format(const comms::IOrigin& o, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", comms::to_string(o));
    }
};  // namespace std

// NOLINTEND(readability-convert-member-functions-to-static)
