#pragma once

/// @file
/// @brief `comms::IReason` — a polymorphic envelope explaining *why* something
///        happened (a reject, a cancel, …) — and its `comms::IFailureReason`
///        refinement for developer-controlled failures.
///
/// A `Reason` answers "why?" for an outcome that is **not** an exception: a
/// rejected request, a cancelled job, a refused operation. It carries a numeric
/// `code`, a human `message`, and a `created_at` timestamp, and — exactly like
/// `comms::IOrigin` — it is an **open set**: each concrete kind identifies itself
/// with a `kind()` discriminator supplied as a compile-time `comms::FixedString`
/// template parameter via the `ReasonKind<"kind", Derived>` CRTP base (the
/// `IconifySet<FixedString Set>` pattern), which also wires `clone()` and an
/// (optional) `DisplayInfo`-backed `info()`. New kinds self-register into the
/// program-wide `GlobalReasonRegistry` via `COMMONS_REGISTER_REASON(Type)`
/// (mirroring `GlobalOriginRegistry`/`COMMONS_REGISTER_ORIGIN`).
///
/// For the common case, the one-line `COMMONS_DEFINE_REASON(Ident, "kind")` /
/// `COMMONS_DEFINE_FAILURE_REASON(Ident, "kind")` macros define *and* register a
/// kind, inheriting the `ReasonKind` constructors so
/// `make_reason<Ident>(code, "msg")` works out of the box — you only hand-write a
/// class (against `ReasonKind` / `FailureReasonKind`) when you want a baked-in
/// default or a `display_info()`. `display_info()` is optional: a kind without
/// one simply gets an empty `info()`.
///
/// `comms::IFailureReason` refines `IReason` for the case where code *submits a
/// failure* deliberately — a developer-controlled "error" that is not an
/// exception. It adds `throw_as_exception()`, which packages the reason into a
/// `FailureReasonException` for the rare site that has no failure channel of its
/// own; a handler upstack catches it and resubmits.
///
/// Built-in kinds are `GenericReason`/`UnknownReason` and the failure
/// counterparts `GenericFailureReason`/`UnknownFailureReason`. `ReasonPtr` /
/// `FailureReasonPtr` are the owning `std::unique_ptr` handles; `make_reason<R>`
/// / `make_failure_reason<R>` are the factory helpers (defaulting to the generic
/// kinds).
///
/// Ordering: a free `operator<=>` compares two reasons by `created_at` (the C++
/// analog of the Java `Reason implements Comparable` by creation time).
///
/// Exceptions (rooted at `comms::Exception`):
/// `ReasonException` carries any `IReason`; `FailureReasonException` (a
/// `ReasonException`) carries an `IFailureReason` and is what
/// `throw_as_exception()` throws; `RejectException` / `CancelException` are
/// `ReasonException`s for the two most common reject/cancel sites.
///
/// Every reason also carries an optional `metadata` bag — a `comms::Metadata`
/// (`comms::md::Object`), empty by default — for arbitrary structured context
/// (attempt counts, offending values, …). It is a public member, set directly
/// like `code`/`message`.
///
/// Serialization (gated by `COMMONS_WITH_NLOHMANN_JSON`): a reason travels as
/// `{"kind", "code", "message", "created_at"}` (the timestamp as epoch
/// milliseconds), plus a `"metadata"` object when that bag is non-empty (omitted
/// when empty). Uniquely among the Commons types, the per-field (de)serializers
/// are the **virtual** `IReason::write_json` / `read_json` hooks *in this header*
/// (gated, so nlohmann is still only pulled when present) — a sub-reason adds its
/// own fields by overriding them (calling the base first), and they round-trip
/// through `ReasonPtr` automatically. `commons/json.hpp` only supplies the
/// `adl_serializer<ReasonPtr>` / `<FailureReasonPtr>` that drive those hooks and
/// resolve the `kind` through the registry. **Caveat:** because the gate adds
/// virtual members, it changes `IReason`'s vtable — every translation unit in a
/// build must resolve `COMMONS_WITH_NLOHMANN_JSON` identically (force it with a
/// `-D` if your build is mixed).
///
/// Text output (always available): `to_string`, `operator<<`, and `std::format`
/// all emit `title: message`. `title()` defaults to the concrete type's name, so
/// a custom kind reads `RateLimitReason: …`; the generic/unknown built-ins
/// override it to also carry their code — `GenericReason(123): …`. Override
/// `title()` per kind to customize.

