# CLAUDE.md — commons

## What this is

`commons` is a header-only **C++23** library of common/shared types reused
across the sibling C++ libraries (`conduit`, `parcel`, `dimval`, …). It is the
shared home for small building blocks that were otherwise hand-rolled per repo.
Namespace is `comms`; the CMake/Meson identity and include dir are `commons`
(`commons::commons`, `<commons/...>`).

The base library has **no forced dependencies**. Optional integrations are
gated and auto-detected.

## Layout

```
include/commons/      public headers; umbrella is commons/commons.hpp
cmake/                CompilerWarnings, Sanitizers, Coverage, Dependencies, commonsConfig.cmake.in
tests/                GoogleTest; integration tests appended conditionally
examples/             commons_* demos; examples/consumers/fetch_content downstream demo
docs/Doxyfile.in      Doxygen template
CMakeLists.txt, CMakePresets.json, Makefile, meson.build, meson.options, subprojects/
```

Seed types: `comms::FixedString<N>` (`fixed_string.hpp`) and the fixed-width
numeric aliases `i8`…`u64` / `f32` / `f64` / `usize` / `isize` / `i128` / `u128`,
plus complex aliases `cs8`…`cs64` / `cu8`…`cu64` / `cf32` / `cf64` (`types.hpp`).
The complex aliases serialize to JSON as a `[real, imaginary]` array (via an
`nlohmann::adl_serializer<std::complex<T>>` specialization, since ADL can't reach
`std::complex`).

Richer types: `comms::Color` (`color.hpp`, RGBA + HSL/HSV + CSS/MUI palettes)
and `comms::Icon` (`icon.hpp`, an Iconify `set:name` identifier such as
`mdi:abacus`). `Icon`'s predefined catalogs are opt-in: `comms::Icons::mdi::abacus`
becomes available via `#include <commons/icons.hpp>` (not pulled by the umbrella).
The 7,447-entry MDI table in `commons/icons/mdi.hpp` is **generated** by
`scripts/generate_mdi_icons.py` from `data/iconify-mdi.json` — regenerate with
`python3 scripts/generate_mdi_icons.py data/iconify-mdi.json > include/commons/icons/mdi.hpp`
(it runs `clang-format`, so output is idempotent).

`comms::SemVer` (`semver.hpp`) is a Semantic Versioning 2.0.0 value
(`major.minor.patch` + optional prerelease/build, full §11 prerelease ordering,
build metadata parsed but ignored in comparison, lenient partial parsing) and
`comms::VersionConstraint` (`version_constraint.hpp`) is an npm-style semver range
(`^`/`~`, comparisons, space-separated intersection) answering
`satisfies(SemVer)`. Both hold `std::string` members, so — unlike `Color`/`Icon` —
they are runtime (non-`constexpr`) types with no UDL literal; `SemVer::parse` is
non-throwing while `VersionConstraint::parse` throws `std::invalid_argument` on a
malformed sub-version.

The `comms::literals` user-defined literals `"#6366f1"_color` and
`"mdi:home"_icon` live in `literals.hpp`; both are `consteval`, so a
malformed literal is a compile error. The umbrella includes `literals.hpp`.

Behavioral types: `comms::DisplayInfo` (`display_info.hpp`, optional presentation
metadata — name/description/icon/color — that any type opts into via a
`display_info()` member, becoming `Displayable`) and the `comms::Flag` family
(`flag.hpp`, compile-time named flags grouped into categories, a runtime
`FlagSet`, a program-wide `GlobalFlagRegistry`, plus CRTP/holder mixins —
`IHasFlags`/`HasFlags`/`FlagBuilderMixin`/`FlagBuilderGetters` — for types that
own a flag set).

`comms::Id<Tag, Repr>` (`id.hpp`) is a strong-typed identifier wrapping an
allowed `Repr` (the unsigned fixed-width ints `u8`/`u16`/`u32`/`u64`,
`std::string`, and — gated by `COMMONS_WITH_ULID` — `ulid::Ulid`) under a
phantom `Tag` so ids of different kinds cannot be mixed. Aliases `Uint8Id<Tag>`
… `Uint64Id<Tag>`, `StringId<Tag>`, and `UlidId<Tag>` save typing; the
`COMMONS_DEFINE_UINT{8,16,32,64}_ID`, `COMMONS_DEFINE_STRING_ID`, and
`COMMONS_DEFINE_ULID_ID` macros emit both a `Name##Tag` (with a
`static constexpr std::string_view name`) and a `using Name = …Id<Name##Tag>`
alias in one shot. `to_string` delegates to the underlying repr;
`display_string` prefixes it with `Tag::name` for named tags; the
`std::formatter<Id>` specialization inherits from `std::formatter<Repr>` so any
spec the underlying type accepts (e.g. `"{:#x}"`) works transparently.

