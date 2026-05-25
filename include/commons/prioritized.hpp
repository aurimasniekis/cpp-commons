#pragma once

/// @file
/// @brief Priorities for orderable things (adapters, transports, …) plus the
///        helpers to sort them deterministically — the C++ analog of Spring's
///        `Ordered` interface.
///
/// Lower priority value sorts first (higher precedence), mirroring Spring:
/// `HIGHEST_PRECEDENCE` is `INT_MIN`, `LOWEST_PRECEDENCE` is `INT_MAX`, and the
/// neutral `DEFAULT_PRIORITY` is `0`. The three sentinels are `static constexpr`
/// members of `comms::Prioritized`; a build system or consumer may override them
/// by predefining the matching `COMMONS_PRIORITIZED_*` macro (see below).
///
/// What this header provides:
///   - `comms::Prioritized` — a virtual-with-default interface. A bare
///     `struct Adapter : comms::Prioritized {};` already works at
///     `DEFAULT_PRIORITY`; override `priority()` to change it.
///   - `comms::Prioritizable<T>` — concept for any type exposing `.priority()`.
///   - `comms::get_priority(x)` — uniform priority lookup over values,
///     references, raw pointers and smart pointers (null-safe), falling back to
///     `DEFAULT_PRIORITY` when no priority is discoverable.
///   - `comms::PrioritizedCompare` / `comms::LenientPrioritizedCompare<T>` —
///     strict-weak comparators over `std::shared_ptr` (lower value first, with a
///     stable pointer tie-break).
///   - `comms::PrioritizedSet<T>` — a transparent `std::set<T>` (unique by `T`
///     value) whose iteration order is `(priority asc, insertion-order asc)`.
///   - `comms::PrioritizedBuilder<Derived>` — a CRTP mixin adding fluent
///     `priority()` / `highest_priority()` / `lowest_priority()` setters.
///   - `comms::WithPriority<T>` + `with_priority` / `make_prioritized` — attach a
///     priority to an existing value, by inheritance for non-final classes (a
///     true is-a `T`) or by composition otherwise.
///
/// Serialization (in `commons/json.hpp`, gated by `COMMONS_WITH_NLOHMANN_JSON`):
/// `WithPriority<T>` travels as `{"priority":N,"value":<T>}` and a
/// `PrioritizedSet<T>` as a JSON array in sorted order, both only when `T` is
/// itself json-serializable. The interface and mixins carry no JSON hooks.

#include <commons/types.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>

// ---------------------------------------------------------------------------
// Configurable sentinel values (override seam)
//
// These live here rather than in commons/config.hpp (which is reserved for the
// boolean COMMONS_WITH_* integration gates) so the umbrella does not force
// <limits> on every consumer. The static constexpr members of `Prioritized`
// below are the canonical surface; these macros exist only so a build system or
// consumer can predefine an override before this header is first included.
// ---------------------------------------------------------------------------

#if !defined(COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE)
#define COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE (std::numeric_limits<int>::min())
#endif
#if !defined(COMMONS_PRIORITIZED_LOWEST_PRECEDENCE)
#define COMMONS_PRIORITIZED_LOWEST_PRECEDENCE (std::numeric_limits<int>::max())
#endif
#if !defined(COMMONS_PRIORITIZED_DEFAULT_PRIORITY)
#define COMMONS_PRIORITIZED_DEFAULT_PRIORITY 0
#endif

