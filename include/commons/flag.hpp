#pragma once

/// @file
/// @brief Compile-time named flags grouped into categories, plus a runtime
///        `FlagSet`, a program-wide `GlobalFlagRegistry`, and declaration macros.
///
/// A `comms::Flag<Name, Category>` is a distinct type identified by a
/// `FixedString` name and belonging to a `comms::FlagCategory<Name>` (defaulting
/// to `comms::UnsetFlagCategory`). Both the name and the category name are
/// exposed as `static constexpr std::string_view` members — mirroring
/// `IconifySet<Set>::set` — so they live in the flag type's static, NTTP-backed
/// storage and stay valid for the whole program.
///
/// A flag may carry presentation metadata: give it a
/// `static const DisplayInfo& display_info()` member (see
/// `commons/display_info.hpp`) and it becomes `Displayable`; `FlagRef::of<F>()`
/// then captures a pointer to it. `Flag` does **not** inherit the display trait,
/// so a plain flag is simply not `Displayable`.
///
/// `comms::FlagRef` is a type-erased, non-owning runtime handle (name +
/// category + optional display pointer) whose views point at that static
/// storage. `comms::FlagSet` is an insertion-ordered set of unique flags
/// (deduplicated by name) exposing `std::set`-style methods plus
/// `group_by_category()`. `comms::GlobalFlagRegistry` is a Meyers singleton that
/// knows about every registered flag.
///
/// Flags self-register through the define macros (each emits a self-registering
/// `inline` object) or explicitly via `COMMONS_REGISTER_FLAG` /
/// `GlobalFlagRegistry::add`.
///
/// A small family of mixins lets a type *own* a `FlagSet`. `comms::IHasFlags`
/// is the abstract read interface (`flags()` / `has_flag()`) for polymorphic
/// access; `comms::HasFlags<Categories...>` implements it as a plain holder.
/// `comms::FlagBuilderMixin<Derived, Categories...>` (CRTP) adds fluent
/// `set`/`insert`/`remove` mutators returning `Derived&` while keeping read
/// access private (a "silent" builder), and
/// `comms::FlagBuilderGetters<Derived, Categories...>` combines those mutators
/// with the public, `IHasFlags`-backed read API. Listing `Categories...`
/// constrains the typed overloads to flags in those categories; an empty list
/// accepts any flag.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// a `FlagRef` travels as its name string and a `FlagSet` as a JSON array of
/// names; reading them back resolves each name against the `GlobalFlagRegistry`.

#include <commons/display_info.hpp>
#include <commons/fixed_string.hpp>
#include <commons/types.hpp>

#include <algorithm>
#include <concepts>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace comms {

// ---------------------------------------------------------------------------
// Compile-time flag / category types
// ---------------------------------------------------------------------------

/// A flag category, identified by a `FixedString` name. The name is exposed as a
/// `static constexpr std::string_view` backed by the NTTP's static storage.
template <FixedString Name>
struct FlagCategory {
    static constexpr std::string_view name = Name.view();
};

/// The default category for a `Flag` declared without an explicit one.
struct UnsetFlagCategory : FlagCategory<"unset"> {};

/// A compile-time flag: a distinct type named by `Name` and belonging to
/// `Category` (defaulting to `UnsetFlagCategory`). The `category` alias exposes
/// the category type; `category_name` mirrors `Category::name` for convenience.
template <FixedString Name, typename Category = UnsetFlagCategory>
struct Flag {
    static constexpr std::string_view name = Name.view();
    using category = Category;
    static constexpr std::string_view category_name = Category::name;
};

// ---------------------------------------------------------------------------
// Concepts
// ---------------------------------------------------------------------------

/// Any type that exposes a `name` convertible to `std::string_view` — satisfied
/// by `FlagCategory<Name>` and types deriving from it.
template <typename C>
concept AnyFlagCategory = requires {
    { C::name } -> std::convertible_to<std::string_view>;
};

/// Any flag type: a `name` plus a `category` member type that is itself an
/// `AnyFlagCategory`.
template <typename F>
concept AnyFlag = requires {
    { F::name } -> std::convertible_to<std::string_view>;
    typename F::category;
    requires AnyFlagCategory<typename F::category>;
};

/// A flag whose category is exactly `Category`.
template <typename F, typename Category>
concept FlagInCategory = AnyFlag<F> && std::same_as<typename F::category, Category>;

// ---------------------------------------------------------------------------
// FlagRef — type-erased runtime handle
// ---------------------------------------------------------------------------