`comms::IOrigin` (`origin.hpp`) is a polymorphic provenance envelope: an abstract
base whose `kind()` discriminator is supplied as a compile-time `FixedString`
template parameter via the `OriginKind<"kind", Derived>` CRTP base (the
`IconifySet<FixedString Set>` pattern), which also wires `clone()` and a
`DisplayInfo`-backed `info()`. Built-in kinds are `CoreOrigin`/`InternalOrigin`/
`ExternalOrigin`/`UnknownOrigin`; new kinds self-register into the program-wide
`GlobalOriginRegistry` via `COMMONS_REGISTER_ORIGIN(Type)` (mirroring
`GlobalFlagRegistry`/`COMMONS_REGISTER_FLAG`). `OriginPtr` is
`std::unique_ptr<IOrigin>`. Per the no-forced-dependency rule, `origin.hpp` holds
no JSON: `json.hpp` round-trips the built-in kinds (and resolves the `kind`
discriminator through the registry, via an `adl_serializer<OriginPtr>`); a custom
kind ships its own `to_json`/`from_json`.

`comms::Prioritized` (`prioritized.hpp`) attaches **priorities** to orderable
things (adapters, transports, …) and sorts them deterministically — the C++ analog
of Spring's `Ordered` (lower value = higher precedence,
`HIGHEST_PRECEDENCE = INT_MIN`). It ships the virtual-with-default `Prioritized`
interface and `Prioritizable<T>` concept; `get_priority(x)` (uniform, null-safe
lookup over values, raw and smart pointers); the `PrioritizedCompare` /
`LenientPrioritizedCompare<T>` comparators over `std::shared_ptr`;
`PrioritizedSet<T>` (a transparent `std::set<T>` that iterates in
`(priority asc, insertion-order asc)` order, snapshotting priority at insert);
the `PrioritizedBuilder<Derived>` CRTP mixin; and `WithPriority<T>` +
`with_priority` / `make_prioritized` (inherits `T` for non-final classes, composes
otherwise). `WithPriority<T>` and `PrioritizedSet<T>` get JSON hooks gated on `T`
itself being json-serializable. The three sentinel *values* are `static constexpr`
members overridable at build time — see the override-seam note below.

`comms::Exception` (`exception.hpp`) is the **root of the Commons exception
hierarchy** — a thin `std::runtime_error` subclass so every Commons-thrown
exception is catchable as one type. Feature-specific exceptions derive from it
(or a more specific Commons exception); they live next to the feature that
throws them.