#include <commons/config.hpp>
#include <commons/display_info.hpp>
#include <commons/exception.hpp>
#include <commons/fixed_string.hpp>
#include <commons/metadata.hpp>

#include <chrono>
#include <compare>
#include <concepts>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <ranges>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

#if COMMONS_WITH_NLOHMANN_JSON
#include <commons/json/metadata.hpp>  // comms::md::Object ⇄ json, for the metadata field

#include <nlohmann/json.hpp>

#include <cstdint>  // std::int64_t, used only by the gated read_json below
#endif

namespace comms {

/// The wall-clock type stamped onto every reason at construction.
using ReasonClock = std::chrono::system_clock;

namespace detail {
/// A human title for `ti`: the demangled type name reduced to its final
/// identifier (enclosing namespaces — including `(anonymous namespace)` — and
/// any template arguments stripped). Backs the default `IReason::title()`.
[[nodiscard]] inline std::string demangle_type_name(const std::type_info& ti) {
    std::string name;
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    // __cxa_demangle returns a malloc'd buffer we must free; the manual
    // malloc/free is intrinsic to the Itanium ABI and confined to these lines.
    // NOLINTBEGIN(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
    char* demangled = abi::__cxa_demangle(ti.name(), nullptr, nullptr, &status);
    name = (status == 0 && demangled != nullptr) ? demangled : ti.name();
    std::free(demangled);  // free(nullptr) is a no-op
    // NOLINTEND(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
#else
    name = ti.name();
    // MSVC prefixes the readable name with "class "/"struct ".
    for (const std::string_view prefix : {"class ", "struct "}) {
        if (name.starts_with(prefix)) {
            name.erase(0, prefix.size());
            break;
        }
    }
#endif
    if (const auto lt = name.find('<'); lt != std::string::npos) {
        name.erase(lt);  // drop template args (and any "::" inside them)
    }
    if (const auto pos = name.rfind("::"); pos != std::string::npos) {
        name.erase(0, pos + 2);  // drop namespace / enclosing-scope qualification
    }
    return name;
}

/// `demangle_type_name(ti)` with the reason `code` appended in parens — e.g.
/// `GenericReason(123)`. The `title()` form the generic/unknown built-ins use,
/// which always carry a code.
[[nodiscard]] inline std::string titled_with_code(const std::type_info& ti, int code) {
    return std::format("{}({})", demangle_type_name(ti), code);
}
}  // namespace detail

/// Abstract "why" envelope. Hold one through `ReasonPtr`; read its `code`,
/// `message`, `created_at`, and optional `metadata`, query its `kind()`
/// discriminator, copy it with `clone()`, and read its description via `info()`.
class IReason {
public:
    // Public data members (rather than getters) match the codebase convention
    // for carried fields — e.g. ExternalOrigin::source — and let the factories
    // and JSON hooks populate a reason after default construction.
    int code = 0;         ///< Numeric reason code (`0` = unspecified).
    std::string message;  ///< Human-readable explanation of the "why".
    ReasonClock::time_point created_at = ReasonClock::now();  ///< When it was raised.
    Metadata metadata;  ///< Optional structured context (a `comms::md::Object`); empty by default.

    virtual ~IReason() = default;

    /// The stable discriminator for this reason's kind (e.g. `"generic"`).
    [[nodiscard]] virtual std::string_view kind() const noexcept = 0;

    /// A deep, independent copy.
    [[nodiscard]] virtual std::unique_ptr<IReason> clone() const = 0;

    /// Presentation metadata for this reason's kind, sourced from the concrete
    /// type's `static display_info()`.
    [[nodiscard]] virtual const DisplayInfo& info() const = 0;