namespace comms {

// ---------------------------------------------------------------------------
// Prioritized — abstract interface (virtual-with-default, not pure)
// ---------------------------------------------------------------------------

/// The orderable interface. `priority()` is virtual *with a default* rather than
/// pure, so a bare `struct Adapter : comms::Prioritized {};` compiles and reports
/// `DEFAULT_PRIORITY`; the mixin and both `WithPriority` flavors override it.
///
/// Lower value sorts first (higher precedence). The special members are
/// `protected` to guard against slicing through the interface (mirroring
/// `comms::IHasFlags`); derived types remain copyable/movable via their own
/// implicitly defined operations.
struct Prioritized {
    // The sentinels keep Spring's `Ordered` SCREAMING_CASE spelling rather than
    // the project's lower_case constant style — it is the recognized convention
    // for these specific values.
    // NOLINTBEGIN(readability-identifier-naming)
    /// Most-preferred priority (`INT_MIN`): sorts before everything else.
    static constexpr int HIGHEST_PRECEDENCE = COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE;
    /// Least-preferred priority (`INT_MAX`): sorts after everything else.
    static constexpr int LOWEST_PRECEDENCE = COMMONS_PRIORITIZED_LOWEST_PRECEDENCE;
    /// The neutral priority (`0`) reported when none is set.
    static constexpr int DEFAULT_PRIORITY = COMMONS_PRIORITIZED_DEFAULT_PRIORITY;
    // NOLINTEND(readability-identifier-naming)

    virtual ~Prioritized() = default;

    /// This object's priority; lower sorts first. Defaults to `DEFAULT_PRIORITY`.
    [[nodiscard]] virtual int priority() const noexcept {
        return DEFAULT_PRIORITY;
    }

protected:
    Prioritized() = default;
    Prioritized(const Prioritized&) = default;
    Prioritized(Prioritized&&) = default;
    Prioritized& operator=(const Prioritized&) = default;
    Prioritized& operator=(Prioritized&&) = default;
};

/// Any type exposing a `priority()` convertible to `int` — satisfied by types
/// deriving from `Prioritized` and by any standalone type with the method.
template <typename T>
concept Prioritizable = requires(const T& t) {
    { t.priority() } -> std::convertible_to<int>;
};

// ---------------------------------------------------------------------------
// get_priority — uniform priority lookup
// ---------------------------------------------------------------------------

namespace detail {

/// A type whose `.get()` yields something pointer-like (a smart pointer).
template <typename T>
concept PointerReturningGet = requires(const T& t) {
    { t.get() } -> std::convertible_to<const volatile void*>;
};

/// Priority of a pointee. `nullptr` → default; a `Prioritizable` pointee answers
/// directly; an unrelated *polymorphic* pointee is probed via `dynamic_cast`
/// (the `is_polymorphic_v` guard keeps the cast well-formed for non-polymorphic
/// `T`); otherwise the default. `noexcept`: a pointer `dynamic_cast` cannot throw.
template <typename T>
[[nodiscard]] inline int get_priority_ptr(const T* ptr) noexcept {
    if (ptr == nullptr) {
        return Prioritized::DEFAULT_PRIORITY;
    }
    if constexpr (Prioritizable<T>) {
        return ptr->priority();
    } else if constexpr (std::is_polymorphic_v<T>) {
        if (const auto* p = dynamic_cast<const Prioritized*>(ptr)) {
            return p->priority();
        }
        return Prioritized::DEFAULT_PRIORITY;
    } else {
        return Prioritized::DEFAULT_PRIORITY;
    }
}

}  // namespace detail

/// Priority of a value or reference. The `Prioritized`/`Prioritizable` branch is
/// checked **first** so a `Prioritizable` type that also exposes `.get()` is not
/// misrouted into the smart-pointer branch. Smart pointers forward to their
/// pointee; anything else reports `DEFAULT_PRIORITY`. Constrained off raw
/// pointers so those select the more specific overload below (template partial
/// ordering alone does not reliably prefer it).
template <typename T>
    requires(!std::is_pointer_v<T>)
[[nodiscard]] inline int get_priority(const T& value) noexcept {
    if constexpr (std::is_base_of_v<Prioritized, T> || Prioritizable<T>) {
        return value.priority();
    } else if constexpr (detail::PointerReturningGet<T>) {
        return detail::get_priority_ptr(value.get());
    } else {
        return Prioritized::DEFAULT_PRIORITY;
    }
}

/// Priority behind a raw pointer (null-safe). This overload is more specialized
/// than the value/reference one, so a raw pointer argument selects it.
template <typename T>
[[nodiscard]] inline int get_priority(const T* ptr) noexcept {
    return detail::get_priority_ptr(ptr);
}

// ---------------------------------------------------------------------------
// Comparators (strict-weak; lower value sorts first)
// ---------------------------------------------------------------------------

/// Orders `std::shared_ptr<Prioritized>` by ascending priority. Null pointers
/// are treated as `DEFAULT_PRIORITY`; equal priorities break the tie on the
/// stored address (so equal pointers compare `false`, satisfying strict-weak).
struct PrioritizedCompare {
    [[nodiscard]] bool operator()(const std::shared_ptr<Prioritized>& a,
                                  const std::shared_ptr<Prioritized>& b) const noexcept {
        const int pa = a ? a->priority() : Prioritized::DEFAULT_PRIORITY;
        if (const int pb = b ? b->priority() : Prioritized::DEFAULT_PRIORITY; pa != pb) {
            return pa < pb;
        }
        return std::less<const Prioritized*>{}(a.get(), b.get());
    }
};

/// Like `PrioritizedCompare`, but over `std::shared_ptr<T>` for an arbitrary `T`
/// that need not derive from `Prioritized`: the priority is resolved through
/// `get_priority` (which handles `Prioritizable` types and a `dynamic_cast`
/// probe). Passing the `shared_ptr` itself — not its raw `.get()` — is what lets
/// `get_priority` route correctly.
template <typename T>
struct LenientPrioritizedCompare {
    [[nodiscard]] bool operator()(const std::shared_ptr<T>& a,
                                  const std::shared_ptr<T>& b) const noexcept {
        const int pa = get_priority(a);
        if (const int pb = get_priority(b); pa != pb) {
            return pa < pb;
        }
        return std::less<const T*>{}(a.get(), b.get());
    }
};

// ---------------------------------------------------------------------------
// PrioritizedSet — a transparent std::set<T> with priority on top
// ---------------------------------------------------------------------------

/// A set that behaves like `std::set<T>` from the outside — **unique by `T`
/// value**, iterators yield `const T&`, every method is expressed in terms of
/// `T` — but whose iteration order is `(priority asc, insertion-order asc)`
/// rather than by `T` value. Identity is the `T` value only; priority is
/// metadata. Following `std::set`, `insert` never mutates an existing element:
/// re-inserting an equal `T` is a no-op that leaves the stored priority
/// unchanged — use `set_priority` to change it.
///
/// Priority is **snapshotted at insert** into a private `Item`, so later mutation
/// of an element's own priority cannot corrupt the tree's ordering invariant.
///
/// @note Deliberately omitted: `lower_bound`/`upper_bound`/`equal_range`/
/// `key_comp`/`value_comp` and the node-handle `extract`/`merge`. They assume the
/// container is ordered by `T`, but ordering here is by priority, so they have no
/// coherent meaning.
///
/// @note Complexity: `insert`, `find`, `erase(const T&)` and `set_priority` are
/// O(n) — uniqueness and value lookup cannot use the priority-keyed tree.
/// Iteration and `erase(iterator)` match `std::set`. This is intended for
/// config-sized collections (adapters, transports).
template <typename T>
class PrioritizedSet {
    static_assert(std::equality_comparable<T>,
                  "PrioritizedSet<T> requires T to be equality-comparable");

