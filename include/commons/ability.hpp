#pragma once

/// @file
/// @brief `comms::IAbility` — a polymorphic envelope describing *what* a
///        principal may do (a role, a permission, …), discriminated by a
///        compile-time kind.
///
/// An `Ability` answers "what is allowed?". Like `comms::IOrigin` /
/// `comms::IReason` it is an **open set**: each concrete kind identifies itself
/// with a `kind()` discriminator supplied as a compile-time `comms::FixedString`
/// template parameter via the `AbilityKind<"kind", Derived>` CRTP base, which
/// also wires `clone()` and an (optional) `DisplayInfo`-backed `info()`. New
/// kinds self-register into the program-wide `GlobalAbilityRegistry` via
/// `COMMONS_REGISTER_ABILITY(Type)`, and `COMMONS_DEFINE_ABILITY(Ident, "kind")`
/// defines *and* registers one in a line (inheriting the `AbilityKind`
/// constructors `()` / `(value)`).
///
/// **Direction of the check.** The *required* ability is the receiver and the
/// candidate the argument: `required.allowed(candidate)` answers "does
/// `candidate` satisfy this requirement?". The default rule is "same kind and
/// same `value`"; a kind owns its own rule by overriding `allowed` (see
/// `RecordPermissionAbility`, which matches an `action`/`resource` pair with a
/// `"*"` wildcard). A second overload, `required.allowed(identity)`, forwards to
/// `identity.satisfies(required)` — it is declared here but **defined in
/// `commons/identity.hpp`**, so that overload needs that header.
///
/// Built-in kinds: `RoleAbility` (`"role"`, `value` = role name), the extensible
/// `RecordPermissionAbility` (`"record_permission"`), and the macro-defined
/// `GenericAbility` (`"generic"`) / `UnknownAbility` (`"unknown"`).
///
/// Serialization (gated by `COMMONS_WITH_NLOHMANN_JSON`): like `comms::IReason`,
/// the per-field (de)serializers are the **virtual** `IAbility::write_json` /
/// `read_json` hooks *in this header* (gated, so nlohmann is still only pulled
/// when present) — a sub-ability adds its own fields by overriding them (calling
/// the base first). `commons/json/ability.hpp` only supplies the
/// `adl_serializer<AbilityPtr>` that drives those hooks and resolves the `kind`
/// through the registry. **Caveat:** because the gate adds virtual members it
/// changes `IAbility`'s vtable — every translation unit in a build must resolve
/// `COMMONS_WITH_NLOHMANN_JSON` identically (force it with a `-D` if mixed).
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// emit `title: value`, where `title()` defaults to the concrete type's name.

#include <commons/config.hpp>
#include <commons/detail/type_name.hpp>
#include <commons/display_info.hpp>
#include <commons/fixed_string.hpp>
#include <commons/id.hpp>
#include <commons/metadata.hpp>

#include <concepts>
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
#include <commons/json/metadata.hpp>  // comms::md::Object ⇄ json, for the metadata field

#include <nlohmann/json.hpp>
#endif

namespace comms {

class IIdentity;  // for IAbility::allowed(const IIdentity&), defined in identity.hpp

class IAbility;

/// An owning handle to an ability. The canonical way to carry an `IAbility` by
/// value.
using AbilityPtr = std::unique_ptr<IAbility>;

/// Abstract "what is allowed" envelope. Hold one through `AbilityPtr`; read its
/// `value`, optional `id`, and `metadata`, query its `kind()` discriminator,
/// copy it with `clone()`, and check a candidate with `allowed()`.
class IAbility {
public:
    // Public data members (rather than getters) match the codebase convention
    // for carried fields — e.g. ExternalOrigin::source / IReason::message.
    std::string value;              ///< The thing granted (a role name, a permission, …).
    std::optional<std::string> id;  ///< Optional id; set via set_id().
    Metadata metadata;  ///< Optional structured context (a `comms::md::Object`); empty by default.

    virtual ~IAbility() = default;

    /// The stable discriminator for this ability's kind (e.g. `"role"`).
    [[nodiscard]] virtual std::string_view kind() const noexcept = 0;

    /// A deep, independent copy.
    [[nodiscard]] virtual AbilityPtr clone() const = 0;

    /// Presentation metadata for this ability's kind, sourced from the concrete
    /// type's `static display_info()` (empty when it defines none).
    [[nodiscard]] virtual const DisplayInfo& info() const = 0;

    /// Whether `candidate` satisfies *this* (the **required**) ability. Default:
    /// same `kind()` and same `value`. The requirement owns the rule — a kind
    /// with richer matching overrides this (see `RecordPermissionAbility`).
    [[nodiscard]] virtual bool allowed(const IAbility& candidate) const {
        return kind() == candidate.kind() && value == candidate.value;
    }