    /// A human-readable title for `to_string` / `operator<<` / `std::format`.
    /// Defaults to the concrete type's name inferred at runtime (e.g.
    /// `RateLimitReason`) — distinct from the lower-case `kind()` discriminator.
    /// The generic/unknown built-ins override it to also carry their code
    /// (`GenericReason(123)`); override it yourself to customize.
    [[nodiscard]] virtual std::string title() const {
        return detail::demangle_type_name(typeid(*this));
    }

#if COMMONS_WITH_NLOHMANN_JSON
    /// Write this reason's fields into the JSON object `j`. The base writes
    /// `kind`/`code`/`message`/`created_at` (timestamp as epoch milliseconds),
    /// plus a `metadata` object when it is non-empty (omitted when empty, so the
    /// common case keeps its compact shape); a sub-reason overrides this to add
    /// its own fields — call `IReason::write_json(j)` first. Drives `to_json`
    /// through `ReasonPtr`. Gated on `COMMONS_WITH_NLOHMANN_JSON` (see the vtable
    /// caveat in the file header).
    virtual void write_json(nlohmann::json& j) const {
        j["kind"] = std::string{kind()};
        j["code"] = code;
        j["message"] = message;
        j["created_at"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(created_at.time_since_epoch())
                .count();
        if (!metadata.empty()) {
            j["metadata"] = metadata;  // comms::md::Object ⇄ json via ADL
        }
    }

    /// Read this reason's fields from the JSON object `j`. The base reads
    /// `code`/`message`/`created_at`/`metadata` (absent keys keep their defaults;
    /// `kind` is fixed by the concrete type). Override to read your own fields —
    /// call `IReason::read_json(j)` first. Drives `from_json` through `ReasonPtr`.
    virtual void read_json(const nlohmann::json& j) {
        if (const auto it = j.find("code"); it != j.end() && !it->is_null()) {
            it->get_to(code);
        }
        if (const auto it = j.find("message"); it != j.end() && !it->is_null()) {
            it->get_to(message);
        }
        if (const auto it = j.find("created_at"); it != j.end() && !it->is_null()) {
            created_at = ReasonClock::time_point{
                std::chrono::milliseconds{it->template get<std::int64_t>()}};
        }
        if (const auto it = j.find("metadata"); it != j.end() && !it->is_null()) {
            it->get_to(metadata);  // comms::md::Object ⇄ json via ADL
        }
    }
#endif

protected:
    // Protected to prevent slicing through the interface; derived types remain
    // copyable/movable via their own (implicitly defined) operations.
    IReason() = default;
    IReason(const IReason&) = default;
    IReason(IReason&&) = default;
    IReason& operator=(const IReason&) = default;
    IReason& operator=(IReason&&) = default;
};

/// An owning handle to a reason. The canonical way to carry an `IReason` by
/// value.
using ReasonPtr = std::unique_ptr<IReason>;

namespace detail {
/// The `info()` fallback for a reason kind that defines no `display_info()`.
/// (`HasMemberDisplayInfo` lives in `display_info.hpp`.)
[[nodiscard]] inline const DisplayInfo& empty_display_info() {
    static const DisplayInfo empty{};
    return empty;
}
}  // namespace detail

/// CRTP base wiring `kind()`, `clone()`, and `info()` from a compile-time kind
/// string and the concrete `Derived` type, over an `Interface` that is either
/// `IReason` (a plain reason) or `IFailureReason` (a failure reason). Use it as
/// `class GenericReason final : public ReasonKind<"generic", GenericReason> {…};`
/// or, for a failure kind, the `FailureReasonKind<"x_failure", X>` alias below.
///
/// `Derived` need only be copy-constructible (for `clone()`). It *may* expose a
/// `static const DisplayInfo& display_info()` to give `info()` real presentation
/// metadata; if it doesn't, `info()` returns an empty `DisplayInfo` (most reasons
/// don't need one). The common constructors — `()`, `(message)`, `(code)`,
/// `(code, message)` — are provided here, so a derived kind can simply inherit
/// them with `using ReasonKind::ReasonKind;` (which is what the
/// `COMMONS_DEFINE_REASON` / `COMMONS_DEFINE_FAILURE_REASON` macros do).
template <FixedString Kind, typename Derived, typename Interface = IReason>
class ReasonKind : public Interface {
public:
    // KIND keeps a SCREAMING_CASE spelling as the type's identity tag, matching
    // OriginKind::KIND; it is the recognized convention for the discriminator.
    // NOLINTNEXTLINE(readability-identifier-naming)
    static constexpr std::string_view KIND = Kind.view();  ///< Compile-time discriminator.

    ReasonKind() = default;
    explicit ReasonKind(std::string msg) {
        this->message = std::move(msg);
    }
    explicit ReasonKind(int reason_code) {
        this->code = reason_code;
    }
    ReasonKind(int reason_code, std::string msg) {
        this->code = reason_code;
        this->message = std::move(msg);
    }