    /// The stored record: the value plus its snapshotted priority and a
    /// monotonic insertion index used only for tie-breaking.
    struct Item {
        T value;
        int priority;
        u64 order;
    };

    /// Orders `Item`s by `(priority asc, order asc)`. Never by `T` value.
    struct ItemCompare {
        [[nodiscard]] bool operator()(const Item& a, const Item& b) const noexcept {
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            return a.order < b.order;
        }
    };

    using set_type = std::set<Item, ItemCompare>;

public:
    using key_type = T;
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = const T&;
    using const_reference = const T&;

    /// A thin bidirectional adapter over `std::set<Item>::const_iterator` that
    /// projects each `Item` to its `const T& value` — the private `Item` is never
    /// observable, so iteration looks exactly like a `std::set<T>`'s.
    // STL-style lower_case name (not the project's CamelCase) so the type reads
    // as a standard container iterator at the call site.
    // NOLINTNEXTLINE(readability-identifier-naming)
    class const_iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const noexcept {
            return it_->value;
        }
        [[nodiscard]] pointer operator->() const noexcept {
            return &it_->value;
        }

        const_iterator& operator++() noexcept {
            ++it_;
            return *this;
        }
        const_iterator operator++(int) noexcept {
            const_iterator tmp = *this;
            ++it_;
            return tmp;
        }
        const_iterator& operator--() noexcept {
            --it_;
            return *this;
        }
        const_iterator operator--(int) noexcept {
            const_iterator tmp = *this;
            --it_;
            return tmp;
        }

