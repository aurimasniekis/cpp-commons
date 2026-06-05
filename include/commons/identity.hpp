#pragma once

/// @file
/// @brief `comms::IIdentity` — a polymorphic envelope describing *who* a
///        principal is (a user, a server, …), discriminated by a compile-time
///        kind and carrying the abilities it holds.
///
/// An `Identity` answers "who?". Like `comms::IAbility` it is an **open set**:
/// each concrete kind identifies itself with a `kind()` discriminator supplied
/// as a compile-time `comms::FixedString` via the `IdentityKind<"kind", Derived>`
/// CRTP base (wiring `clone()` and an optional `DisplayInfo`-backed `info()`).
/// New kinds self-register into the program-wide `GlobalIdentityRegistry` via
/// `COMMONS_REGISTER_IDENTITY(Type)`; `COMMONS_DEFINE_IDENTITY(Ident, "kind")`
/// defines *and* registers one in a line.
///
/// An identity carries a list of `abilities` (each an `AbilityPtr`).
/// `identity.satisfies(required)` answers "does this principal hold an ability
/// that satisfies the `required` one?" — the default tries every held ability
/// against `required.allowed(...)`. This pairs with `IAbility::allowed`: the two
/// spellings `required.allowed(identity)` and `identity.satisfies(required)` are
/// equivalent (the former is defined here, forwarding to the latter).
///
/// Built-in kinds: the value-carrying `UserIdentity` (`"user"`), `ServerIdentity`
/// (`"server"`), `ApiClientIdentity` (`"api_client"`), `UnknownIdentity`
/// (`"unknown"`) all use the default `satisfies`; `RootIdentity` (`"root"`)
/// overrides it to allow **everything**; `NoIdentity` (`"none"`) overrides it to
/// allow **nothing** (and is the `AuditRecord` default — never null).
///
/// Serialization (gated by `COMMONS_WITH_NLOHMANN_JSON`): like `comms::IAbility`,
/// the per-field (de)serializers are the **virtual** `IIdentity::write_json` /
/// `read_json` hooks *in this header*; `commons/json/identity.hpp` only supplies
/// the `adl_serializer<IdentityPtr>` that drives them and resolves the `kind`
/// through the registry. The `abilities` array round-trips through
/// `adl_serializer<AbilityPtr>`. **Caveat:** the gate adds virtual members, so
/// every translation unit in a build must resolve `COMMONS_WITH_NLOHMANN_JSON`
/// identically.
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// emit `title: value`, where `title()` defaults to the concrete type's name.

#include <commons/ability.hpp>
#include <commons/config.hpp>
#include <commons/detail/type_name.hpp>
#include <commons/display_info.hpp>
#include <commons/fixed_string.hpp>
#include <commons/id.hpp>
#include <commons/metadata.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#if COMMONS_WITH_NLOHMANN_JSON
#include <commons/json/ability.hpp>  // abilities round-trip via adl_serializer<AbilityPtr>
#include <commons/json/metadata.hpp>

#include <nlohmann/json.hpp>
#endif

namespace comms {

class IIdentity;

/// An owning handle to an identity. The canonical way to carry an `IIdentity` by
/// value.
using IdentityPtr = std::unique_ptr<IIdentity>;

/// Abstract "who" envelope. Hold one through `IdentityPtr`; read its `value`,
/// optional `id`, `abilities`, and `metadata`, query its `kind()` discriminator,
/// copy it with `clone()`, and check a requirement with `satisfies()`.
class IIdentity {
public:
    std::string value;                  ///< The principal's identifier (username, host, …).
    std::optional<std::string> id;      ///< Optional id; set via set_id().
    std::vector<AbilityPtr> abilities;  ///< The abilities this principal holds.
    Metadata metadata;  ///< Optional structured context (a `comms::md::Object`); empty by default.

    virtual ~IIdentity() = default;

    /// The stable discriminator for this identity's kind (e.g. `"user"`).
    [[nodiscard]] virtual std::string_view kind() const noexcept = 0;

    /// A deep, independent copy (abilities are cloned).
    [[nodiscard]] virtual IdentityPtr clone() const = 0;