    [[nodiscard]] std::string_view kind() const noexcept override {
        return KIND;
    }

    [[nodiscard]] std::unique_ptr<IReason> clone() const override {
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

// -- built-in plain reasons --------------------------------------------------

/// A run-of-the-mill reason with a caller-supplied code/message.
class GenericReason final : public ReasonKind<"generic", GenericReason> {
public:
    GenericReason() {
        message = "Generic reason";
    }
    explicit GenericReason(std::string msg) {
        message = std::move(msg);
    }
    explicit GenericReason(const int reason_code) : GenericReason() {
        code = reason_code;
    }
    GenericReason(const int reason_code, std::string msg) {
        code = reason_code;
        message = std::move(msg);
    }

    /// `GenericReason(<code>)` — the type name with the code appended.
    [[nodiscard]] std::string title() const override {
        return detail::titled_with_code(typeid(*this), code);
    }

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Generic",
            .description = "A generic reason.",
            .icon = Icon::from("mdi:information"),
        };
        return info;
    }
};

/// The reason is not known / was not specified.
class UnknownReason final : public ReasonKind<"unknown", UnknownReason> {
public:
    UnknownReason() {
        message = "Unknown reason";
    }
    explicit UnknownReason(std::string msg) {
        message = std::move(msg);
    }
    explicit UnknownReason(const int reason_code) : UnknownReason() {
        code = reason_code;
    }
    UnknownReason(const int reason_code, std::string msg) {
        code = reason_code;
        message = std::move(msg);
    }

    /// `UnknownReason(<code>)` — the type name with the code appended.
    [[nodiscard]] std::string title() const override {
        return detail::titled_with_code(typeid(*this), code);
    }

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Unknown",
            .description = "An unspecified reason.",
            .icon = Icon::from("mdi:help-circle"),
        };
        return info;
    }
};

// -- failure reasons ---------------------------------------------------------

/// A reason refinement for a **developer-controlled failure** — code that
/// deliberately submits a failure (not an exception). Adds
/// `throw_as_exception()` for sites that lack a failure channel of their own.
class IFailureReason : public IReason {
public:
    /// Package this failure reason into a `FailureReasonException` and throw it.
    /// Used where there is no `submit_failure`-style channel: throw here, let a
    /// handler upstack catch and resubmit. Clones the reason, so the thrown
    /// exception owns an independent copy. Defined out of line below, once
    /// `FailureReasonException` exists.
    [[noreturn]] void throw_as_exception() const;

protected:
    IFailureReason() = default;
    IFailureReason(const IFailureReason&) = default;
    IFailureReason(IFailureReason&&) = default;
    IFailureReason& operator=(const IFailureReason&) = default;
    IFailureReason& operator=(IFailureReason&&) = default;
};

/// An owning handle to a failure reason.
using FailureReasonPtr = std::unique_ptr<IFailureReason>;

/// CRTP base for a **failure** reason kind — `ReasonKind` over `IFailureReason`.
/// The clean spelling for a custom failure kind:
/// `class X final : public FailureReasonKind<"x", X> { … };`.
template <FixedString Kind, typename Derived>
using FailureReasonKind = ReasonKind<Kind, Derived, IFailureReason>;

/// A run-of-the-mill failure with a caller-supplied code/message.
class GenericFailureReason final
    : public FailureReasonKind<"generic_failure", GenericFailureReason> {
public:
    GenericFailureReason() {
        message = "Generic failure reason";
    }
    explicit GenericFailureReason(std::string msg) {
        message = std::move(msg);
    }
    explicit GenericFailureReason(const int reason_code) : GenericFailureReason() {
        code = reason_code;
    }
    GenericFailureReason(const int reason_code, std::string msg) {
        code = reason_code;
        message = std::move(msg);
    }

    /// `GenericFailureReason(<code>)` — the type name with the code appended.
    [[nodiscard]] std::string title() const override {
        return detail::titled_with_code(typeid(*this), code);
    }

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Generic Failure",
            .description = "A generic, developer-submitted failure.",
            .icon = Icon::from("mdi:alert-circle"),
        };
        return info;
    }
};