        [[nodiscard]] bool operator==(const const_iterator&) const = default;

    private:
        friend class PrioritizedSet;
        explicit const_iterator(typename set_type::const_iterator it) noexcept : it_(it) {}
        typename set_type::const_iterator it_{};
    };

    using iterator = const_iterator;  // elements are immutable, like std::set keys
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = reverse_iterator;

    PrioritizedSet() = default;

    PrioritizedSet(std::initializer_list<T> init) {
        for (const auto& v : init) {
            insert(v);
        }
    }

    template <typename It>
    PrioritizedSet(It first, It last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    // -- std::set-shaped, T-facing API ---------------------------------------

    /// Insert `v` at its discovered priority (`get_priority(v)`, i.e.
    /// `DEFAULT_PRIORITY` for a plain `T`). No-op if an equal `T` is present.
    std::pair<iterator, bool> insert(const T& v) {
        return insert(get_priority(v), v);
    }

    std::pair<iterator, bool> insert(T&& v) {
        const int p = get_priority(v);
        return insert(p, std::move(v));
    }

    /// Priority-aware addition. If an equal `T` is already present this is a
    /// no-op returning `{existing, false}` and the passed `priority` is ignored
    /// (use `set_priority` to change a present element's priority).
    std::pair<iterator, bool> insert(int priority, T v) {
        if (const auto found = find_item(v); found != items_.end()) {
            return {const_iterator{found}, false};
        }
        const auto [it, ok] = items_.insert(
            Item{.value = std::move(v), .priority = priority, .order = next_order_++});
        return {const_iterator{it}, ok};
    }

    /// Construct a `T` from `args` and insert it at its discovered priority.
    template <typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        return insert(T(std::forward<Args>(args)...));
    }

    void insert(std::initializer_list<T> init) {
        for (const auto& v : init) {
            insert(v);
        }
    }

    template <typename It>
    void insert(It first, It last) {
        for (; first != last; ++first) {
            insert(*first);
        }
    }

    iterator erase(const_iterator pos) {
        return const_iterator{items_.erase(pos.it_)};
    }

    iterator erase(const_iterator first, const_iterator last) {
        return const_iterator{items_.erase(first.it_, last.it_)};
    }

    /// Erase the element equal to `v`. Returns 0 or 1.
    size_type erase(const T& v) {
        const auto it = find_item(v);
        if (it == items_.end()) {
            return 0;
        }
        items_.erase(it);
        return 1;
    }

    /// Linear lookup by value; `end()` when absent.
    [[nodiscard]] iterator find(const T& v) const {
        return const_iterator{find_item(v)};
    }

    /// 0 or 1 — elements are unique by value.
    [[nodiscard]] size_type count(const T& v) const {
        return contains(v) ? 1U : 0U;
    }

    [[nodiscard]] bool contains(const T& v) const {
        return find_item(v) != items_.end();
    }

    // -- priority-specific extras --------------------------------------------

    /// The snapshotted priority of `v`, or `DEFAULT_PRIORITY` if absent.
    [[nodiscard]] int priority_of(const T& v) const {
        const auto it = find_item(v);
        return it != items_.end() ? it->priority : Prioritized::DEFAULT_PRIORITY;
    }

    /// Re-snapshot `v`'s priority and reorder it, keeping its insertion-order
    /// tie-break. Returns `false` if `v` is absent.
    bool set_priority(const T& v, int priority) {
        const auto it = find_item(v);
        if (it == items_.end()) {
            return false;
        }
        if (it->priority == priority) {
            return true;
        }
        Item updated = *it;  // preserves the original `order` tie-break
        updated.priority = priority;
        items_.erase(it);
        items_.insert(std::move(updated));
        return true;
    }