/// A type-erased, non-owning runtime handle to a flag. `name` and `category`
/// are views into the flag type's static NTTP-backed storage, valid for the
/// program lifetime (the same guarantee `IconifySet::set` relies on). `display`
/// points at the flag's `DisplayInfo` when it is `Displayable`, else is null.
///
/// Identity is the flag name: two `FlagRef`s compare equal iff their names match.
struct FlagRef {
    std::string_view name;                 ///< Flag name; also the identity used for `==`.
    std::string_view category;             ///< Owning category name.
    const DisplayInfo* display = nullptr;  ///< null when the flag is not Displayable

    /// Build a `FlagRef` from a compile-time flag type, capturing its display
    /// metadata only if `F` is `Displayable`.
    template <AnyFlag F>
    [[nodiscard]] static FlagRef of() noexcept {
        FlagRef r{.name = F::name, .category = F::category::name};
        if constexpr (Displayable<F>) {
            r.display = &display_info<F>();
        }
        return r;
    }

    bool operator==(const FlagRef& o) const noexcept {
        return name == o.name;
    }
};

// ---------------------------------------------------------------------------
// FlagSet — insertion-ordered unique set
// ---------------------------------------------------------------------------

/// An insertion-ordered set of unique flags, deduplicated by name. Backed by a
/// `std::vector<FlagRef>` so iteration preserves insertion order — hence it
/// wraps a vector and exposes `std::set`-style names rather than deriving from
/// `std::set`, which would sort.
class FlagSet {
    std::vector<FlagRef> items_;

public:
    /// Insert a flag by type. Returns `true` if it was newly added, `false` if a
    /// flag with the same name was already present.
    template <AnyFlag F>
    bool insert() {
        return insert(FlagRef::of<F>());
    }

    /// Insert a `FlagRef`, deduplicating by name. Returns `true` if newly added.
    bool insert(const FlagRef& f) {
        if (contains(f.name)) {
            return false;
        }
        items_.push_back(f);
        return true;
    }

    template <AnyFlag F>
    [[nodiscard]] bool contains() const {
        return contains(F::name);
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return std::ranges::any_of(items_, [name](const FlagRef& f) { return f.name == name; });
    }

    /// 0 or 1 — flags are unique by name, so this mirrors `std::set::count`.
    [[nodiscard]] std::size_t count(const std::string_view name) const {
        return contains(name) ? 1U : 0U;
    }

    template <AnyFlag F>
    bool erase() {
        return erase(F::name);
    }

    /// Erase the flag with the given name. Returns `true` if one was removed.
    bool erase(const std::string_view name) {
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (it->name == name) {
                items_.erase(it);
                return true;
            }
        }
        return false;
    }

    void clear() noexcept {
        items_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        return items_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return items_.size();
    }

    [[nodiscard]] auto begin() const noexcept {
        return items_.begin();
    }

    [[nodiscard]] auto end() const noexcept {
        return items_.end();
    }

    /// Flags grouped by category name. Within each group, insertion order is
    /// preserved; the outer map is ordered by category name (`std::map`).
    [[nodiscard]] std::map<std::string_view, std::vector<FlagRef>> group_by_category() const {
        std::map<std::string_view, std::vector<FlagRef>> groups;
        for (const auto& f : items_) {
            groups[f.category].push_back(f);
        }
        return groups;
    }

    bool operator==(const FlagSet&) const = default;  // order-sensitive
};

// ---------------------------------------------------------------------------
// GlobalFlagRegistry — Meyers singleton
// ---------------------------------------------------------------------------

/// A program-wide registry of every known flag. A Meyers singleton, so the
/// underlying `FlagSet` is constructed before any `inline` registrar runs.
class GlobalFlagRegistry {
    FlagSet flags_;
    GlobalFlagRegistry() = default;

public:
    GlobalFlagRegistry(const GlobalFlagRegistry&) = delete;
    GlobalFlagRegistry& operator=(const GlobalFlagRegistry&) = delete;
    GlobalFlagRegistry(GlobalFlagRegistry&&) = delete;
    GlobalFlagRegistry& operator=(GlobalFlagRegistry&&) = delete;
    ~GlobalFlagRegistry() = default;

    [[nodiscard]] static GlobalFlagRegistry& instance() noexcept {
        static GlobalFlagRegistry r;
        return r;
    }

    template <AnyFlag F>
    bool add() {
        return flags_.insert<F>();
    }

    bool add(const FlagRef& f) {
        return flags_.insert(f);
    }

    [[nodiscard]] const FlagSet& flags() const noexcept {
        return flags_;
    }