/// The failure is not known / was not specified.
class UnknownFailureReason final
    : public FailureReasonKind<"unknown_failure", UnknownFailureReason> {
public:
    UnknownFailureReason() {
        message = "Unknown failure reason";
    }
    explicit UnknownFailureReason(std::string msg) {
        message = std::move(msg);
    }
    explicit UnknownFailureReason(const int reason_code) : UnknownFailureReason() {
        code = reason_code;
    }
    UnknownFailureReason(const int reason_code, std::string msg) {
        code = reason_code;
        message = std::move(msg);
    }

    /// `UnknownFailureReason(<code>)` — the type name with the code appended.
    [[nodiscard]] std::string title() const override {
        return detail::titled_with_code(typeid(*this), code);
    }

    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Unknown Failure",
            .description = "An unspecified, developer-submitted failure.",
            .icon = Icon::from("mdi:help-circle"),
        };
        return info;
    }
};

// -- factories ---------------------------------------------------------------

/// Construct a reason on the heap. Defaults to `GenericReason`; pass another
/// kind as the explicit template argument: `make_reason<UnknownReason>(404)`.
template <typename R = GenericReason, typename... Args>
    requires std::derived_from<R, IReason>
[[nodiscard]] ReasonPtr make_reason(Args&&... args) {
    return std::make_unique<R>(std::forward<Args>(args)...);
}

/// Construct a failure reason on the heap. Defaults to `GenericFailureReason`;
/// pass another kind explicitly: `make_failure_reason<UnknownFailureReason>()`.
template <typename R = GenericFailureReason, typename... Args>
    requires std::derived_from<R, IFailureReason>
[[nodiscard]] FailureReasonPtr make_failure_reason(Args&&... args) {
    return std::make_unique<R>(std::forward<Args>(args)...);
}

// -- registry ----------------------------------------------------------------

/// A program-wide registry mapping a reason `kind` string to a factory. A
/// Meyers singleton, like `GlobalOriginRegistry`, so the map is constructed
/// before any `inline` registrar runs. Used by the JSON `from_json` path to turn
/// a `kind` discriminator back into the right concrete reason.
class GlobalReasonRegistry {
public:
    /// Produces a fresh, default-constructed reason of one kind.
    using Factory = ReasonPtr (*)();

private:
    std::map<std::string, Factory, std::less<>> factories_;
    GlobalReasonRegistry() = default;

public:
    GlobalReasonRegistry(const GlobalReasonRegistry&) = delete;
    GlobalReasonRegistry& operator=(const GlobalReasonRegistry&) = delete;
    GlobalReasonRegistry(GlobalReasonRegistry&&) = delete;
    GlobalReasonRegistry& operator=(GlobalReasonRegistry&&) = delete;
    ~GlobalReasonRegistry() = default;

    [[nodiscard]] static GlobalReasonRegistry& instance() noexcept {
        static GlobalReasonRegistry r;
        return r;
    }

    /// Register `factory` under `kind`. Returns `false` if the kind was already
    /// registered (the first registration wins).
    bool register_kind(const std::string_view kind, Factory factory) {
        return factories_.emplace(std::string{kind}, factory).second;
    }

    /// Create a fresh reason for `kind`, or `nullptr` if the kind is unknown.
    [[nodiscard]] ReasonPtr create(const std::string_view kind) const {
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

    /// The names of the registered kinds whose reason is-a `Base` — the generic
    /// filter. `kinds_of<IReason>()` is every kind; `kinds_of<IFailureReason>()`
    /// is the failure kinds; pass your own `IReason` sub-interface (the way
    /// `IFailureReason` refines `IReason`) to select that family. Classifies by
    /// instantiating one of each kind and `dynamic_cast`-ing, so call it for
    /// setup/inspection rather than on a hot path.
    template <typename Base = IReason>
        requires std::derived_from<Base, IReason>
    [[nodiscard]] std::vector<std::string> kinds_of() const {
        std::vector<std::string> out;
        for (const auto& [name, factory] : factories_) {
            const ReasonPtr probe = factory();
            if (dynamic_cast<const Base*>(probe.get()) != nullptr) {
                out.push_back(name);
            }
        }
        return out;
    }

    /// The names of the registered **failure** kinds (`IFailureReason`s).
    [[nodiscard]] std::vector<std::string> failure_kinds() const {
        return kinds_of<IFailureReason>();
    }

    /// The names of the registered **non-failure** kinds (plain `IReason`s that
    /// are not `IFailureReason`s).
    [[nodiscard]] std::vector<std::string> non_failure_kinds() const {
        std::vector<std::string> out;
        for (const auto& [name, factory] : factories_) {
            const ReasonPtr probe = factory();
            if (dynamic_cast<const IFailureReason*>(probe.get()) == nullptr) {
                out.push_back(name);
            }
        }
        return out;
    }
};

/// A self-registering object: constructing one registers `T`'s factory into the
/// `GlobalReasonRegistry`. `COMMONS_REGISTER_REASON` emits one as an `inline`
/// object so registration happens at static init. `T` must be
/// default-constructible and expose a `static constexpr std::string_view KIND`.
template <typename T>
struct ReasonRegistrar {
    ReasonRegistrar() noexcept {
        GlobalReasonRegistry::instance().register_kind(
            T::KIND, []() -> ReasonPtr { return std::make_unique<T>(); });
    }
};

}  // namespace comms