    /// Presentation metadata for this identity's kind (empty when none).
    [[nodiscard]] virtual const DisplayInfo& info() const = 0;

    /// Whether this principal satisfies the `required` ability. Default: any
    /// held ability satisfies it (`required.allowed(held)`). `RootIdentity`
    /// overrides this to `true`; `NoIdentity` to `false`.
    [[nodiscard]] virtual bool satisfies(const IAbility& required) const {
        return std::ranges::any_of(abilities,
                                   [&](const AbilityPtr& a) { return a && required.allowed(*a); });
    }

    /// Value equality against another identity. Default compares
    /// `kind`/`value`/`id`/`metadata` plus the `abilities` element-wise (via
    /// `ability_equal`). A kind with extra fields overrides this.
    [[nodiscard]] virtual bool equals(const IIdentity& other) const {
        if (kind() != other.kind() || value != other.value || id != other.id ||
            metadata != other.metadata || abilities.size() != other.abilities.size()) {
            return false;
        }
        for (std::size_t i = 0; i < abilities.size(); ++i) {
            if (!ability_equal(abilities[i], other.abilities[i])) {
                return false;
            }
        }
        return true;
    }

    /// A human-readable title for `to_string` / `operator<<` / `std::format`.
    /// Defaults to the concrete type's name (e.g. `UserIdentity`).
    [[nodiscard]] virtual std::string title() const {
        return detail::demangle_type_name(typeid(*this));
    }

    /// Record an `id` from a strong-typed `Id`, captured as its string form.
    template <class Tag, class Repr>
    void set_id(const Id<Tag, Repr>& ident) {
        id = to_string(ident);
    }

    /// Attach an ability to this identity.
    void add_ability(AbilityPtr ability) {
        abilities.push_back(std::move(ability));
    }

#if COMMONS_WITH_NLOHMANN_JSON
    /// Write this identity's fields into `j`: `kind`/`value`, plus `id`,
    /// `abilities` (an array), and `metadata` when present/non-empty. Override to
    /// add your own fields — call `IIdentity::write_json(j)` first.
    virtual void write_json(nlohmann::json& j) const {
        j["kind"] = std::string{kind()};
        j["value"] = value;
        if (id) {
            j["id"] = *id;
        }
        if (!abilities.empty()) {
            j["abilities"] = abilities;  // vector<AbilityPtr> via adl_serializer<AbilityPtr>
        }
        if (!metadata.empty()) {
            j["metadata"] = metadata;  // comms::md::Object ⇄ json via ADL
        }
    }

    /// Read this identity's fields from `j` (absent keys keep their defaults;
    /// `kind` is fixed by the concrete type). Override to read your own fields —
    /// call `IIdentity::read_json(j)` first.
    virtual void read_json(const nlohmann::json& j) {
        if (const auto it = j.find("value"); it != j.end() && !it->is_null()) {
            it->get_to(value);
        }
        if (const auto it = j.find("id"); it != j.end() && !it->is_null()) {
            id = it->template get<std::string>();
        }
        if (const auto it = j.find("abilities"); it != j.end() && it->is_array()) {
            abilities.clear();
            for (const auto& el : *it) {
                abilities.push_back(el.template get<AbilityPtr>());
            }
        }
        if (const auto it = j.find("metadata"); it != j.end() && !it->is_null()) {
            it->get_to(metadata);  // comms::md::Object ⇄ json via ADL
        }
    }
#endif

protected:
    IIdentity() = default;

    // Deep-clone the abilities so the CRTP clone() produces an independent
    // identity (a vector<AbilityPtr> is not copyable on its own).
    IIdentity(const IIdentity& other) : value(other.value), id(other.id), metadata(other.metadata) {
        abilities.reserve(other.abilities.size());
        for (const auto& a : other.abilities) {
            abilities.push_back(a ? a->clone() : nullptr);
        }
    }
    IIdentity(IIdentity&&) = default;
    IIdentity& operator=(IIdentity&&) = default;
    // Copy-assignment stays implicitly deleted (the unique_ptr vector is not
    // copy-assignable); identities are copied via clone(), not assignment.
};

/// CRTP base wiring `kind()`, `clone()`, and `info()` from a compile-time kind
/// string and the concrete `Derived` type, over an `Interface` that is either
/// `IIdentity` or a custom sub-interface. Provides the `()` / `(value)`
/// constructors so a derived kind can `using IdentityKind::IdentityKind;`.
template <FixedString Kind, typename Derived, typename Interface = IIdentity>
class IdentityKind : public Interface {
public:
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr std::string_view KIND = Kind.view();  ///< Compile-time discriminator.