    /// Whether `who` satisfies *this* (the **required**) ability — forwards to
    /// `who.satisfies(*this)`. Declared here to break the Ability⇄Identity
    /// cycle; **defined in `commons/identity.hpp`**, so this overload needs that
    /// header.
    [[nodiscard]] bool allowed(const IIdentity& who) const;

    /// Value equality against another ability. Default compares
    /// `kind`/`value`/`id`/`metadata`; a kind with extra fields overrides this
    /// (an `AbilityPtr` cannot use a defaulted `operator==`).
    [[nodiscard]] virtual bool equals(const IAbility& other) const {
        return kind() == other.kind() && value == other.value && id == other.id &&
               metadata == other.metadata;
    }

    /// A human-readable title for `to_string` / `operator<<` / `std::format`.
    /// Defaults to the concrete type's name (e.g. `RoleAbility`).
    [[nodiscard]] virtual std::string title() const {
        return detail::demangle_type_name(typeid(*this));
    }

    /// Record an `id` from a strong-typed `Id`, captured as its string form.
    template <class Tag, class Repr>
    void set_id(const Id<Tag, Repr>& ident) {
        id = to_string(ident);
    }

#if COMMONS_WITH_NLOHMANN_JSON
    /// Write this ability's fields into `j`. The base writes `kind`/`value`,
    /// plus `id`/`metadata` when present/non-empty; a sub-ability overrides this
    /// to add its own fields — call `IAbility::write_json(j)` first. Gated on
    /// `COMMONS_WITH_NLOHMANN_JSON` (see the vtable caveat in the file header).
    virtual void write_json(nlohmann::json& j) const {
        j["kind"] = std::string{kind()};
        j["value"] = value;
        if (id) {
            j["id"] = *id;
        }
        if (!metadata.empty()) {
            j["metadata"] = metadata;  // comms::md::Object ⇄ json via ADL
        }
    }

    /// Read this ability's fields from `j` (absent keys keep their defaults;
    /// `kind` is fixed by the concrete type). Override to read your own fields —
    /// call `IAbility::read_json(j)` first.
    virtual void read_json(const nlohmann::json& j) {
        if (const auto it = j.find("value"); it != j.end() && !it->is_null()) {
            it->get_to(value);
        }
        if (const auto it = j.find("id"); it != j.end() && !it->is_null()) {
            id = it->template get<std::string>();
        }
        if (const auto it = j.find("metadata"); it != j.end() && !it->is_null()) {
            it->get_to(metadata);  // comms::md::Object ⇄ json via ADL
        }
    }
#endif

protected:
    // Protected to prevent slicing through the interface; derived types remain
    // copyable/movable via their own (implicitly defined) operations.
    IAbility() = default;
    IAbility(const IAbility&) = default;
    IAbility(IAbility&&) = default;
    IAbility& operator=(const IAbility&) = default;
    IAbility& operator=(IAbility&&) = default;
};

/// CRTP base wiring `kind()`, `clone()`, and `info()` from a compile-time kind
/// string and the concrete `Derived` type, over an `Interface` that is either
/// `IAbility` or a custom `IAbility` sub-interface. `Derived` need only be
/// copy-constructible (for `clone()`); it *may* expose a
/// `static const DisplayInfo& display_info()` for a real `info()`. Provides the
/// `()` / `(value)` constructors so a derived kind can `using AbilityKind::AbilityKind;`.
template <FixedString Kind, typename Derived, typename Interface = IAbility>
class AbilityKind : public Interface {
public:
    // KIND keeps a SCREAMING_CASE spelling as the type's identity tag, matching
    // OriginKind::KIND / ReasonKind::KIND.
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr std::string_view KIND = Kind.view();  ///< Compile-time discriminator.

    AbilityKind() = default;
    explicit AbilityKind(std::string val) {
        this->value = std::move(val);
    }

    [[nodiscard]] std::string_view kind() const noexcept override {
        return KIND;
    }

    [[nodiscard]] AbilityPtr clone() const override {
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }

    [[nodiscard]] const DisplayInfo& info() const override {
        // display_info() is optional: use the type's if it has one, else empty.
        if constexpr (detail::HasMemberDisplayInfo<Derived>) {
            return Derived::display_info();
        } else {
            return detail::empty_display_info();
        }
    }
};

// -- registry ----------------------------------------------------------------

/// A program-wide registry mapping an ability `kind` string to a factory. A
/// Meyers singleton, like `GlobalReasonRegistry`, so the map is constructed
/// before any `inline` registrar runs. Used by the JSON `from_json` path to turn
/// a `kind` discriminator back into the right concrete ability.
class GlobalAbilityRegistry {
public:
    /// Produces a fresh, default-constructed ability of one kind.
    using Factory = AbilityPtr (*)();

private:
    std::map<std::string, Factory, std::less<>> factories_;
    GlobalAbilityRegistry() = default;

public:
    GlobalAbilityRegistry(const GlobalAbilityRegistry&) = delete;
    GlobalAbilityRegistry& operator=(const GlobalAbilityRegistry&) = delete;
    GlobalAbilityRegistry(GlobalAbilityRegistry&&) = delete;
    GlobalAbilityRegistry& operator=(GlobalAbilityRegistry&&) = delete;
    ~GlobalAbilityRegistry() = default;

    [[nodiscard]] static GlobalAbilityRegistry& instance() noexcept {
        static GlobalAbilityRegistry r;
        return r;
    }

    /// Register `factory` under `kind`. Returns `false` if the kind was already
    /// registered (the first registration wins).
    bool register_kind(const std::string_view kind, Factory factory) {
        return factories_.emplace(std::string{kind}, factory).second;
    }

    /// Create a fresh ability for `kind`, or `nullptr` if the kind is unknown.
    [[nodiscard]] AbilityPtr create(const std::string_view kind) const {
        if (const auto it = factories_.find(kind); it != factories_.end()) {
            return it->second();
        }
        return nullptr;
    }

    /// Whether `kind` has a registered factory.
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

    /// The names of the registered kinds whose ability is-a `Base` — the generic
    /// filter (the way a custom `IAbility` sub-interface refines `IAbility`).
    /// Classifies by instantiating one of each kind and `dynamic_cast`-ing, so
    /// call it for setup/inspection rather than on a hot path.
    template <typename Base = IAbility>
        requires std::derived_from<Base, IAbility>
    [[nodiscard]] std::vector<std::string> kinds_of() const {
        std::vector<std::string> out;
        for (const auto& [name, factory] : factories_) {
            const AbilityPtr probe = factory();
            if (dynamic_cast<const Base*>(probe.get()) != nullptr) {
                out.push_back(name);
            }
        }
        return out;
    }
};

/// A self-registering object: constructing one registers `T`'s factory into the
/// `GlobalAbilityRegistry`. `COMMONS_REGISTER_ABILITY` emits one as an `inline`
/// object so registration happens at static init. `T` must be
/// default-constructible and expose a `static constexpr std::string_view KIND`.
template <typename T>
struct AbilityRegistrar {
    AbilityRegistrar() noexcept {
        GlobalAbilityRegistry::instance().register_kind(
            T::KIND, []() -> AbilityPtr { return std::make_unique<T>(); });
    }
};

}  // namespace comms

/// Register an already-defined ability type `Ident` into the
/// `GlobalAbilityRegistry`. Place it at namespace scope after the type.
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_REGISTER_ABILITY(Ident)                                                            \
    inline const ::comms::AbilityRegistrar<Ident> commons_ability_registrar_##Ident {}

/// Define **and** register an ability kind in one line. `Ident` is the C++ type
/// name, `Kind` the discriminator string literal. The generated type inherits
/// the `AbilityKind` constructors — `()` and `(value)` — so
/// `comms::make_ability<Ident>("x")` just works. For richer matching (an
/// overridden `allowed`) or a `display_info()`, write the class by hand against
/// `comms::AbilityKind<Kind, Ident>` instead.
///
///     COMMONS_DEFINE_ABILITY(ScopeAbility, "scope");
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_DEFINE_ABILITY(Ident, Kind)                                                        \
    class Ident final : public ::comms::AbilityKind<Kind, Ident> {                                 \
    public:                                                                                        \
        using ::comms::AbilityKind<Kind, Ident>::AbilityKind;                                      \
    };                                                                                             \
    COMMONS_REGISTER_ABILITY(Ident)

namespace comms {

// -- built-in kinds ----------------------------------------------------------

/// A coarse, generic capability identified by its `value`.
COMMONS_DEFINE_ABILITY(GenericAbility, "generic");

/// The capability is not known / was not specified.
COMMONS_DEFINE_ABILITY(UnknownAbility, "unknown");

/// A role (e.g. `"admin"`, `"editor"`) named by `value`. The default
/// `allowed()` (same kind + same value) is exactly role-name matching.
class RoleAbility final : public AbilityKind<"role", RoleAbility> {
public:
    using AbilityKind<"role", RoleAbility>::AbilityKind;

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Role",
            .description = "A named role.",
            .icon = Icon::from("mdi:account-key"),
        };
        return info;
    }
};