`comms::IReason` (`reason.hpp`) is a polymorphic **"why" envelope** for an
outcome that is *not* an exception (a reject, a cancel, a refusal): it carries a
numeric `code`, a `message`, a `created_at` timestamp, and an optional
`metadata` bag (a `comms::Metadata` / `comms::md::Object`, empty by default, for
arbitrary structured context), and — exactly like
`IOrigin` — is an **open set** keyed by a `kind()` discriminator supplied via the
`ReasonKind<"kind", Derived>` CRTP base (which wires `clone()` and an *optional*
`DisplayInfo`-backed `info()` — a kind with no `display_info()` gets an empty
one). `comms::IFailureReason` refines it for a **developer-controlled failure**
(code deliberately submitting a failure, not an exception), adding
`throw_as_exception()`. Built-in kinds: `GenericReason`/`UnknownReason` and the
failure counterparts `GenericFailureReason`/`UnknownFailureReason`; `ReasonPtr` /
`FailureReasonPtr` are the owning handles and `make_reason<R>` /
`make_failure_reason<R>` the factories (defaulting to the generic kinds). A free
`operator<=>` orders reasons by `created_at`. Text output (`to_string`/`<<`/
`std::format`) emits `title: message`, where the virtual `title()` defaults to the
concrete type name (demangled, e.g. `RateLimitReason`) — the generic/unknown
built-ins override it to append their code (`GenericReason(123)`). The registry
filters by family: `kinds()` (all), `failure_kinds()` / `non_failure_kinds()`,
and the generic `kinds_of<Base>()` for a custom `IReason` sub-interface (the way
`IFailureReason` refines `IReason`). The one-line
`COMMONS_DEFINE_REASON(Ident,"kind")` / `COMMONS_DEFINE_FAILURE_REASON(...)`
macros define **and** register a kind (inheriting the `ReasonKind` constructors
`()`/`(message)`/`(code)`/`(code,message)`); hand-write the class only for a
baked-in default or a `display_info()`. New kinds self-register into the
program-wide `GlobalReasonRegistry` via `COMMONS_REGISTER_REASON(Type)` (mirroring
`GlobalOriginRegistry`). The reason exceptions sit under `comms::Exception`:
`ReasonException` (carries any `IReason`) → `FailureReasonException` (carries an
`IFailureReason`, thrown by `throw_as_exception()`), with `RejectException` /
`CancelException` as sibling `ReasonException`s. A reason round-trips as
`{"kind","code","message","created_at"}` (timestamp as epoch milliseconds), plus
a `"metadata"` object when that bag is non-empty (omitted when empty).
**Reason is one of the exceptions to the "JSON lives only in `json.hpp`" rule**
(`IAbility`/`IIdentity` are the others — see below): the per-field
(de)serializers are the **virtual** `IReason::write_json` / `read_json` hooks *in
`reason.hpp`*, gated by `COMMONS_WITH_NLOHMANN_JSON` (so nlohmann is still pulled
only when present) — a sub-reason adds its own fields by overriding them (calling
the base first) and they round-trip through `ReasonPtr` automatically. `json.hpp`
only carries the `adl_serializer<ReasonPtr>` / `adl_serializer<FailureReasonPtr>`
that resolve `kind` through the registry and drive those hooks. Because the gate
adds virtual members it changes `IReason`'s vtable, so **every TU in a build must
resolve `COMMONS_WITH_NLOHMANN_JSON` identically** (force it with a `-D` if the
build is mixed).

The `comms::md` namespace (`metadata.hpp`) is a JSON-like **dynamic value tree**.
`comms::md::Value` is a discriminated union of null/bool/`i64`/`u64`/`float`/
`double`/`string`/`Array`/`Object` (the `Object` alternative held through a
`unique_ptr` so `Value` can recurse); `Array` is `vector<Value>` and `Object` a
transparent-lookup `unordered_map<string, Value>` with JSON-flavored helpers.
The whole library lives in **one self-contained header** (`Value`'s
variant-touching members are declared in-class and defined out-of-line after
`Object` is complete — preserve that ordering). It ships dotted/bracketed path
lookup (`find_path`/`require_path`/`contains_path`, e.g. `"a.b[0].c"`), deep
`merge` (objects recurse, scalars overwrite, arrays replace), order-independent
`std::hash` for `Object`, compact-JSON `operator<<` and `std::formatter` (both
hand-rolled via `to_chars`, no nlohmann), and free-function helpers
(`contains`/`find_ptr`/`require*`/`get_*_if`/`merge`). The headline document-root
case is surfaced at the Commons **root** as `comms::Metadata`
(`using Metadata = md::Object;`); everything else stays under `comms::md::…`. Its
exception trio roots at `comms::Exception`: `MetadataError` →
`MissingKeyError` / `TypeError`. Per the no-forced-dependency rule the header
carries no nlohmann; the `Value`/`Object`/`Array` JSON round-trip lives in
`commons/json/metadata.hpp` (pulled by the `commons/json.hpp` umbrella) under
`COMMONS_WITH_NLOHMANN_JSON`.