    IdentityKind() = default;
    explicit IdentityKind(std::string val) {
        this->value = std::move(val);
    }

    [[nodiscard]] std::string_view kind() const noexcept override {
        return KIND;
    }

    [[nodiscard]] IdentityPtr clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }

    [[nodiscard]] const DisplayInfo& info() const override {
        if constexpr (detail::HasMemberDisplayInfo<Derived>) {
            return Derived::display_info();
        } else {
            return detail::empty_display_info();
        }
    }
};

// -- registry ----------------------------------------------------------------

/// A program-wide registry mapping an identity `kind` string to a factory.
/// Mirrors `GlobalAbilityRegistry`. Used by the JSON `from_json` path.
class GlobalIdentityRegistry {
public:
    /// Produces a fresh, default-constructed identity of one kind.
    using Factory = IdentityPtr (*)();

private:
    std::map<std::string, Factory, std::less<>> factories_;
    GlobalIdentityRegistry() = default;

public:
    GlobalIdentityRegistry(const GlobalIdentityRegistry&) = delete;
    GlobalIdentityRegistry& operator=(const GlobalIdentityRegistry&) = delete;
    GlobalIdentityRegistry(GlobalIdentityRegistry&&) = delete;
    GlobalIdentityRegistry& operator=(GlobalIdentityRegistry&&) = delete;
    ~GlobalIdentityRegistry() = default;

    [[nodiscard]] static GlobalIdentityRegistry& instance() noexcept {
        static GlobalIdentityRegistry r;
        return r;
    }

    bool register_kind(const std::string_view kind, Factory factory) {
        return factories_.emplace(std::string{kind}, factory).second;
    }

    [[nodiscard]] IdentityPtr create(const std::string_view kind) const {
        if (const auto it = factories_.find(kind); it != factories_.end()) {
            return it->second();
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(const std::string_view kind) const {
        return factories_.contains(kind);
    }

    /// The names of all registered kinds, in sorted order.
    [[nodiscard]] std::vector<std::string> kinds() const {
        std::vector<std::string> out;
        out.reserve(factories_.size());
        for (const auto& name : factories_ | std::views::keys) {
            out.push_back(name);
        }
        return out;
    }

    /// The names of the registered kinds whose identity is-a `Base`.
    template <typename Base = IIdentity>
        requires std::derived_from<Base, IIdentity>
    [[nodiscard]] std::vector<std::string> kinds_of() const {
        std::vector<std::string> out;
        for (const auto& [name, factory] : factories_) {
            const IdentityPtr probe = factory();
            if (dynamic_cast<const Base*>(probe.get()) != nullptr) {
                out.push_back(name);
            }
        }
        return out;
    }
};

/// A self-registering object for the `GlobalIdentityRegistry`. `T` must be
/// default-constructible and expose a `static constexpr std::string_view KIND`.
template <typename T>
struct IdentityRegistrar {
    IdentityRegistrar() noexcept {
        GlobalIdentityRegistry::instance().register_kind(
            T::KIND, []() -> IdentityPtr { return std::make_unique<T>(); });
    }
};

}  // namespace comms

/// Register an already-defined identity type `Ident` into the
/// `GlobalIdentityRegistry`. Place it at namespace scope after the type.
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_REGISTER_IDENTITY(Ident)                                                           \
    inline const ::comms::IdentityRegistrar<Ident> commons_identity_registrar_##Ident {}

/// Define **and** register an identity kind in one line, inheriting the
/// `IdentityKind` constructors — `()` and `(value)`.
///
///     COMMONS_DEFINE_IDENTITY(ServiceIdentity, "service");
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_DEFINE_IDENTITY(Ident, Kind)                                                       \
    class Ident final : public ::comms::IdentityKind<Kind, Ident> {                                \
    public:                                                                                        \
        using ::comms::IdentityKind<Kind, Ident>::IdentityKind;                                    \
    };                                                                                             \
    COMMONS_REGISTER_IDENTITY(Ident)

namespace comms {

// -- built-in kinds ----------------------------------------------------------

/// A human user, named by `value` (the username).
COMMONS_DEFINE_IDENTITY(UserIdentity, "user");

/// A server / service principal.
COMMONS_DEFINE_IDENTITY(ServerIdentity, "server");

/// An API client / integration principal.
COMMONS_DEFINE_IDENTITY(ApiClientIdentity, "api_client");

/// The principal is not known / was not specified.
COMMONS_DEFINE_IDENTITY(UnknownIdentity, "unknown");

/// A superuser that satisfies **every** ability, regardless of what it holds.
class RootIdentity final : public IdentityKind<"root", RootIdentity> {
public:
    using IdentityKind<"root", RootIdentity>::IdentityKind;