/// A permission to perform an `action` on a `resource` — the worked example of
/// an *extensible* kind. It carries two extra fields and overrides `allowed`
/// (action + resource match, with `"*"` as a wildcard on either side), `equals`,
/// and the JSON hooks so its fields round-trip.
class RecordPermissionAbility final
    : public AbilityKind<"record_permission", RecordPermissionAbility> {
public:
    std::string action;    ///< The operation (e.g. `"read"`, `"write"`, or `"*"`).
    std::string resource;  ///< The resource it applies to (e.g. `"order"`, or `"*"`).

    RecordPermissionAbility() = default;
    RecordPermissionAbility(std::string act, std::string res)
        : action(std::move(act)), resource(std::move(res)) {}

    using IAbility::allowed;  // keep the allowed(const IIdentity&) overload visible

    /// `candidate` satisfies this permission when their `action` and `resource`
    /// match — where `"*"` on either side matches anything.
    [[nodiscard]] bool allowed(const IAbility& candidate) const override {
        if (kind() != candidate.kind()) {
            return false;
        }
        // kind() match proves the dynamic type; static_cast avoids the RTTI check.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        const auto& other = static_cast<const RecordPermissionAbility&>(candidate);
        return field_matches(action, other.action) && field_matches(resource, other.resource);
    }

    [[nodiscard]] bool equals(const IAbility& other) const override {
        if (kind() != other.kind()) {
            return false;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        const auto& o = static_cast<const RecordPermissionAbility&>(other);
        return value == o.value && id == o.id && metadata == o.metadata && action == o.action &&
               resource == o.resource;
    }

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Record Permission",
            .description = "Permission to act on a record/resource.",
            .icon = Icon::from("mdi:shield-key"),
        };
        return info;
    }

#if COMMONS_WITH_NLOHMANN_JSON
    void write_json(nlohmann::json& j) const override {
        IAbility::write_json(j);  // kind/value/id/metadata
        j["action"] = action;
        j["resource"] = resource;
    }
    void read_json(const nlohmann::json& j) override {
        IAbility::read_json(j);
        if (const auto it = j.find("action"); it != j.end() && !it->is_null()) {
            it->get_to(action);
        }
        if (const auto it = j.find("resource"); it != j.end() && !it->is_null()) {
            it->get_to(resource);
        }
    }
#endif

private:
    [[nodiscard]] static bool field_matches(const std::string& required,
                                            const std::string& candidate) {
        return required == "*" || candidate == "*" || required == candidate;
    }
};

// The built-in kinds self-register so the JSON `from_json` dispatch resolves them.
COMMONS_REGISTER_ABILITY(RoleAbility);
COMMONS_REGISTER_ABILITY(RecordPermissionAbility);

// -- factory -----------------------------------------------------------------

/// Construct an ability on the heap. Defaults to `GenericAbility`; pass another
/// kind explicitly: `make_ability<RoleAbility>("admin")`.
template <typename A = GenericAbility, typename... Args>
    requires std::derived_from<A, IAbility>
[[nodiscard]] AbilityPtr make_ability(Args&&... args) {
    return std::make_unique<A>(std::forward<Args>(args)...);
}

// -- helpers / text output ---------------------------------------------------

/// Null-safe value equality over two `AbilityPtr`s (both null → equal; one null
/// → unequal; otherwise `a->equals(*b)`).
[[nodiscard]] inline bool ability_equal(const AbilityPtr& a, const AbilityPtr& b) {
    if (!a || !b) {
        return !a && !b;
    }
    return a->equals(*b);
}

/// An ability as `title: value`.
[[nodiscard]] inline std::string to_string(const IAbility& a) {
    return std::format("{}: {}", a.title(), a.value);
}

inline std::ostream& operator<<(std::ostream& os, const IAbility& a) {
    return os << to_string(a);
}

}  // namespace comms

// ---------------------------------------------------------------------------
// std::format support. A partial specialization constrained to IAbility-derived
// types, mirroring the IReason / IOrigin formatters.
// ---------------------------------------------------------------------------

// This spec-less formatter reads no member state, but `std::formatter` requires
// `parse`/`format` to be non-static members.
// NOLINTBEGIN(readability-convert-member-functions-to-static)

/// Formats any `comms::IAbility` (and derived) as `title: value`. No spec.
template <typename T>
    requires std::derived_from<T, comms::IAbility>
struct std::formatter<T> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: IAbility takes no format spec");
        }
        return it;
    }

    auto format(const comms::IAbility& a, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", comms::to_string(a));
    }
};  // namespace std

// NOLINTEND(readability-convert-member-functions-to-static)