    /// Resolve a flag by name. Used by the JSON `from_json` path.
    [[nodiscard]] std::optional<FlagRef> find(const std::string_view name) const {
        for (const auto& f : flags_) {
            if (f.name == name) {
                return f;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto group_by_category() const {
        return flags_.group_by_category();
    }
};

/// A self-registering object: constructing one registers `F` into the
/// `GlobalFlagRegistry`. The define macros emit one of these as an `inline`
/// object so the registration happens at static init.
template <AnyFlag F>
struct FlagRegistrar {
    FlagRegistrar() noexcept {
        GlobalFlagRegistry::instance().add<F>();
    }
};

// ---------------------------------------------------------------------------
// Mixins for types that own a FlagSet
// ---------------------------------------------------------------------------

/// Whether flag `F` may go into a set constrained to `Categories...`: any flag
/// when the list is empty (unconstrained), otherwise only flags whose category
/// is one of `Categories...`.
template <typename F, typename... Categories>
concept AllowedFlag =
    AnyFlag<F> && (sizeof...(Categories) == 0 || (FlagInCategory<F, Categories> || ...));

/// Abstract read-only view of a type that owns a `FlagSet`. Lets a caller hold a
/// `const IHasFlags&`/`*` to any flag owner — a `HasFlags` holder or a
/// `FlagBuilderGetters` builder — and query it polymorphically. The single pure
/// virtual is `flags()`; the `has_flag` helpers are built on top of it.
class IHasFlags {
public:
    virtual ~IHasFlags() = default;

    /// The owned flag set (insertion order preserved).
    [[nodiscard]] virtual const FlagSet& flags() const noexcept = 0;

    template <AnyFlag F>
    [[nodiscard]] bool has_flag() const {
        return flags().template contains<F>();
    }

    [[nodiscard]] bool has_flag(const std::string_view name) const {
        return flags().contains(name);
    }

protected:
    // Protected to prevent slicing through the interface; derived types remain
    // copyable/movable via their own (implicitly defined) operations.
    IHasFlags() = default;
    IHasFlags(const IHasFlags&) = default;
    IHasFlags(IHasFlags&&) = default;
    IHasFlags& operator=(const IHasFlags&) = default;
    IHasFlags& operator=(IHasFlags&&) = default;
};

/// Implementation of `IHasFlags`: embeds a `FlagSet` and exposes it publicly.
/// Use it for a plain holder. List `Categories...` to record which categories
/// the holder is meant for (surfaced via `flag_allowed`); leave it empty for any.
///
/// `flags_` is `protected` so a deriving holder can populate it; outside code
/// reads through the inherited `flags()` / `has_flag()`.
template <typename... Categories>
class HasFlags : public IHasFlags {
public:
    [[nodiscard]] const FlagSet& flags() const noexcept override {
        return flags_;
    }

    /// True when a flag in `F`'s category is permitted by this set's constraint.
    template <AnyFlag F>
    static constexpr bool flag_allowed = AllowedFlag<F, Categories...>;

protected:
    FlagSet flags_;
};

/// CRTP helper for builders. Owns its own `FlagSet` and adds fluent mutators
/// that return `Derived&` for chaining.
///
/// It does **not** expose the set publicly or implement `IHasFlags`: `flags()`
/// is `protected`, used internally. A builder that should keep its flags to
/// itself inherits this directly; one that should be observable inherits
/// `FlagBuilderGetters` instead.
///
/// The typed overloads are constrained to allowed categories; the runtime
/// `FlagRef`/name overloads are unconstrained (the category is only known at
/// compile time), so prefer the typed ones to keep the constraint.
template <typename Derived, typename... Categories>
class FlagBuilderMixin {
public:
    /// True when a flag in `F`'s category is permitted by this builder's constraint.
    template <AnyFlag F>
    static constexpr bool flag_allowed = AllowedFlag<F, Categories...>;

    /// Insert flag `F` (no-op if already present). Constrained to allowed
    /// categories.
    template <AnyFlag F>
        requires AllowedFlag<F, Categories...>
    Derived& insert_flag() {
        flags_.template insert<F>();
        return self();
    }

    Derived& insert_flag(const FlagRef& f) {
        flags_.insert(f);
        return self();
    }

    /// Alias for `insert_flag` that reads naturally in a fluent chain
    /// (`builder.flag<F>()`).
    template <AnyFlag F>
        requires AllowedFlag<F, Categories...>
    Derived& flag() {
        return insert_flag<F>();
    }

    Derived& flag(const FlagRef& f) {
        return insert_flag(f);
    }

    /// Set flag `F`'s presence: insert when `on`, erase otherwise.
    template <AnyFlag F>
        requires AllowedFlag<F, Categories...>
    Derived& set_flag(const bool on) {
        if (on) {
            flags_.template insert<F>();
        } else {
            flags_.template erase<F>();
        }
        return self();
    }

    template <AnyFlag F>
    Derived& remove_flag() {
        flags_.template erase<F>();
        return self();
    }

    Derived& remove_flag(const std::string_view name) {
        flags_.erase(name);
        return self();
    }

    /// Replace the whole set.
    Derived& set_flags(FlagSet flags) {
        flags_ = std::move(flags);
        return self();
    }

    Derived& clear_flags() {
        flags_.clear();
        return self();
    }

protected:
    FlagSet flags_;

    /// Read access kept `protected` and used internally; `FlagBuilderGetters`
    /// publishes it through `IHasFlags`.
    [[nodiscard]] const FlagSet& flags() const noexcept {
        return flags_;
    }

private:
    Derived& self() noexcept {
        return static_cast<Derived&>(*this);
    }
};

/// A builder that is also observable: combines `FlagBuilderMixin`'s fluent
/// mutators with a public, `IHasFlags`-backed read API. Use this when a builder
/// should be readable through `IHasFlags`; use the bare `FlagBuilderMixin` when
/// it should keep its flags to itself.
template <typename Derived, typename... Categories>
class FlagBuilderGetters : public FlagBuilderMixin<Derived, Categories...>, public IHasFlags {
public:
    [[nodiscard]] const FlagSet& flags() const noexcept override {
        return this->flags_;
    }
};

}  // namespace comms

// ---------------------------------------------------------------------------
// Declaration macros (define-then-use, mirroring COMMONS_MUI_FAMILY). Each
// expands to a declaration; the caller supplies the trailing `;`.
//
// Registration caveat: the `inline` registrar registers once at static init
// (the Meyers singleton guarantees the registry exists first), so query the
// registry at or after `main`. In a *static library*, an unreferenced registrar
// may be stripped unless the archive is linked whole (e.g. `--whole-archive` /
// `-force_load`); reference the flag, or register explicitly, if that matters.
// ---------------------------------------------------------------------------

/// Declare a flag category type `Ident` named `Name`.
#define COMMONS_FLAG_CATEGORY(Ident, Name)                                                         \
    struct Ident : ::comms::FlagCategory<Name> {}

/// Declare a flag type `Ident` named `Name` in `UnsetFlagCategory`.
#define COMMONS_FLAG(Ident, Name)                                                                  \
    struct Ident : ::comms::Flag<Name> {}

/// Declare a flag type `Ident` named `Name` in category `Cat`.
#define COMMONS_FLAG_IN(Ident, Name, Cat)                                                          \
    struct Ident : ::comms::Flag<Name, Cat> {}

/// Register an already-defined flag type `Ident` into the `GlobalFlagRegistry`.
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_REGISTER_FLAG(Ident)                                                               \
    inline const ::comms::FlagRegistrar<Ident> commons_flag_registrar_##Ident {}

/// Define a flag type `Ident` named `Name` and auto-register it.
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_DEFINE_FLAG(Ident, Name)                                                           \
    struct Ident : ::comms::Flag<Name> {};                                                         \
    inline const ::comms::FlagRegistrar<Ident> commons_flag_registrar_##Ident

/// Define a flag type `Ident` named `Name` in category `Cat` and auto-register it.
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_DEFINE_FLAG_IN(Ident, Name, Cat)                                                   \
    struct Ident : ::comms::Flag<Name, Cat> {};                                                    \
    inline const ::comms::FlagRegistrar<Ident> commons_flag_registrar_##Ident

/// Declare a whole flag family in one line: a category `CatIdent` named `Name`, a
/// per-category flag template `FlagTmpl<N>`, and a concept `ConceptName`
/// satisfied by flags in that category.
// `ConceptName` is a concept name and `FlagTmpl` a template name — neither can be
// parenthesized, so the macro-parentheses check is suppressed here.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define COMMONS_FLAG_FAMILY(CatIdent, Name, FlagTmpl, ConceptName)                                 \
    struct CatIdent : ::comms::FlagCategory<Name> {};                                              \
    template <::comms::FixedString N>                                                              \
    struct FlagTmpl : ::comms::Flag<N, CatIdent> {};                                               \
    template <typename F>                                                                          \
    concept ConceptName = ::comms::FlagInCategory<F, CatIdent>
// NOLINTEND(bugprone-macro-parentheses)