    /// Root allows everything.
    [[nodiscard]] bool satisfies(const IAbility& /*required*/) const override {
        return true;
    }

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Root",
            .description = "A superuser that satisfies every ability.",
            .icon = Icon::from("mdi:shield-crown"),
        };
        return info;
    }
};

/// The absence of a principal — satisfies **no** ability. The `AuditRecord`
/// default, so an unauthenticated record is never null.
class NoIdentity final : public IdentityKind<"none", NoIdentity> {
public:
    using IdentityKind<"none", NoIdentity>::IdentityKind;

    /// No identity allows nothing.
    [[nodiscard]] bool satisfies(const IAbility& /*required*/) const override {
        return false;
    }

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "None",
            .description = "No principal.",
            .icon = Icon::from("mdi:account-off"),
        };
        return info;
    }
};

// The built-in kinds self-register so the JSON `from_json` dispatch resolves them.
COMMONS_REGISTER_IDENTITY(RootIdentity);
COMMONS_REGISTER_IDENTITY(NoIdentity);

// -- factory -----------------------------------------------------------------

/// Construct an identity on the heap. Defaults to `NoIdentity`; pass another
/// kind explicitly: `make_identity<UserIdentity>("alice")`.
template <typename I = NoIdentity, typename... Args>
    requires std::derived_from<I, IIdentity>
[[nodiscard]] IdentityPtr make_identity(Args&&... args) {
    return std::make_unique<I>(std::forward<Args>(args)...);
}

// -- the deferred IAbility::allowed(const IIdentity&) ------------------------

/// `required.allowed(who)` ⇔ `who.satisfies(required)`. Defined here (not in
/// `ability.hpp`) because it needs the full `IIdentity`.
[[nodiscard]] inline bool IAbility::allowed(const IIdentity& who) const {
    return who.satisfies(*this);
}

// -- helpers / text output ---------------------------------------------------

/// Null-safe value equality over two `IdentityPtr`s (both null → equal; one null
/// → unequal; otherwise `a->equals(*b)`).
[[nodiscard]] inline bool identity_equal(const IdentityPtr& a, const IdentityPtr& b) {
    if (!a || !b) {
        return !a && !b;
    }
    return a->equals(*b);
}

/// An identity as `title: value`.
[[nodiscard]] inline std::string to_string(const IIdentity& i) {
    return std::format("{}: {}", i.title(), i.value);
}

inline std::ostream& operator<<(std::ostream& os, const IIdentity& i) {
    return os << to_string(i);
}

}  // namespace comms

// ---------------------------------------------------------------------------
// std::format support, mirroring the IAbility / IReason / IOrigin formatters.
// ---------------------------------------------------------------------------

// NOLINTBEGIN(readability-convert-member-functions-to-static)

/// Formats any `comms::IIdentity` (and derived) as `title: value`. No spec.
template <typename T>
    requires std::derived_from<T, comms::IIdentity>
struct std::formatter<T> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: IIdentity takes no format spec");
        }
        return it;
    }

    auto format(const comms::IIdentity& i, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", comms::to_string(i));
    }
};  // namespace std

// NOLINTEND(readability-convert-member-functions-to-static)