    [[nodiscard]] size_type size() const noexcept {
        return items_.size();
    }
    [[nodiscard]] bool empty() const noexcept {
        return items_.empty();
    }

    /// Remove all elements. Does **not** reset the insertion-order counter, which
    /// is monotonic for the lifetime of the set.
    void clear() noexcept {
        items_.clear();
    }

    void swap(PrioritizedSet& other) noexcept {
        items_.swap(other.items_);
        std::swap(next_order_, other.next_order_);
    }

    [[nodiscard]] iterator begin() const noexcept {
        return const_iterator{items_.begin()};
    }
    [[nodiscard]] iterator end() const noexcept {
        return const_iterator{items_.end()};
    }
    [[nodiscard]] iterator cbegin() const noexcept {
        return begin();
    }
    [[nodiscard]] iterator cend() const noexcept {
        return end();
    }
    [[nodiscard]] reverse_iterator rbegin() const noexcept {
        return reverse_iterator{end()};
    }
    [[nodiscard]] reverse_iterator rend() const noexcept {
        return reverse_iterator{begin()};
    }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept {
        return rbegin();
    }
    [[nodiscard]] const_reverse_iterator crend() const noexcept {
        return rend();
    }

    /// Element-wise comparison over the ordered `T` sequence.
    [[nodiscard]] friend bool operator==(const PrioritizedSet& a, const PrioritizedSet& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    }

private:
    [[nodiscard]] typename set_type::const_iterator find_item(const T& v) const {
        for (auto it = items_.begin(); it != items_.end(); ++it) {
            if (it->value == v) {
                return it;
            }
        }
        return items_.end();
    }

    set_type items_;
    u64 next_order_ = 0;
};

template <typename T>
void swap(PrioritizedSet<T>& a, PrioritizedSet<T>& b) noexcept {
    a.swap(b);
}

// ---------------------------------------------------------------------------
// PrioritizedBuilder — CRTP mixin (mirrors FlagBuilderMixin)
// ---------------------------------------------------------------------------

/// CRTP mixin that makes `Derived` a `Prioritized` carrying a mutable priority,
/// with fluent setters returning `Derived&` for chaining.
///
/// @note `int priority() const` (the getter/override) and `Derived& priority(int)`
/// (the fluent setter) form an overload set in the **same** class scope, resolved
/// by arity — this is legal, with no name hiding and no `using` needed, because
/// both are introduced here.
template <typename Derived>
class PrioritizedBuilder : public Prioritized {
public:
    [[nodiscard]] int priority() const noexcept override {
        return priority_;
    }

    /// Set the priority. Same-scope overload of the `priority()` getter above.
    Derived& priority(const int p) noexcept {
        priority_ = p;
        return self();
    }

    Derived& highest_priority() noexcept {
        priority_ = HIGHEST_PRECEDENCE;
        return self();
    }

    Derived& lowest_priority() noexcept {
        priority_ = LOWEST_PRECEDENCE;
        return self();
    }

protected:
    int priority_ = Prioritized::DEFAULT_PRIORITY;

private:
    Derived& self() noexcept {
        return static_cast<Derived&>(*this);
    }
};

// ---------------------------------------------------------------------------
// WithPriority — attach a priority to an existing value
// ---------------------------------------------------------------------------

/// Whether `WithPriority<T>` attaches its priority by inheritance (a true is-a
/// `T`): only for non-final class types. Final classes and fundamentals fall
/// back to composition.
template <typename T>
inline constexpr bool with_priority_inherits = std::is_class_v<T> && !std::is_final_v<T>;

namespace detail {

/// Inheritance flavor: `WithPriority<T>` *is a* `T` (and a `Prioritized`). Used
/// for non-final class types so the wrapper is usable wherever a `T&` is.
template <typename T>
class WithPriorityInherit : public T, public Prioritized {
public:
    using value_type = T;

    template <typename... Args>
    explicit WithPriorityInherit(const int p, Args&&... args)
        : T(std::forward<Args>(args)...), priority_(p) {}

    [[nodiscard]] int priority() const noexcept override {
        return priority_;
    }
    void set_priority(const int p) noexcept {
        priority_ = p;
    }