/// Register an already-defined reason type `Ident` into the
/// `GlobalReasonRegistry`. Place it at namespace scope after the type.
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_REGISTER_REASON(Ident)                                                             \
    inline const ::comms::ReasonRegistrar<Ident> commons_reason_registrar_##Ident {}

/// Define **and** register a plain reason kind in one line. `Ident` is the C++
/// type name, `Kind` the discriminator string literal. The generated type
/// inherits the `ReasonKind` constructors — `()`, `(message)`, `(code)`,
/// `(code, message)` — so `comms::make_reason<Ident>(404, "nope")` just works.
/// For a baked-in default code/message or a `display_info()`, write the class by
/// hand against `comms::ReasonKind<Kind, Ident>` instead.
///
///     COMMONS_DEFINE_REASON(NotFoundReason, "not_found");
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_DEFINE_REASON(Ident, Kind)                                                         \
    class Ident final : public ::comms::ReasonKind<Kind, Ident> {                                  \
    public:                                                                                        \
        using ::comms::ReasonKind<Kind, Ident>::ReasonKind;                                        \
    };                                                                                             \
    COMMONS_REGISTER_REASON(Ident)

/// Define and register a **failure** reason kind in one line — the
/// `IFailureReason` counterpart of `COMMONS_DEFINE_REASON`. The generated type
/// is a `comms::IFailureReason`, so it has `throw_as_exception()` and works with
/// `comms::make_failure_reason<Ident>(…)`.
///
///     COMMONS_DEFINE_FAILURE_REASON(RateLimitReason, "rate_limit");
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define COMMONS_DEFINE_FAILURE_REASON(Ident, Kind)                                                 \
    class Ident final : public ::comms::FailureReasonKind<Kind, Ident> {                           \
    public:                                                                                        \
        using ::comms::FailureReasonKind<Kind, Ident>::ReasonKind;                                 \
    };                                                                                             \
    COMMONS_REGISTER_REASON(Ident)

namespace comms {

// The built-in kinds self-register so the JSON `from_json` dispatch resolves them.
COMMONS_REGISTER_REASON(GenericReason);
COMMONS_REGISTER_REASON(UnknownReason);
COMMONS_REGISTER_REASON(GenericFailureReason);
COMMONS_REGISTER_REASON(UnknownFailureReason);

// -- ordering ----------------------------------------------------------------

/// Order two reasons by `created_at` (earliest first) — the C++ analog of the
/// Java `Reason implements Comparable<Reason>` by creation time. Binds any
/// concrete reason through the `IReason` base.
[[nodiscard]] inline std::strong_ordering operator<=>(const IReason& a, const IReason& b) noexcept {
    return a.created_at <=> b.created_at;
}

// -- exceptions --------------------------------------------------------------

/// Carries an `IReason` as a thrown exception. Its `what()` is the reason's
/// `message`. Base of the reject/cancel/failure exceptions.
class ReasonException : public Exception {
public:
    /// Take shared ownership of an already-heap-allocated reason.
    explicit ReasonException(std::shared_ptr<const IReason> reason)
        : Exception(reason ? reason->message : std::string{"reason"}), reason_(std::move(reason)) {}

    /// Clone `reason` into the exception (independent copy).
    explicit ReasonException(const IReason& reason)
        : ReasonException(std::shared_ptr<const IReason>{reason.clone()}) {}