The `comms::IAbility` (`ability.hpp`) and `comms::IIdentity` (`identity.hpp`)
families are a shared **authn/authz** vocabulary — *what* a principal may do and
*who* it is — built as **polymorphic open sets** exactly like `IOrigin`/
`IReason`: a `kind()` discriminator supplied by an `AbilityKind<"kind", Derived>`
/ `IdentityKind<"kind", Derived>` CRTP base (wiring `clone()` and an optional
`DisplayInfo`-backed `info()`), self-registration into the program-wide
`GlobalAbilityRegistry` / `GlobalIdentityRegistry` (`COMMONS_REGISTER_ABILITY` /
`_IDENTITY`, with the `kinds()`/`kinds_of<Base>()`/`contains` API), and the
one-line `COMMONS_DEFINE_ABILITY(Ident,"kind")` / `COMMONS_DEFINE_IDENTITY(...)`
macros (inheriting the `()`/`(value)` constructors). `AbilityPtr`/`IdentityPtr`
are the owning handles, `make_ability<A>` / `make_identity<I>` the factories
(`make_identity` defaults to `NoIdentity`). The authorization check is
**`required.allowed(subject)`** — the *required* ability is the receiver and the
candidate (another ability, or a whole identity) is the argument; the default
`IAbility::allowed(const IAbility&)` is "same kind + same `value`", and a kind
owns its own rule by overriding it. An identity carries `abilities`, and
`identity.satisfies(required)` ≡ `required.allowed(identity)` (the
`IAbility::allowed(const IIdentity&)` overload is declared in `ability.hpp` but
**defined out-of-line in `identity.hpp`** to break the cycle, which also
forward-declares `IIdentity`). `IIdentity`'s protected copy ctor deep-clones the
`abilities` so the CRTP `clone()` works; both bases use a virtual `equals()`
(and the null-safe free `ability_equal`/`identity_equal`) because `…Ptr` cannot
use a defaulted `==`. Built-in abilities: `RoleAbility`, the extensible
`RecordPermissionAbility` (extra `action`/`resource`, `"*"` wildcard — the worked
example overriding `allowed`/`equals`/JSON), `GenericAbility`, `UnknownAbility`.
Built-in identities: `UserIdentity`/`ServerIdentity`/`ApiClientIdentity`/
`UnknownIdentity` (default `satisfies`), `RootIdentity` (`satisfies`→true),
`NoIdentity` (`satisfies`→false). **Identity/Ability join `reason.hpp` as the
headers that keep their per-field JSON in gated virtual `write_json`/`read_json`
hooks** (so a sub-kind extends the JSON by overriding); `json/ability.hpp` /
`json/identity.hpp` hold only the `adl_serializer<…Ptr>` glue (resolving `kind`
through the registry, unknown kind throws). Same vtable/uniform-gate caveat as
`reason.hpp`: every TU must resolve `COMMONS_WITH_NLOHMANN_JSON` identically. The
shared `detail::demangle_type_name` (backing the default `title()`) now lives in
`commons/detail/type_name.hpp`, and `detail::empty_display_info` in
`display_info.hpp`, so `reason.hpp`/`ability.hpp`/`identity.hpp` reuse one
definition.