    [[nodiscard]] T& value() noexcept {
        return static_cast<T&>(*this);
    }
    [[nodiscard]] const T& value() const noexcept {
        return static_cast<const T&>(*this);
    }

protected:
    int priority_ = Prioritized::DEFAULT_PRIORITY;
};

/// Composition flavor: `WithPriority<T>` *holds* a `T`. Used for final classes
/// and fundamental types, which cannot serve as a base.
template <typename T>
class WithPriorityCompose : public Prioritized {
public:
    using value_type = T;

    template <typename... Args>
    explicit WithPriorityCompose(const int p, Args&&... args)
        : value_(std::forward<Args>(args)...), priority_(p) {}

    [[nodiscard]] int priority() const noexcept override {
        return priority_;
    }
    void set_priority(const int p) noexcept {
        priority_ = p;
    }

    [[nodiscard]] T& value() noexcept {
        return value_;
    }
    [[nodiscard]] const T& value() const noexcept {
        return value_;
    }

    [[nodiscard]] T& operator*() noexcept {
        return value_;
    }
    [[nodiscard]] const T& operator*() const noexcept {
        return value_;
    }
    [[nodiscard]] T* operator->() noexcept {
        return &value_;
    }
    [[nodiscard]] const T* operator->() const noexcept {
        return &value_;
    }

protected:
    T value_{};
    int priority_ = Prioritized::DEFAULT_PRIORITY;
};

template <typename T>
using WithPriorityBase =
    std::conditional_t<with_priority_inherits<T>, WithPriorityInherit<T>, WithPriorityCompose<T>>;

}  // namespace detail

/// A value of `T` carrying a priority. For a non-final class `T` it inherits
/// `T` (a true is-a `T`, usable wherever `T&` is expected); otherwise it holds a
/// `T` accessible through `value()` / `operator*` / `operator->`. Either way it
/// is a `Prioritized`. Construct it via `with_priority` / `make_prioritized`, or
/// directly: the inherited constructor's **first argument is always the
/// priority**, the rest forward to `T`.
template <typename T>
class WithPriority : public detail::WithPriorityBase<T> {
    using base = detail::WithPriorityBase<T>;

public:
    using value_type = T;
    using base::base;

    /// Default-constructs a `T` at `DEFAULT_PRIORITY` — available only when `T`
    /// is default-constructible (needed for nlohmann's `get<WithPriority<T>>()`).
    WithPriority()
        requires std::default_initializable<T>
        : base(Prioritized::DEFAULT_PRIORITY) {}
};

/// Wrap `item` with priority `p`. The wrapper's `T` is `std::remove_cvref_t<T>`,
/// chosen by `with_priority_inherits` between the inheritance and composition
/// flavors.
template <typename T>
[[nodiscard]] WithPriority<std::remove_cvref_t<T>> with_priority(int p, T&& item) {
    return WithPriority<std::remove_cvref_t<T>>(p, std::forward<T>(item));
}

/// Make a `shared_ptr<WithPriority<T>>` (for use with the comparators / a
/// `std::set`). The first argument is the priority; the rest construct the `T`.
template <typename T, typename... Args>
[[nodiscard]] std::shared_ptr<WithPriority<T>> make_prioritized(int p, Args&&... args) {
    return std::make_shared<WithPriority<T>>(p, std::forward<Args>(args)...);
}

}  // namespace comms

// ---------------------------------------------------------------------------
// Override seam macros (mirror flag.hpp's trailing declaration macros). These
// only take effect if predefined before this header is first included; the
// static constexpr members of `comms::Prioritized` are the canonical surface.
// ---------------------------------------------------------------------------

/// @def COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE
/// Override for `comms::Prioritized::HIGHEST_PRECEDENCE` (default `INT_MIN`).
/// @def COMMONS_PRIORITIZED_LOWEST_PRECEDENCE
/// Override for `comms::Prioritized::LOWEST_PRECEDENCE` (default `INT_MAX`).
/// @def COMMONS_PRIORITIZED_DEFAULT_PRIORITY
/// Override for `comms::Prioritized::DEFAULT_PRIORITY` (default `0`).