    /// The carried reason (may be null if constructed from a null pointer).
    [[nodiscard]] const std::shared_ptr<const IReason>& reason() const noexcept {
        return reason_;
    }

private:
    std::shared_ptr<const IReason> reason_;
};

/// Carries a developer-controlled `IFailureReason`. Thrown by
/// `IFailureReason::throw_as_exception()` for sites with no failure channel.
class FailureReasonException : public ReasonException {
public:
    /// Take shared ownership of an already-heap-allocated failure reason.
    explicit FailureReasonException(std::shared_ptr<const IFailureReason> reason)
        : ReasonException(reason),  // upcasts to shared_ptr<const IReason>
          failure_(std::move(reason)) {}

    /// Clone `reason` into the exception (independent copy).
    explicit FailureReasonException(const IFailureReason& reason)
        : FailureReasonException(clone_failure(reason)) {}

    /// The carried failure reason (the same object as `reason()`, typed).
    [[nodiscard]] const std::shared_ptr<const IFailureReason>& failure_reason() const noexcept {
        return failure_;
    }

private:
    // clone() returns a unique_ptr<IReason> whose dynamic type is the IFailureReason
    // subclass; recover the typed pointer so the exception keeps it as such.
    [[nodiscard]] static std::shared_ptr<const IFailureReason>
    clone_failure(const IFailureReason& reason) {
        // The clone's dynamic type is `reason`'s — an IFailureReason subclass —
        // so this downcast is provably safe; static_cast avoids the RTTI check.
        IReason* raw = reason.clone().release();
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        return std::shared_ptr<const IFailureReason>{static_cast<const IFailureReason*>(raw)};
    }

    std::shared_ptr<const IFailureReason> failure_;
};

/// A `ReasonException` for a rejected operation. Inherits `ReasonException`'s
/// constructors (from a shared pointer or by cloning a reason).
class RejectException : public ReasonException {
public:
    using ReasonException::ReasonException;
};

/// A `ReasonException` for a cancelled operation. Inherits `ReasonException`'s
/// constructors (from a shared pointer or by cloning a reason).
class CancelException : public ReasonException {
public:
    using ReasonException::ReasonException;
};

[[noreturn]] inline void IFailureReason::throw_as_exception() const {
    // Clone self and hand the typed copy to the exception. `*this` is an
    // IFailureReason, so the clone's dynamic type is too — this downcast is
    // provably safe; static_cast avoids the RTTI check.
    IReason* raw = clone().release();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    std::shared_ptr<const IFailureReason> failure{static_cast<const IFailureReason*>(raw)};
    throw FailureReasonException(std::move(failure));
}

// ---------------------------------------------------------------------------
// Text output: to_string + std::ostream insertion. (std::format support is the
// std::formatter specialization below, outside namespace comms.) Both accept any
// reason by upcast to the IReason base.
// ---------------------------------------------------------------------------

/// A reason as `title: message`. The `title()` carries any code for the
/// generic/unknown built-ins (`GenericReason(123): message`); a custom kind shows
/// just its type name (`RateLimitReason: message`) unless it overrides `title()`.
[[nodiscard]] inline std::string to_string(const IReason& r) {
    return std::format("{}: {}", r.title(), r.message);
}

inline std::ostream& operator<<(std::ostream& os, const IReason& r) {
    return os << to_string(r);
}

}  // namespace comms

// ---------------------------------------------------------------------------
// std::format support. A partial specialization constrained to IReason-derived
// types so any concrete reason (or an IReason reference) formats, mirroring the
// IOrigin formatter in origin.hpp.
// ---------------------------------------------------------------------------

// This spec-less formatter reads no member state, but `std::formatter` requires
// `parse`/`format` to be non-static members — so silence the convert-to-static
// suggestion here.
// NOLINTBEGIN(readability-convert-member-functions-to-static)

/// Formats any `comms::IReason` (and derived) as `title(code): message`. No spec.
template <typename T>
    requires std::derived_from<T, comms::IReason>
struct std::formatter<T> {
    constexpr auto parse(const std::format_parse_context& ctx) {
        const auto* it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("commons: IReason takes no format spec");
        }
        return it;
    }

    auto format(const comms::IReason& r, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "{}", comms::to_string(r));
    }
};  // namespace std

// NOLINTEND(readability-convert-member-functions-to-static)