The `comms::AuditRecord` family (`audit_record.hpp`) is a shared **audit trail**
facility — *who* did *what*, *when*, *from where*. `comms::AuditRecord` is the
base value type: an `identity` (a `comms::IdentityPtr` defaulting to
`comms::NoIdentity` — never null; `set_identity(make_identity<UserIdentity>(...))`),
a `timestamp` (a real `AuditClock` `time_point` defaulting to `now()`, mirroring
`reason.hpp`'s `created_at`), optional `ip`/`user_agent`/`session_id`, a
`related_ids` `map<string,string>` (name → id string), and a `comms::Metadata`
bag (empty by default). Ids are stored as **strings** because `Id<Tag,Repr>` has
no type-erased form — the templated `set_session_id(id)` / `add_related_id(name,
id)` helpers capture `comms::to_string(id)` so a single record can reference ids
of different kinds. Because `IdentityPtr` is non-copyable/non-comparable,
`AuditRecord` is **not** an aggregate: its copy ctor/copy-assign deep-clone the
identity (move defaulted) and its `operator==` is hand-written (identity compared
with `identity_equal`); it is `operator==`-only (no `<=>`: `Metadata` is
equality-only).
`comms::ChangeAuditRecord<T>` publicly inherits `AuditRecord` and adds
`before`/`after` as `std::optional<T>` (absent on create/delete). `comms::AuditLog<Record>`
is a capped, insertion-ordered collection (`push()` appends, drops the oldest
front records FIFO once over capacity; capacity `0` keeps nothing), with the
runtime cap defaulting to the `COMMONS_AUDIT_RECORDS_CAPACITY` value-override
seam (default 3, see below); `AuditRecords` and `ChangeAuditRecords<T>` are the
aliases. **All three types plus the `<chrono>` millisecond timestamp encoding
live in the single `audit_record.hpp`, and all their JSON hooks in the single
`commons/json/audit_record.hpp`** (free ADL `to_json`/`from_json`, the
container/templated forms instantiated only for a serializable `Record`/`T`): an
`AuditRecord` always emits `identity`+`timestamp` (epoch millis) with optional
fields omitted when absent/empty, a `ChangeAuditRecord<T>` adds `before`/`after`,
and an `AuditLog` is a JSON array (capacity is **not** serialized — a log read
under a smaller cap keeps the newest N). Pulling in `<commons/id.hpp>` for the
helpers does not force ulid (still gated by `COMMONS_WITH_ULID`).

`comms::LockFreeQueue<T, Mode>` (`lock_free_queue.hpp`) is the **first
`std::atomic`-based type** — a lock-free MPMC FIFO built as a Michael–Scott
linked-list queue with a node freelist and ABA-safe tagged links. `T` must
satisfy the `comms::LockFreeValue` concept (trivially copyable/destructible,
nothrow copy/move constructible — the element restriction a lock-free node pool
needs, made explicit). `Mode` (a `LockFreeQueueMode`) selects the
strategy, factored into a `detail` storage policy that the shared push/pop logic
sits on top of: `Fixed` is fully portable (nodes in one preallocated array,
links a `std::atomic<comms::u64>` packing a 32-bit slot index + 32-bit tag, no
DWCAS — `push` returns `false` when full) and `Dynamic` (the default) is
unbounded (heap nodes recycled through a lock-free freelist,
links a 16-byte `std::atomic<TaggedPtr>` updated by 128-bit DWCAS — lock-free
natively on arm64, but on x86-64 only with `-mcx16`, else a correct hidden-lock
fallback; `is_lock_free()` reports the truth at runtime). The element is carried
through a `std::atomic<T>` accessed `relaxed` (ordering rides the link CAS) so
the consumer's read-before-validating-CAS is well-defined rather than a benign
data race — which is what keeps it clean under ThreadSanitizer. The guarantee: a
value successfully `push`ed *happens-before* the `pop` that returns it; `empty()`
is a racy best-effort snapshot and there is deliberately no exact `size()`. The
API is `push(const T&)` / `push(T&&)` / `emplace(args...)` (all `-> bool`,
`false` = full/alloc-failed) and `pop(T&)` / `try_pop() -> std::optional<T>`
(`pop` and `try_pop` both return the value, never a reference — a popped node may
be recycled immediately). It is **not** a `std::queue` (no `front`/`back`/`size`,
those can't be made race-safe here) and exposes no iterators, so it is not a
range. The queue owns atomics + a node pool, so it is non-copyable and
non-movable (mirrors
`StatusTransitionTimeline` in `lifecycle.hpp`). Its fixed-mode default capacity
is the `COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY` value-override seam (default
1024). It ships **no JSON** — a deliberate exception (see below).

## Feature gates (live in `commons/config.hpp`)

Each optional integration is a `COMMONS_WITH_*` macro resolving to `1`/`0`:

- A **predefined** macro (from CMake `-DCOMMONS_WITH_NLOHMANN_JSON=ON` /
  `-DCOMMONS_WITH_ULID=ON`, Meson `-Djson=true` / `-Dulid=true`, or the
  consumer) always wins — forces the integration on or off.
- Otherwise **autodetect** via `__has_include(<nlohmann/json.hpp>)` /
  `__has_include(<ulid/ulid.h>)`.

The currently supported gates are `COMMONS_WITH_NLOHMANN_JSON` (turns on the
JSON `to_json`/`from_json` hooks) and `COMMONS_WITH_ULID` (turns on
`comms::Id<Tag, ulid::Ulid>` plus the `COMMONS_DEFINE_ULID_ID` macro).

`COMMONS_HAS_INT128` (in `types.hpp`) signals 128-bit integer availability
(`__SIZEOF_INT128__`).

In CMake/Meson the integration options default **OFF**: autodetect covers the
common case, and turning an option ON is what additionally *fetches* the
dependency (and hard-defines the macro).

**Value-override seam (distinct from the boolean gates):** the
`COMMONS_PRIORITIZED_HIGHEST_PRECEDENCE` / `_LOWEST_PRECEDENCE` /
`_DEFAULT_PRIORITY` macros override `comms::Prioritized`'s sentinel *values* and
live in `prioritized.hpp` (not `config.hpp`, so the umbrella does not force
`<limits>` on consumers that only want the gates). The C++ header default
(`numeric_limits`-based) is canonical — it has no CMake/Meson literal — so the
build only emits a `-D` when a concrete override is supplied: CMake via a cache
`STRING` (`-DCOMMONS_PRIORITIZED_DEFAULT_PRIORITY=5`), Meson via a string option
(`-Dprioritized_default_priority=5`). Downstream consumers can predefine the macro
directly. The `COMMONS_AUDIT_RECORDS_CAPACITY` macro (in `audit_record.hpp`,
overriding `comms::AuditLog`'s default capacity — default 3) is wired the same
way (CMake `-DCOMMONS_AUDIT_RECORDS_CAPACITY=5`, Meson
`-Daudit_records_capacity=5`), but its C++ default *is* a concrete literal. The
`COMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY` macro (in `lock_free_queue.hpp`,
overriding `comms::LockFreeQueue`'s fixed-mode default capacity — default 1024)
is wired identically (CMake `-DCOMMONS_LOCK_FREE_QUEUE_DEFAULT_CAPACITY=2048`,
Meson `-Dlock_free_queue_default_capacity=2048`).

## The rule for every public type

**Every public Commons type must ship its serialization hooks under the gates.**
When adding a type, also add, guarded by the matching macro:

- **nlohmann `to_json` / `from_json`** under `COMMONS_WITH_NLOHMANN_JSON`.
  `commons/json.hpp` is a thin **umbrella** over per-type modules in
  `commons/json/<name>.hpp`: a new type adds its hooks in its own
  `commons/json/<name>.hpp` (each `#pragma once`, self-gating behind
  `COMMONS_WITH_NLOHMANN_JSON`, including `<nlohmann/json.hpp>` + the commons
  type header it serializes + any dependency sub-headers so it compiles
  standalone) and is then `#include`d from the umbrella. Class types: free ADL
  functions in namespace `comms`. Fundamental/`std` types (e.g. the 128-bit
  aliases, `std::complex`, `std::optional`): an `nlohmann::adl_serializer<T>`
  specialization — ADL cannot find free functions for builtins. *Exceptions:*
  `reason.hpp`, `ability.hpp`, and `identity.hpp` keep their per-field
  (de)serialization in **gated virtual hooks on the base** (`write_json` /
  `read_json`) so a polymorphic, open-set type's subkinds can extend the JSON by
  overriding; the matching `commons/json/<name>.hpp` then only holds the
  `adl_serializer<…Ptr>` glue. Do this only for a polymorphic open set that
  genuinely needs subclass-extensible JSON, and document the vtable/uniform-gate
  caveat. A second, narrower *exception* is a type for which JSON is
  **deliberately omitted**: `lock_free_queue.hpp` ships none, because a
  concurrent queue is mutable shared state — a snapshot is racy and a round-trip
  meaningless — so there is no `commons/json/lock_free_queue.hpp` and the
  umbrella is untouched. This follows the precedent that `lifecycle.hpp` keeps
  JSON on its value records, not on the synchronizing object; document the
  omission in the type's `/// @brief`.

Then: register the type's tests in `tests/CMakeLists.txt` + `tests/meson.build`
(append the integration test under the `COMMONS_WITH_*` / `commons_with_*`
guard), and include it from the umbrella `commons/commons.hpp`.

## Build / test

```sh
make test           # base library: configure + build + ctest (no forced integrations)
make integrations   # COMMONS_WITH_NLOHMANN_JSON=ON + COMMONS_WITH_ULID=ON, fetches both
make examples       # build + run every commons_* example
make format-check   # clang-format --dry-run --Werror
make tidy           # clang-tidy (build-tidy/)
make sanitize       # ASan + UBSan
make ci             # format-check + tidy + test + sanitize + release + integrations
```

Meson: `meson setup build-meson -Dtests=true -Dexamples=true && meson test -C build-meson`
(add `-Djson=true` / `-Dulid=true` to force an integration).

## Naming / style notes

- Namespace `comms`; types `CamelCase` (`FixedString`); numeric aliases and
  functions `lower_case` (`i32`, `to_json`, `Prioritized::priority`).
- Intentional naming exceptions are wrapped in scoped
  `// NOLINT(BEGIN/END)(readability-identifier-naming)` with a rationale comment:
  the CSS palette names in `color.hpp`, `Prioritized`'s Spring-style
  `SCREAMING_CASE` sentinels, and `PrioritizedSet`'s STL-style `const_iterator`.
- 4-space indent, 100-col, LLVM-based `.clang-format`. Includes regrouped:
  `<commons/...>` first.
- C++23 required (`cxx_std_23`); `cmake_minimum_required(VERSION 3.25)`.
- Version `0.1.7` is declared once in CMake `project()` and Meson `project()`.
  `commons/version.hpp` is **generated** from `commons/version.hpp.in` by the
  build (into the build tree, not the source tree): it defines the
  `COMMONS_VERSION_MAJOR/MINOR/PATCH/STRING` macros and the `comms::version` /
  `version_major|minor|patch` constants. The umbrella includes it. Bump the
  version in the two `project()` declarations only.
