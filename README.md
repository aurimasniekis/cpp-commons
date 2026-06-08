# Commons

[![CI](https://github.com/aurimasniekis/cpp-commons/actions/workflows/ci.yml/badge.svg)](https://github.com/aurimasniekis/cpp-commons/actions/workflows/ci.yml)
[![Docs](https://github.com/aurimasniekis/cpp-commons/actions/workflows/docs.yml/badge.svg)](https://aurimasniekis.github.io/cpp-commons/)

A header-only C++23 library of small, shared building-block types — a
compile-time fixed-size string, Rust-flavoured fixed-width numeric aliases, an
RGBA `Color` with full HSL/HSV manipulation and CSS/Material-UI palettes, an
Iconify `Icon` identifier, presentation metadata (`DisplayInfo`), a compile-time
named-`Flag` system, a Spring-style `Prioritized` ordering toolkit,
`SemVer` / `VersionConstraint` semantic-version types, an `IOrigin`
provenance envelope, and a strong-typed `Id<Tag, Repr>` identifier. Every type
carries optional nlohmann/json serialization that turns on by itself when the
dependency is available; `Id<Tag, ulid::Ulid>` lights up the same way when
cpp-ulid is on the include path. The namespace is `comms`; headers live under
`<commons/...>`.

## Why use this library?

- **Good for** sharing one definition of common vocabulary types across several
  projects instead of re-implementing them per repository.
- **Good for** UI-adjacent backend code: colors, icons, and display metadata
  that need to round-trip to JSON for a frontend.
- **Good for** carrying schemaless, JSON-like data: `comms::md` (with the
  `comms::Metadata` document-root alias) is a dynamic `Value`/`Object`/`Array`
  tree with path lookup, deep merge, and JSON round-trip.
- **Light by default.** The core depends only on the C++23 standard library.
  The JSON hooks stay completely inert unless nlohmann/json is on the include
  path, so you never pay for an integration you don't use.
- **Compile-time friendly.** `FixedString`, `Color`, and `Icon` are literal
  types usable in `constexpr` and `static_assert` contexts and as non-type
  template parameters.
- **Not ideal for** large, hot containers: `PrioritizedSet` and `FlagSet` are
  designed for config-sized collections and use linear-time lookups.
- **Not ideal for** projects that cannot move to C++23 — the whole library
  requires it.

## Quick example

```cpp
#include <commons/commons.hpp>

#include <iostream>

int main() {
    namespace c = comms;

    c::FixedString tag{"order.created"};   // compile-time string, usable as an NTTP
    c::u32 count = 42;
    c::f64 ratio = 1.0 / 3.0;

    std::cout << tag.view() << " x" << count << " (" << ratio << ")\n";
    std::cout << "commons " << c::version << "\n";
}
```

`FixedString` is built with class template argument deduction (CTAD) straight
from the literal, so you never spell its size. It is a *structural* type, which
means it can also appear directly in a template argument list, e.g.
`Event<"order.created">`. The numeric aliases (`u32`, `f64`, …) are lowercase
names for the standard fixed-width types. Including `commons/commons.hpp` pulls
in every core type plus the self-gating JSON hooks; it is always safe to include
even when nlohmann/json is absent.

## Installation

`commons` is a header-only `INTERFACE` library. There is no compiled artifact to
link — you only need the headers on your include path and C++23 enabled.

Package-manager support (vcpkg, Conan, system packages) is not provided. The
supported integration paths are CMake, Meson, and copying the headers.

### CMake — vendored subdirectory

The most reliable option: drop the repository into your tree (a submodule or
copy) and add it.

```cmake
cmake_minimum_required(VERSION 3.25)
project(example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/commons)

add_executable(example main.cpp)
target_link_libraries(example PRIVATE commons::commons)
```

Linking `commons::commons` brings the include directory and the `cxx_std_23`
requirement along with it.

### CMake — FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    commons
    URL      https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.6.tar.gz
    URL_HASH SHA256=04047f92aca576555346b17f628e516b9fe5caab1dbdc8a853245a3a83f27be0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(commons)

target_link_libraries(example PRIVATE commons::commons)
```

By default no optional dependency is fetched: the JSON hooks and the ULID
`Id` repr both auto-detect their backing headers. To *force* an integration on
— which additionally fetches the dependency and hard-defines the gate macro —
configure with `-DCOMMONS_WITH_NLOHMANN_JSON=ON` (fetches nlohmann/json 3.12.0
or newer) and/or `-DCOMMONS_WITH_ULID=ON` (fetches cpp-ulid 1.0.0).

### CMake — installed package

After `cmake --install`, the package is consumable via `find_package`:

```cmake
find_package(commons 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE commons::commons)
```

Install rules are automatically disabled when nlohmann/json or cpp-ulid was
fetched (a fetched dependency cannot be re-exported). For a clean install,
leave the gate options `OFF` (the default) or provide the dependencies through
system packages.

### Meson

```meson
commons_dep = dependency('commons', version: '>=0.1.0',
    fallback: ['commons', 'commons_dep'])
```

Meson options mirror the CMake ones: `-Djson=true|false`,
`-Dulid=true|false`, `-Dtests=true|false`, `-Dexamples=true|false`. A
`pkg-config` file is generated on install.

### Manual / header-only

Copy `include/commons` onto your include path and compile with C++23. The only
generated header is `commons/version.hpp`: CMake and Meson produce it from
`commons/version.hpp.in` using the project version. For a pure manual copy,
either configure once with CMake/Meson and copy the generated
`commons/version.hpp` alongside the rest, or create it by hand from the template
(replace the four `@PROJECT_VERSION...@` tokens). It is only needed if you
include the umbrella `commons/commons.hpp` or `commons/version.hpp` directly.

## Requirements

- **C++23.** The library uses structural non-type template parameters,
  `constexpr` `std::string_view`, `std::format`, and concepts. CMake enforces
  this with `target_compile_features(commons INTERFACE cxx_std_23)`.
- **Build tooling:** CMake 3.25 or newer, or Meson 1.3.0 or newer. Neither is
  required if you only copy the headers.
- **Optional dependencies:** [nlohmann/json](https://github.com/nlohmann/json)
  3.12.0 or newer enables `<commons/json.hpp>`;
  [cpp-ulid](https://github.com/aurimasniekis/cpp-ulid) 1.0.0 enables
  `comms::Id<Tag, ulid::Ulid>`.

## Core concepts

### `comms::FixedString<N>`

A fixed-capacity string whose contents are fixed at compile time. `N` is the
buffer size *including* the trailing null terminator, so `size()` returns
`N - 1`. Because it is a structural type, it can be a non-type template
parameter.

```cpp
#include <commons/fixed_string.hpp>

#include <iostream>

template <comms::FixedString Name>
struct Event {
    static constexpr std::string_view name = Name.view();
};

int main() {
    comms::FixedString id{"login"};   // N = 6 (5 chars + null), size() == 5
    std::cout << id.view() << " / " << id.size() << "\n";

    static_assert(Event<"login">::name == "login");
}
```

It converts implicitly to `std::string_view`, and `operator==` compares against
any `FixedString<M>` (different sizes simply compare unequal).

### `comms::Color`

Four `u8` channels (`r`, `g`, `b`, `a`), with almost the entire API `constexpr`:
packed-integer conversions, HSL/HSV conversion, channel and alpha tweaks, the
HSL transforms, WCAG luminance/contrast, palette generation, and parsing. Only
the `std::string`-producing methods are non-`constexpr`. The default `Color` is
opaque black.

```cpp
#include <commons/literals.hpp>   // brings in color.hpp and the _color literal

#include <iostream>

int main() {
    using comms::Color;
    using namespace comms::literals;

    constexpr Color indigo = "#6366f1"_color;   // compile-time hex literal
    std::cout << indigo.lighten(0.15).to_hex_string() << "\n";
    std::cout << indigo.complement().to_hex_string() << "\n";
}
```

### `comms::Icon`

A value type holding an Iconify `set:name` identifier (e.g. `mdi:abacus`) inline
in a 64-byte buffer — no heap, trivially copyable, usable in `constexpr`
contexts. Construct it from a whole value or from the two parts; both validate.

```cpp
#include <commons/literals.hpp>   // brings in icon.hpp and the _icon literal

#include <iostream>

int main() {
    using namespace comms::literals;

    constexpr comms::Icon cog = comms::Icon::from("mdi", "cog");
    constexpr comms::Icon home = "mdi:home"_icon;   // compile-time literal
    std::cout << cog.value() << " | " << cog.set() << " | " << cog.name() << "\n";
    std::cout << home.value() << "\n";
}
```

### `comms::DisplayInfo`

Optional presentation metadata — `name`, `description`, `icon`, `color`, every
field an `std::optional`. The intent is *static* data attached to a type once and
never mutated. The `icon`/`color` fields reuse `Icon`/`Color`, so they serialize
to JSON for a frontend out of the box.

### `comms::Flag` family

Compile-time named flags grouped into categories, a runtime `FlagSet` that keeps
insertion order, a program-wide `GlobalFlagRegistry`, and mixins for types that
own a flag set. Flags are *types*, declared (and optionally auto-registered) with
the `COMMONS_*_FLAG*` macros.

### `comms::IOrigin`

A polymorphic provenance envelope — *where a definition came from* — for an open
set of sources. The `kind()` discriminator is a compile-time `FixedString`
template parameter: derive from `OriginKind<"yourkind", YourType>` and you get
`kind()`, a deep `clone()`, and a `DisplayInfo`-backed `info()` (so every origin
is also `Displayable`). Built-ins are `CoreOrigin`, `InternalOrigin`,
`ExternalOrigin` (carrying a `source`), and `UnknownOrigin`; carry one as an
`OriginPtr` (`std::unique_ptr<IOrigin>`). New kinds self-register into the
`GlobalOriginRegistry` with `COMMONS_REGISTER_ORIGIN(Type)` — mirroring the flag
registry — so a JSON `kind` can be resolved back to the right type.

### `comms::Prioritized`

Attaches integer priorities to orderable things and sorts them deterministically,
mirroring Spring's `Ordered`: **lower value sorts first** (higher precedence).
`HIGHEST_PRECEDENCE` is `INT_MIN`, `LOWEST_PRECEDENCE` is `INT_MAX`, and the
neutral `DEFAULT_PRIORITY` is `0`.

### `comms::SemVer`

A [Semantic Versioning 2.0.0](https://semver.org) value — `major.minor.patch`
plus optional prerelease and build metadata. `SemVer::parse` is non-throwing
(returns `std::optional`), accepts an optional `v` prefix, and parses partial
versions leniently (`"1"`, `"1.2"`). Ordering implements the full §11 prerelease
precedence — a prerelease ranks below its release and numeric identifiers compare
numerically, so `1.0.0-alpha.2 < 1.0.0-alpha.10 < 1.0.0-beta < 1.0.0` — while
build metadata is preserved in the text form but ignored by comparison and
equality. Because it holds `std::string` members it is a runtime type (not
`constexpr`).

### `comms::VersionConstraint`

An npm-style semver range that answers `satisfies(SemVer)`: `*`, an exact
version, the comparisons `>=`/`>`/`<=`/`<`/`!=`, caret (`^1.2.3` → `>=1.2.3
<2.0.0`) and tilde (`~1.2.3` → `>=1.2.3 <1.3.0`) ranges, and space-separated
intersections like `>=1.0.0 <2.0.0` (all must match). Unlike `SemVer::parse`,
`VersionConstraint::parse` **throws** `std::invalid_argument` on a malformed
sub-version.

### `comms::Id<Tag, Repr>`

A strong-typed identifier: a `Repr` value tagged with a phantom `Tag` so ids
of different kinds cannot be mixed even when the underlying representation is
identical. The allowed reprs are deliberately narrow — the unsigned
fixed-width ints (`std::uint8_t` / `16` / `32` / `64`), `std::string`, and —
gated by `COMMONS_WITH_ULID` — `ulid::Ulid`. Aliases `Uint8Id<Tag>` …
`Uint64Id<Tag>`, `StringId<Tag>`, and `UlidId<Tag>` save typing; the
`COMMONS_DEFINE_UINT{8,16,32,64}_ID`, `COMMONS_DEFINE_STRING_ID`, and
`COMMONS_DEFINE_ULID_ID` macros emit both a `Name##Tag` (with a
`static constexpr std::string_view name`) and the matching `using` alias in
one shot. `to_string` delegates to the underlying repr; `display_string`
prefixes it with `Tag::name` for named tags; and the `std::formatter<Id>`
specialization inherits from `std::formatter<Repr>` so any spec the wrapped
type accepts (e.g. `"{:#x}"` for the uint reprs) works transparently.

### `comms::md` — dynamic value tree (`comms::Metadata`)

A JSON-like dynamic value tree for schemaless data. `comms::md::Value` is a
discriminated union of null / bool / `i64` / `u64` / `float` / `double` /
`string` / `Array` / `Object`; `Array` is a `vector<Value>` and `Object` a
string-keyed map with transparent `string_view` lookup. The common
document-root case is surfaced at the Commons root as **`comms::Metadata`**
(an alias for `comms::md::Object`); everything else stays under `comms::md::`.
On top of the containers it ships dotted/bracketed **path lookup**
(`find_path` / `require_path` / `contains_path`, e.g. `"a.b[0].c"`), **deep
merge** (nested objects recurse, scalars overwrite, arrays replace),
order-independent `std::hash` for `Object`, compact-JSON `operator<<` and
`std::formatter` (hand-rolled, no nlohmann dependency), and free-function
helpers (`contains` / `find_ptr` / `require*` / `get_*_if` / `merge`). Its
exception trio roots at `comms::Exception`: `MetadataError` → `MissingKeyError`
/ `TypeError`. The whole library is one self-contained header,
`commons/metadata.hpp`; the `Value`/`Object`/`Array` ⇄ JSON round-trip lives in
`commons/json.hpp` under `COMMONS_WITH_NLOHMANN_JSON`.

### `comms::Exception`

The root of the Commons exception hierarchy — a thin `std::runtime_error`
subclass that exists so every exception the library throws is catchable as one
type (`catch (const comms::Exception&)`). Feature-specific exceptions derive from
it; the reason exceptions below are the first family.

### `comms::IReason` / `comms::IFailureReason`

A polymorphic *"why" envelope* for an outcome that is **not** an exception — a
rejected request, a cancelled job, a refused operation. It carries an `int code`,
a `std::string message`, a `created_at` timestamp, and an optional `metadata`
bag (a `comms::Metadata` / `comms::md::Object`, empty by default, for arbitrary
structured context), and — like `IOrigin` — it
is an **open set**: each kind has a compile-time `kind()` discriminator via the
`ReasonKind<"kind", Derived>` CRTP base, which wires `clone()` and an *optional*
`DisplayInfo`-backed `info()`. `comms::IFailureReason` refines it for a
**developer-controlled failure** (code that deliberately submits a failure rather
than throwing), adding `throw_as_exception()`. Built-ins are
`GenericReason`/`UnknownReason` and the failure counterparts
`GenericFailureReason`/`UnknownFailureReason`; `ReasonPtr` / `FailureReasonPtr`
are the owning handles and `make_reason<R>` / `make_failure_reason<R>` the
factories. The one-line `COMMONS_DEFINE_REASON(Ident, "kind")` /
`COMMONS_DEFINE_FAILURE_REASON(Ident, "kind")` macros define **and** register a
kind (inheriting the `ReasonKind` constructors `()`/`(message)`/`(code)`/
`(code, message)`); hand-write the class only for a baked-in default or a
`display_info()`. New kinds self-register into the `GlobalReasonRegistry` with
`COMMONS_REGISTER_REASON(Type)`, which also filters by family: `kinds()`,
`failure_kinds()` / `non_failure_kinds()`, and the generic `kinds_of<Base>()` for
a custom `IReason` sub-interface. A free `operator<=>` orders reasons by
`created_at`. Text output (`to_string`/`<<`/`std::format`) emits `title: message`,
where the virtual `title()` defaults to the concrete type name (`RateLimitReason`)
and the generic/unknown built-ins override it to carry their code
(`GenericReason(123)`). The reason exceptions sit under `comms::Exception`:
`ReasonException` (carries any reason) → `FailureReasonException` (thrown by
`throw_as_exception()`), with `RejectException` / `CancelException` as siblings.

JSON (when `COMMONS_WITH_NLOHMANN_JSON` is on) round-trips a reason as
`{"kind","code","message","created_at"}` (plus a `"metadata"` object when that
bag is non-empty, omitted when empty), resolving `kind` through the registry,
so a `ReasonPtr` deserializes back to the right concrete kind. A **sub-reason
with extra fields** extends the JSON by overriding the gated virtual
`IReason::write_json` / `read_json` hooks (calling the base first) — its fields
then round-trip through `ReasonPtr` automatically. These hooks live in
`reason.hpp` (gated), the one place the library puts serialization in a type
header rather than in `json.hpp`; the trade-off is that the JSON gate must be
resolved identically across your whole build (it affects `IReason`'s vtable).

### `comms::Identity` / `comms::Ability`

A shared **authentication + authorization** vocabulary: *who* a principal is
(`comms::IIdentity`) and *what* it may do (`comms::IAbility`). Both are
**polymorphic open sets** built exactly like `IOrigin` / `IReason` — a `kind()`
discriminator supplied by an `AbilityKind<"kind", Derived>` /
`IdentityKind<"kind", Derived>` CRTP base (wiring `clone()` and an optional
`DisplayInfo`-backed `info()`), self-registration into a program-wide
`GlobalAbilityRegistry` / `GlobalIdentityRegistry`, and the one-line
`COMMONS_DEFINE_ABILITY(Ident, "kind")` / `COMMONS_DEFINE_IDENTITY(Ident, "kind")`
macros (plus `COMMONS_REGISTER_*` for a hand-written kind). `AbilityPtr` /
`IdentityPtr` are the owning handles and `make_ability<A>` / `make_identity<I>`
the factories.

The check reads **`required.allowed(subject)`**: the *required* ability is the
receiver and the argument is the candidate that must satisfy it
(`read_perm.allowed(admin_role)`, `read_perm.allowed(user)`). The default
`IAbility::allowed` is "same kind + same `value`"; a kind owns its own rule by
overriding it. An identity holds a list of `abilities`, and
`identity.satisfies(required)` (equivalently `required.allowed(identity)`) tries
each held ability against the requirement. Built-in abilities are `RoleAbility`
(`value` = role name), the extensible `RecordPermissionAbility` (an
`action`/`resource` pair matched with a `"*"` wildcard — the worked example of a
kind that overrides `allowed`/`equals`/the JSON hooks), and the macro-defined
`GenericAbility` / `UnknownAbility`. Built-in identities are the value-carrying
`UserIdentity` / `ServerIdentity` / `ApiClientIdentity` / `UnknownIdentity`,
`RootIdentity` (satisfies **everything**), and `NoIdentity` (satisfies
**nothing** — the `AuditRecord` default, so a record's principal is never null).

JSON (when `COMMONS_WITH_NLOHMANN_JSON` is on): an ability/identity round-trips
as `{"kind","value", …}` (an identity adds an `abilities` array when non-empty),
resolving `kind` through the registry so a pointer deserializes back to the right
concrete kind (an unknown kind throws). Like `reason.hpp`, **Identity/Ability
keep their per-field (de)serialization in gated virtual `write_json` / `read_json`
hooks in their own headers** (so a sub-kind extends the JSON by overriding them) —
the same vtable/uniform-gate caveat applies.

### `comms::AuditRecord` family

A small family of value types for an **audit trail** — *who* did *what*, *when*,
*from where*. `comms::AuditRecord` is the base record: an `identity` (a
polymorphic `comms::IdentityPtr` defaulting to `comms::NoIdentity` — never null;
set it with `set_identity(make_identity<UserIdentity>("alice"))`), a `timestamp`
(a real `time_point` defaulting to `now()`, like `IReason::created_at`), optional
request context (`ip`, `user_agent`, `session_id`), a `related_ids` map (name → id
string), and a free-form `metadata` bag (`comms::Metadata`, empty by default).
Because the owning `identity` pointer is neither copyable nor
defaulted-comparable, `AuditRecord` is **not** an aggregate: its copy operations
deep-clone the identity (move is defaulted) and `operator==` is hand-written
(comparing the identity with `comms::identity_equal`). Ids are stored as
**strings**: the templated `set_session_id(id)` / `add_related_id(name, id)`
helpers take any `comms::Id<Tag, Repr>` and capture `comms::to_string(id)`, so a
single record can reference ids of different kinds (which a heterogeneous
`map<string, Id>` could not). Records compare with `operator==` only — `Metadata`
is equality-only, so there is no defaulted `<=>`.

`comms::ChangeAuditRecord<T>` extends the base with `before` / `after`, each a
`std::optional<T>` (a create has no `before`, a delete has no `after`); it
inherits every field and helper. `comms::AuditLog<Record>` is a capped,
insertion-ordered collection: `push()` appends and, once the size exceeds the
capacity, drops the oldest (front) records (FIFO; capacity `0` keeps nothing).
The cap is a runtime member defaulting to `COMMONS_AUDIT_RECORDS_CAPACITY` (a
build/config **value-override seam**, default 3, wired exactly like the
`COMMONS_PRIORITIZED_*` macros). `AuditRecords` and `ChangeAuditRecords<T>` are
the two ready-made aliases.

JSON (when `COMMONS_WITH_NLOHMANN_JSON` is on): an `AuditRecord` is an object
that always carries `identity` (a `{"kind", …}` object) + `timestamp` (epoch
milliseconds), with the optional fields emitted only when present/non-empty; a
`ChangeAuditRecord<T>`
adds `before` / `after`; an `AuditLog` is a JSON array of records. Capacity is
**not** serialized, so a log read back under a smaller cap keeps the newest N (a
within-capacity log round-trips exactly). The millisecond encoding truncates
sub-millisecond ticks, so use ms-aligned timestamps when an exact round-trip
matters.

### `comms::Lifecycle` family

A small family for the **status history** of an object — a status, a chain of
transitions, per-status reports, and a timeline that owns the history and
notifies subscribers. It follows the value-and-template style of `AuditRecord`
(generic `T`, no registry/`kind()`/CRTP), not the open-set style of `IReason`.

`comms::LifecycleStatus` is the built-in string-valued status (a thin named value
that compares and orders by name, with `to_string`/`operator<<`/`std::formatter`/
`std::hash`). `comms::StatusTransition<T>` is a self-contained transition event —
the new `status` and its `timestamp`, plus the `previous` status and its
timestamp (each `std::optional`, absent for the first), so `duration()` works
without a raw-pointer linked list. `comms::StatusReport<T>` is a per-status period
summary (time spent in the status, and the total since the timeline start).

`comms::StatusTransitionTimeline<T>` (with `T` defaulting to `LifecycleStatus` and
constrained by the `StatusType` concept — default-constructible, copyable,
equality-comparable) owns the transition history. `transition_to(status[, at])`
appends an event (timestamp defaults to `now()`) and returns it; the status
accessors (`current_status`/`first_status`/`current_transition`/…) throw
`comms::LifecycleError` on an empty timeline, while the temporal queries
(`status_duration`, `status_timestamp`, `current_status_duration`,
`duration_between`, `total_duration`) return `std::optional` (`nullopt` when
empty). `status_reports()` returns one `StatusReport<T>` per transition. The
timeline is **unbounded** (every transition is kept). `subscribe(fn)` /
`subscribe(status, fn)` register listeners (the latter status-filtered) and return
an id for `unsubscribe(id)` / `unsubscribe_all()`; listeners fire after each
`transition_to`. The timeline is **thread-safe** via an internal `std::mutex`, so
it is **non-copyable / non-movable** (the small value types above are not
affected); listeners are notified after the lock is released, so a listener may
safely re-enter the timeline.

JSON (when `COMMONS_WITH_NLOHMANN_JSON` is on): a `LifecycleStatus` is a plain
string, `StatusTransition<T>` / `StatusReport<T>` are objects (timestamps and
durations as milliseconds, optional `previous` fields omitted when absent), and a
timeline is a JSON array of transitions (subscribers are transient and not
serialized).

## Common usage patterns

### Working with colors

```cpp
#include <commons/commons.hpp>

#include <format>
#include <iostream>
#include <optional>

int main() {
    using comms::Color;

    // Parsing returns std::optional — always check before dereferencing.
    if (std::optional<Color> red = Color::parse("rgb(255 0 0)")) {
        std::cout << red->to_hex_string() << "\n";          // #ff0000
    }

    // Named colors, hex, and HSL functional notation all parse.
    std::cout << Color::parse("rebeccapurple")->to_hex_string() << "\n";
    std::cout << Color::parse("hsl(120, 100%, 50%)")->to_hex_string() << "\n";

    // Palettes from the CSS and Material-UI sets.
    std::cout << comms::Colors::css::indigo.to_hex_string() << "\n";
    std::cout << comms::Colors::mui::red_500.to_hex_string() << "\n";   // flat alias
    std::cout << comms::Colors::mui::red[700].to_hex_string() << "\n";  // indexed shade
    std::cout << comms::Colors::mui::blue.accent(200).to_hex_string() << "\n";

    // WCAG: choose readable text and report contrast.
    constexpr Color bg{0x63, 0x66, 0xf1};
    const Color text = bg.readable_text_color();   // black or white
    std::cout << text.to_hex_string() << " contrast "
              << bg.contrast_ratio(text) << "\n";

    // std::format specs: h (lowercase hex, default), H (uppercase), r (CSS rgb).
    std::cout << std::format("{:H}", bg) << "\n";
    std::cout << std::format("{:r}", bg.fade(0.5)) << "\n";
}
```

This covers the main paths: successful parsing (with the mandatory `optional`
check), the palette accessors, the WCAG helpers, and the formatter specs.
`fade(opacity)` takes a `[0, 1]` opacity and sets the alpha channel. Transforms
such as `lighten`/`darken`/`saturate`/`rotate_hue` clamp their results, so they
never produce an out-of-range channel.

> **Pitfall — invalid shades throw.** `mui::red[shade]` accepts only
> `50, 100, 200, …, 900`, and `accent(shade)` only `100, 200, 400, 700`. Any
> other value throws `std::out_of_range`. The flat aliases (`red_500`, `red_a200`)
> cannot be misindexed, so prefer them for fixed shades.

### Building and parsing icons

```cpp
#include <commons/icons.hpp>   // opt-in predefined catalogs

#include <iostream>

int main() {
    // Predefined Material Design Icons (only via <commons/icons.hpp>).
    constexpr comms::Icon abacus = comms::Icons::mdi::abacus;
    std::cout << abacus.value() << "\n";

    // Keyword-named icons get a trailing underscore; the value is unchanged.
    std::cout << comms::Icons::mdi::delete_.value() << "\n";   // mdi:delete

    // Non-throwing validation for runtime/untrusted input.
    if (std::optional<comms::Icon> icon = comms::Icon::parse("mdi:cog")) {
        std::cout << "valid: " << icon->value() << "\n";
    }
    if (!comms::Icon::parse("not-an-icon")) {
        std::cout << "rejected (no single ':')\n";
    }
}
```

Use `Icon::parse` for runtime input — it returns `std::nullopt` on malformed
values. Use `Icon::from` when you want a hard failure: it throws
`std::invalid_argument` for a malformed value or `std::length_error` for one that
exceeds the 64-byte capacity. In a `constexpr` context, either failure becomes a
compile error.

> **Pitfall — predefined catalogs are not in the umbrella.** The MDI table has
> 7,447 entries, so `commons/commons.hpp` does not include it. Add
> `#include <commons/icons.hpp>` in the translation units that need
> `comms::Icons::mdi::...`.

### Attaching display metadata to a type

There are two ways to attach `DisplayInfo`, and a concept to detect it.

```cpp
#include <commons/display_info.hpp>

#include <iostream>

// 1) Intrusive: a static member returning a reference.
struct Widget {
    static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "Widget",
            .icon = comms::Icon::from("mdi:widgets"),
            .color = comms::Colors::css::indigo,
        };
        return info;
    }
};

// A third-party enum we cannot edit.
enum class Severity { Info, Warning, Error };

// 2) Non-intrusive: specialize the trait (in namespace comms).
template <>
struct comms::HasDisplayInfo<Severity> {
    static const DisplayInfo& display_info() {
        static const DisplayInfo info{.name = "Severity",
                                      .color = Colors::css::orange};
        return info;
    }
};

template <typename T>
    requires comms::Displayable<T>
void show(std::string_view label) {
    const auto& d = comms::display_info<T>();
    std::cout << label << ": " << d.name.value_or("(none)") << "\n";
}

int main() {
    show<Widget>("intrusive");
    show<Severity>("trait");

    static_assert(!comms::Displayable<struct Plain>);   // no metadata → not Displayable
}
```

`comms::display_info<T>()` dispatches to whichever mechanism is present.
`comms::Displayable<T>` reports whether either exists, so you can constrain
templates on it. Calling `display_info<T>()` on a type that has neither is a
compile error, by design.

### Declaring and collecting flags

```cpp
#include <commons/flag.hpp>

#include <iostream>

namespace {
COMMONS_FLAG_CATEGORY(Network, "network");
COMMONS_DEFINE_FLAG_IN(Ipv6, "ipv6", Network);        // defined + auto-registered
COMMONS_DEFINE_FLAG_IN(KeepAlive, "keep-alive", Network);
COMMONS_DEFINE_FLAG(Verbose, "verbose");              // default "unset" category

// A builder that owns a FlagSet limited to Network flags and is readable
// through the IHasFlags interface.
class Config : public comms::FlagBuilderGetters<Config, Network> {};
}  // namespace

int main() {
    comms::FlagSet set;
    set.insert<Verbose>();
    set.insert<Ipv6>();
    set.insert<Ipv6>();   // duplicate by name — ignored, returns false

    for (const auto& f : set) {                       // insertion order preserved
        std::cout << f.name << " [" << f.category << "]\n";
    }

    Config cfg;
    cfg.flag<Ipv6>().set_flag<KeepAlive>(true);       // fluent, returns Config&
    // cfg.flag<Verbose>();  // will not compile: Verbose is not in Network

    const comms::IHasFlags& view = cfg;               // read polymorphically
    std::cout << "has ipv6? " << view.has_flag<Ipv6>() << "\n";

    std::cout << comms::GlobalFlagRegistry::instance().flags().size()
              << " flags registered\n";
}
```

`FlagSet` deduplicates by flag name and keeps insertion order; `insert` returns
`false` when the name is already present. `group_by_category()` returns a
`std::map` from category name to the flags in it. The `COMMONS_DEFINE_FLAG*`
macros register each flag into the `GlobalFlagRegistry` automatically; the
builder mixins (`FlagBuilderMixin` for a private set, `FlagBuilderGetters` for an
observable one) constrain their typed overloads to the listed categories.

### Ordering things by priority

```cpp
#include <commons/prioritized.hpp>

#include <iostream>
#include <string>

// Carry a mutable priority via the CRTP builder mixin.
struct Adapter : comms::PrioritizedBuilder<Adapter> {
    std::string name;
    explicit Adapter(std::string n) : name(std::move(n)) {}
};

int main() {
    Adapter fast("fast");
    fast.highest_priority();                 // fluent; sets INT_MIN
    std::cout << fast.name << " = " << fast.priority() << "\n";

    // Attach a priority to any value. The FIRST argument is always the priority.
    auto level = comms::with_priority(-5, 42);          // WithPriority<int>
    std::cout << *level << " @ " << level.priority() << "\n";

    // A set that iterates in (priority asc, insertion-order asc) order.
    comms::PrioritizedSet<std::string> pipeline;
    pipeline.insert(5, "compress");
    pipeline.insert(1, "auth");
    pipeline.insert(5, "log");               // ties with "compress" → insertion order
    for (const auto& stage : pipeline) {
        std::cout << "[" << pipeline.priority_of(stage) << "] " << stage << "\n";
    }
    // Prints auth (1), compress (5), log (5).
}
```

`get_priority(x)` is a uniform, null-safe lookup that works on values,
references, raw pointers, and smart pointers, falling back to `DEFAULT_PRIORITY`
when no priority is discoverable. `PrioritizedCompare` and
`LenientPrioritizedCompare<T>` order `std::shared_ptr`s for use as the comparator
of a `std::set`.

> **Pitfall — `insert` never updates an existing priority.** Like `std::set`,
> re-inserting an equal value is a no-op; the originally stored priority stays.
> Use `set_priority(value, p)` to change it. Also note `PrioritizedSet`'s
> `insert`/`find`/`erase(value)` are O(n) — it targets config-sized collections,
> not large data sets.

### Versions and constraints

```cpp
#include <commons/semver.hpp>
#include <commons/version_constraint.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

int main() {
    // Parsing is non-throwing; a `v` prefix and partial versions are accepted.
    comms::SemVer v = comms::SemVer::parse("v1.4.0-rc.1").value();
    std::cout << v << "\n";                              // 1.4.0-rc.1

    // Full SemVer ordering: prereleases sort below the release, and numeric
    // identifiers compare numerically (alpha.2 < alpha.10).
    std::vector<comms::SemVer> versions;
    for (const auto* s : {"1.0.0", "1.0.0-alpha.10", "1.0.0-alpha.2", "1.0.0-beta"}) {
        versions.push_back(comms::SemVer::parse(s).value());
    }
    std::ranges::sort(versions);
    // -> 1.0.0-alpha.2, 1.0.0-alpha.10, 1.0.0-beta, 1.0.0

    // Range constraints answer satisfies(SemVer).
    auto range = comms::VersionConstraint::parse(">=1.2.0 <2.0.0");
    std::cout << std::boolalpha
              << range.satisfies(comms::SemVer::parse("1.5.0").value()) << "\n";  // true
}
```

`SemVer` works directly in `std::set`/`std::map`/`std::sort` (via its
`operator<=>`) and in `std::unordered_*` (via the `std::hash` specialization);
both `SemVer` and `VersionConstraint` also support `std::format`, `operator<<`,
and JSON.

### Strong-typed ids

```cpp
#include <commons/id.hpp>

#include <format>
#include <iostream>
#include <string>

// Both macros emit a UserIdTag/OrderIdTag with a `name` member and a `using`
// alias. They have to be invoked at namespace (or class) scope, since they
// declare a struct with a static member.
COMMONS_DEFINE_UINT64_ID(UserId, "user");
COMMONS_DEFINE_STRING_ID(OrderId, "order");

int main() {
    UserId u{1234567u};
    OrderId o{std::string{"o-abc-1"}};

    // display_string prefixes with the tag name; to_string is the bare repr.
    std::cout << comms::display_string(u) << "\n";   // user/1234567
    std::cout << comms::display_string(o) << "\n";   // order/o-abc-1

    // The std::formatter inherits from std::formatter<Repr>, so any spec the
    // underlying type accepts works on the Id directly.
    std::cout << std::format("{:#x}", u) << "\n";    // 0x12d687

    // Different tags are unrelated types — won't compile:
    // bool same = (u == UserId{0u});                // OK
    // bool same = (u == OrderId{"o-abc-1"});        // type mismatch
}
```

### Carrying dynamic metadata

```cpp
#include <commons/metadata.hpp>

#include <format>
#include <iostream>

int main() {
    namespace md = comms::md;

    // comms::Metadata is the root-level alias for comms::md::Object.
    comms::Metadata m;
    m["name"] = "sensor-7";
    m["enabled"] = true;
    m["tags"] = {"alpha", "beta"};                 // braced list → Array
    m["calib"] = {{"gain", 1.5}, {"offset", -2}};  // braced pairs → nested Object

    std::cout << std::format("{}\n", m);           // compact JSON

    // Typed read-back and dotted/bracketed path lookup.
    std::cout << "name      : " << m.require_string("name") << "\n";
    std::cout << "calib.gain: " << m.require_path("calib.gain").as_double() << "\n";
    std::cout << "tags[1]   : " << m.require_path("tags[1]").as_string() << "\n";

    // Deep merge: nested objects recurse, scalars overwrite, arrays replace.
    m.merge(md::Object{{"calib", md::Object{{"offset", -1}}}});
    std::cout << "merged    : " << m << "\n";
}
```

### Explaining outcomes with reasons

```cpp
#include <commons/reason.hpp>

#include <iostream>

// Define + register a domain-specific failure kind in one line. It inherits the
// ReasonKind constructors, so make_failure_reason<RateLimitReason>(429, "…")
// just works.
COMMONS_DEFINE_FAILURE_REASON(RateLimitReason, "rate_limit");

int main() {
    namespace c = comms;

    // A reason answers "why?" for a non-exceptional outcome.
    c::GenericReason rejected{403, "not allowed"};
    std::cout << c::to_string(rejected) << "\n";   // GenericReason(403): not allowed

    // Every reason carries an optional metadata bag for structured context.
    rejected.metadata["user_id"] = 42;
    rejected.metadata["scope"] = "admin";

    // A custom kind titles itself by its type name (no code).
    std::cout << c::to_string(RateLimitReason{}) << "\n";   // RateLimitReason:

    // A FailureReason is a developer-submitted failure. Where there is no
    // failure channel, throw_as_exception() packages it; a handler resubmits.
    try {
        RateLimitReason{429, "slow down"}.throw_as_exception();
    } catch (const c::FailureReasonException& e) {
        std::cout << "caught: " << e.what()
                  << " (code " << e.failure_reason()->code << ")\n";
    }

    // Reject/Cancel exceptions carry any reason; all derive from comms::Exception.
    try {
        throw c::CancelException(c::GenericReason{"user cancelled"});
    } catch (const c::Exception& e) {
        std::cout << "cancelled: " << e.what() << "\n";
    }

    // Filter the registry by family.
    const auto& reg = c::GlobalReasonRegistry::instance();
    for (const auto& k : reg.failure_kinds()) {
        std::cout << "failure kind: " << k << "\n";
    }
}
```

### Authn/authz with Identity & Ability

```cpp
#include <commons/ability.hpp>
#include <commons/identity.hpp>

int main() {
    namespace c = comms;

    // A required ability is the receiver; the candidate is the argument.
    const c::RoleAbility admin_required{"admin"};
    admin_required.allowed(c::RoleAbility{"admin"});   // true  (same kind + value)
    admin_required.allowed(c::RoleAbility{"editor"});  // false

    // RecordPermissionAbility matches an action/resource pair, "*" is a wildcard.
    const c::RecordPermissionAbility read_order{"read", "order"};
    read_order.allowed(c::RecordPermissionAbility{"*", "order"});  // true

    // An identity holds abilities; required.allowed(identity) ==
    // identity.satisfies(required).
    auto user = c::make_identity<c::UserIdentity>("alice");
    user->add_ability(c::make_ability<c::RoleAbility>("admin"));
    admin_required.allowed(*user);   // true
    user->satisfies(admin_required); // true (equivalent)

    // Root allows everything; None (the AuditRecord default) allows nothing.
    admin_required.allowed(*c::make_identity<c::RootIdentity>());  // true
    admin_required.allowed(*c::make_identity<c::NoIdentity>());    // false
}
```

### Recording audit trails

```cpp
#include <commons/audit_record.hpp>
#include <commons/identity.hpp>

#include <iostream>
#include <string>
#include <utility>

COMMONS_DEFINE_UINT64_ID(OrderId, "order");
COMMONS_DEFINE_STRING_ID(TenantId, "tenant");

int main() {
    namespace c = comms;

    // who / what / when / from where. identity defaults to NoIdentity (never
    // null); timestamp defaults to now(); the rest are optional.
    c::AuditRecord rec;
    rec.set_identity(c::make_identity<c::UserIdentity>("alice"));
    rec.ip = "192.0.2.1";
    rec.set_session_id(OrderId{42U});              // stored as "42"
    rec.add_related_id("order", OrderId{1001U});   // ids of any kind, by string
    rec.add_related_id("tenant", TenantId{"acme"});
    rec.metadata["action"] = c::md::Value{"checkout"};

    // before/after for a change; absent on create/delete respectively.
    c::ChangeAuditRecord<std::string> change;
    change.set_identity(c::make_identity<c::UserIdentity>("bob"));
    change.before = std::string{"pending"};
    change.after = std::string{"shipped"};

    // A capped log drops the oldest once it overflows (FIFO).
    c::AuditRecords log{2};                         // hold at most 2
    for (const auto* who : {"alice", "bob", "carol"}) {
        c::AuditRecord r;
        r.set_identity(c::make_identity<c::UserIdentity>(who));
        log.push(std::move(r));
    }
    std::cout << log.size() << " kept, oldest is "
              << log.front().identity->value << "\n";  // 2 kept, oldest is bob
}
```

### Tracking lifecycle status

```cpp
#include <commons/lifecycle.hpp>

#include <iostream>

int main() {
    namespace c = comms;

    // Start at an initial status (timestamp defaults to now()), then transition.
    c::StatusTransitionTimeline<> tl{c::LifecycleStatus{"open"}};

    // Subscribe to every change; subscribe(status, fn) filters to one status.
    tl.subscribe([](const c::StatusTransition<>& tr) {
        std::cout << "-> " << tr.status << "\n";
    });

    tl.transition_to(c::LifecycleStatus{"review"});
    tl.transition_to(c::LifecycleStatus{"closed"});

    std::cout << tl.current_status() << "\n";              // closed
    std::cout << tl.status_reports().size() << " stages\n";  // 3 stages
    // current_status() throws comms::LifecycleError on an empty timeline.
}
```

### JSON serialization (optional)

With nlohmann/json available, every public type gains `to_json`/`from_json`.

```cpp
// Build with -DCOMMONS_WITH_NLOHMANN_JSON=ON, or simply have nlohmann/json
// on the include path.
#include <commons/commons.hpp>
#include <commons/json.hpp>

#include <nlohmann/json.hpp>

#include <iostream>

int main() {
    using json = nlohmann::json;

    comms::FixedString id{"order.created"};
    json j = id;                                   // -> "order.created"
    auto back = j.get<comms::FixedString<14>>();   // round-trips

    json color = comms::Colors::css::indigo;       // -> "#4b0082" (hex string)
    json icon  = comms::Icon::from("mdi:cog");     // -> "mdi:cog"

    comms::cf64 signal{0.5, -1.25};
    json sig = signal;                             // -> [0.5, -1.25]

    std::cout << color.dump() << " " << icon.dump() << " " << sig.dump() << "\n";
}
```

The mappings are: `FixedString` and `Icon` ⇄ strings; `Color` ⇄ a hex string
(`#RRGGBB`, or `#RRGGBBAA` when not opaque); `Hsl`/`Hsv` ⇄ objects; `DisplayInfo`
⇄ an object with absent fields omitted; `FlagRef` ⇄ its name and `FlagSet` ⇄ an
array of names; `SemVer` ⇄ its canonical version string and `VersionConstraint`
⇄ its raw range string; `IOrigin`/`OriginPtr` ⇄ a `{"kind", …fields}` object
(null `OriginPtr` ⇄ `null`), with the four built-in kinds round-tripping their
fields and the `kind` resolved back through the `GlobalOriginRegistry` (an
unknown kind throws; a custom kind brings its own `to_json`/`from_json`);
`i128`/`u128` ⇄ decimal strings; the complex aliases ⇄
`[real, imaginary]` arrays; `WithPriority<T>` ⇄ `{"priority":N,"value":<T>}` and
`PrioritizedSet<T>` ⇄ a sorted array — both only when `T` is itself
JSON-serializable; `Id<Tag, Repr>` ⇄ the inner `Repr`'s natural JSON (a number
for the uint reprs, a string for `std::string`, the ULID string when ULID is
also enabled); `AbilityPtr`/`IdentityPtr` ⇄ a `{"kind","value", …}` object (an
identity adds an `abilities` array when non-empty), with `kind` resolved back
through the `GlobalAbilityRegistry`/`GlobalIdentityRegistry` (an unknown kind
throws) and per-field work in gated virtual `write_json`/`read_json` hooks so a
sub-kind extends the JSON by overriding them; `AuditRecord` ⇄ an object with
`identity` + millisecond `timestamp` and the optional fields omitted when
absent/empty, `ChangeAuditRecord<T>` adds `before`/`after`, and
`AuditLog<Record>` ⇄ a JSON array of records (capacity is not serialized);
`LifecycleStatus` ⇄ a string, `StatusTransition<T>`/`StatusReport<T>` ⇄ objects
(millisecond timestamps/durations, optional `previous` fields omitted), and
`StatusTransitionTimeline<T>` ⇄ a JSON array of transitions.

## Error handling

The library uses three distinct strategies, by type:

- **`std::optional` for parsing.** `Color::parse`, `Color::parse_hex`, and
  `Icon::parse` return `std::nullopt` on malformed input. Check before
  dereferencing.
- **Exceptions for hard failures.** `Icon::from` throws `std::invalid_argument`
  (malformed) or `std::length_error` (too long). `Color`'s MUI shade accessors
  (`operator[]`, `accent`) throw `std::out_of_range`. The `std::format`
  specializations throw `std::format_error` on a bad spec. The JSON `from_json`
  hooks throw nlohmann's exception type when a value is invalid (an unparseable
  color, a string too long for a `FixedString`, an unregistered flag name, …).
- **Compile-time errors.** The `_color` and `_icon` user-defined literals are
  `consteval`, so a malformed literal fails to compile. `Icon::from` in a
  `constexpr` context turns its throws into compile errors. Calling
  `display_info<T>()` on a non-`Displayable` type is a compile error.

```cpp
#include <commons/color.hpp>
#include <commons/icon.hpp>

#include <iostream>

int main() {
    // optional path
    if (auto c = comms::Color::parse("#zzzzzz"); !c) {
        std::cout << "bad color\n";
    }

    // exception path
    try {
        (void)comms::Icon::from("missing-colon");
    } catch (const std::invalid_argument& e) {
        std::cout << "rejected: " << e.what() << "\n";
    }
}
```

## Edge cases and pitfalls

- **`FixedString` size counts the null terminator.** `FixedString{"hi"}` has
  `N == 3` and `size() == 2`. When you need to name the type explicitly for JSON,
  use the size *with* the terminator: a 13-character string round-trips through
  `FixedString<14>`.
- **`FixedString` JSON overflow throws.** Deserializing a string longer than the
  fixed capacity is an error, not a silent truncation.
- **Unchecked `parse` dereference is undefined behavior.** `Color::parse(...)`
  and `Icon::parse(...)` return `std::optional`; calling `->` on a `nullopt`
  result is UB. Always check.
- **MUI shade accessors throw on bad shades.** See the color section above —
  prefer the flat aliases (`red_500`) for compile-time-fixed shades.
- **128-bit aliases may be absent.** `i128`/`u128` are only defined when the
  compiler provides 128-bit integers. Guard their use with
  `#if defined(COMMONS_HAS_INT128)`. They have no default `operator<<`; in JSON
  they travel as decimal strings to avoid lossy narrowing.
- **`PrioritizedSet` snapshots priority at insert.** Mutating an element's own
  priority afterward does not reorder the set; use `set_priority`. `clear()` does
  not reset the internal insertion-order counter.
- **`PrioritizedSet` `from_json` needs recoverable priority.** Reading a set back
  works only when `T` derives from `Prioritized` or is otherwise
  `Prioritizable`; for a plain `T`, the set is serialize-only.
- **`WithPriority<T>` flavor depends on `T`.** For a non-final class it inherits
  `T` (a true *is-a* `T`); for final classes and fundamentals it composes,
  exposing the value through `value()` / `operator*` / `operator->`. Either way,
  the constructor's first argument is the priority.
- **Flag registration order.** The `GlobalFlagRegistry` is populated at static
  initialization; do not query it before `main`.

Thread safety is not documented. The value types (`FixedString`, `Color`,
`Icon`, `DisplayInfo`) are plain data and safe to read concurrently when not
mutated. `FlagSet`, `PrioritizedSet`, and the `GlobalFlagRegistry` are not
synchronized; treat concurrent mutation as unsafe.

## API overview

| Header                           | Provides                                                                                                                                                                                                                                                       |
|----------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `commons/commons.hpp`            | Umbrella header (all core types + JSON hooks).                                                                                                                                                                                                                 |
| `commons/version.hpp`            | Generated from `version.hpp.in` by the build: `COMMONS_VERSION_MAJOR/MINOR/PATCH/STRING` macros and the `comms::version` / `version_major` / `version_minor` / `version_patch` constants.                                                                      |
| `commons/types.hpp`              | `i8`…`u64`, `f32`/`f64`, `usize`/`isize`, complex aliases (`cs8`…`cs64`, `cu8`…`cu64`, `cf32`/`cf64`), and `i128`/`u128` (gated by `COMMONS_HAS_INT128`).                                                                                                      |
| `commons/fixed_string.hpp`       | `comms::FixedString<N>` — structural, NTTP-friendly fixed string.                                                                                                                                                                                              |
| `commons/color.hpp`              | `comms::Color`, `comms::Hsl`/`comms::Hsv`, and `comms::Colors::css` / `comms::Colors::mui` palettes.                                                                                                                                                           |
| `commons/icon.hpp`               | `comms::Icon` — an Iconify `set:name` identifier; `Icon::from` / `Icon::parse`.                                                                                                                                                                                |
| `commons/icons.hpp`              | Opt-in predefined catalogs: `comms::Icons::mdi::...`. Not pulled by the umbrella.                                                                                                                                                                              |
| `commons/literals.hpp`           | The `comms::literals` user-defined literals: `"#6366f1"_color` and `"mdi:home"_icon` (both `consteval`).                                                                                                                                                       |
| `commons/display_info.hpp`       | `comms::DisplayInfo`, the `comms::HasDisplayInfo<T>` trait, free `comms::display_info<T>()`, and the `comms::Displayable<T>` concept.                                                                                                                          |
| `commons/flag.hpp`               | `comms::Flag`/`FlagCategory`, `FlagRef`, `FlagSet`, `GlobalFlagRegistry`, the `IHasFlags`/`HasFlags`/`FlagBuilderMixin`/`FlagBuilderGetters` mixins, and the `COMMONS_*_FLAG*` macros.                                                                         |
| `commons/origin.hpp`             | `comms::IOrigin`/`OriginPtr`, the `OriginKind<FixedString, Derived>` CRTP base, built-in `Core`/`Internal`/`External`/`Unknown` origins, `GlobalOriginRegistry`, `COMMONS_REGISTER_ORIGIN`.                                                                    |
| `commons/prioritized.hpp`        | `comms::Prioritized`, `get_priority`, the comparators, `PrioritizedSet<T>`, `PrioritizedBuilder<Derived>`, and `WithPriority<T>` / `with_priority` / `make_prioritized`.                                                                                       |
| `commons/semver.hpp`             | `comms::SemVer` — a Semantic Versioning 2.0.0 value; non-throwing `SemVer::parse`, full §11 ordering, `std::hash`.                                                                                                                                             |
| `commons/version_constraint.hpp` | `comms::VersionConstraint` — an npm-style semver range answering `satisfies(SemVer)`; `VersionConstraint::parse` throws on a malformed sub-version.                                                                                                            |
| `commons/id.hpp`                 | `comms::Id<Tag, Repr>` — strong-typed identifier; `Uint{8,16,32,64}Id`/`StringId`/`UlidId` aliases, `to_string`/`display_string`, and the `COMMONS_DEFINE_*_ID` macros.                                                                                        |
| `commons/ability.hpp`            | `comms::IAbility`/`AbilityPtr`, the `AbilityKind<FixedString, Derived>` CRTP base, built-in `Role`/`RecordPermission`/`Generic`/`Unknown` abilities, `GlobalAbilityRegistry`, `make_ability`, `COMMONS_DEFINE_ABILITY`/`COMMONS_REGISTER_ABILITY`.             |
| `commons/identity.hpp`           | `comms::IIdentity`/`IdentityPtr`, the `IdentityKind<FixedString, Derived>` CRTP base, built-in `User`/`Server`/`ApiClient`/`Unknown`/`Root`/`No` identities, `GlobalIdentityRegistry`, `make_identity`, `COMMONS_DEFINE_IDENTITY`/`COMMONS_REGISTER_IDENTITY`. |
| `commons/audit_record.hpp`       | `comms::AuditRecord`, `ChangeAuditRecord<T>`, the capped `AuditLog<Record>` (and the `AuditRecords` / `ChangeAuditRecords<T>` aliases); the `COMMONS_AUDIT_RECORDS_CAPACITY` capacity seam.                                                                    |
| `commons/lifecycle.hpp`          | `comms::LifecycleStatus`, `StatusTransition<T>`, `StatusReport<T>`, the thread-safe `StatusTransitionTimeline<T>` (transitions, temporal queries, reports, subscriptions), and the `LifecycleError` exception.                                                 |
| `commons/metadata.hpp`           | `comms::md::Value`/`Array`/`Object` (and the `comms::Metadata` root alias) — a dynamic value tree with path lookup, deep merge, hashing, and `operator<<`/`std::format`; `MetadataError` family.                                                               |
| `commons/config.hpp`             | The `COMMONS_WITH_*` feature-gate macros.                                                                                                                                                                                                                      |
| `commons/json.hpp`               | Optional nlohmann/json hooks (inert unless the dependency is present). A thin umbrella over per-type modules in `commons/json/<name>.hpp`.                                                                                                                     |

## Examples

Each example is a self-contained program under `examples/`.

| Example                             | Demonstrates                                                                                                                        |
|-------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------|
| `examples/hello.cpp`                | `FixedString`, the numeric aliases, and `version`.                                                                                  |
| `examples/color.cpp`                | Parsing, hex/CSS output, HSL transforms, palettes, WCAG, and the formatter specs.                                                   |
| `examples/icon.cpp`                 | Predefined icons, ad-hoc construction, the `set`/`name` accessors, and text output.                                                 |
| `examples/display_info.cpp`         | Intrusive and non-intrusive `DisplayInfo` attachment and the `Displayable` concept.                                                 |
| `examples/flag.cpp`                 | `FlagSet`, the global registry, and a category-constrained builder read through `IHasFlags`.                                        |
| `examples/origin.cpp`               | `IOrigin` kinds, `clone()`, the `DisplayInfo` description, and registry resolution by kind.                                         |
| `examples/prioritized.cpp`          | The builder mixin, both `WithPriority` flavors, `PrioritizedSet`, and the comparators.                                              |
| `examples/semver.cpp`               | Parsing, full-precedence sorting, `std::format`, and `VersionConstraint` range checks.                                              |
| `examples/id.cpp`                   | The `Id<Tag, Repr>` macros, `display_string`, inherited formatter specs, and the ULID repr.                                         |
| `examples/identity_ability.cpp`     | `required.allowed(subject)` for roles and record permissions, an identity holding abilities, and `Root`/`No` identities.            |
| `examples/audit_record.cpp`         | `AuditRecord` fields and id helpers, `ChangeAuditRecord<T>` before/after, and the capped `AuditLog`.                                |
| `examples/lifecycle.cpp`            | `StatusTransitionTimeline` transitions, status/temporal queries, reports, subscriptions, and a custom (enum) status type.           |
| `examples/metadata/`                | The `comms::md` tree: `basic`, `object_helpers`, `nested`, `merge`, `path_lookup`, `format_output`, and `json_integration` (gated). |
| `examples/json_integration.cpp`     | The optional nlohmann/json round-trips (requires the integration).                                                                  |
| `examples/consumers/fetch_content/` | A standalone downstream project that pulls `commons` via FetchContent.                                                              |

## Testing

The test suite uses GoogleTest. With the bundled `Makefile`:

```bash
make test           # base library: configure + build + run ctest
make integrations   # same, with nlohmann/json and cpp-ulid forced on
make examples       # build and run every example
```

Equivalently, with raw CMake:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The JSON tests (`tests/test_json.cpp`) are compiled only when the integration is
enabled. With Meson:

```bash
meson setup build-meson -Dtests=true -Dexamples=true
meson test -C build-meson
```

Add `-Djson=true` and/or `-Dulid=true` to force the respective integration
under Meson.

## FAQ

**Do I need to link a library?** No. `commons` is header-only; linking
`commons::commons` only adds the include path and the C++23 requirement.

**What happens if I give `Color::parse` or `Icon::parse` bad input?** They return
`std::nullopt`. The `from` factory on `Icon` throws instead, and the `_color`
literal fails to compile.

**Can I use it in multiple threads?** Mostly not synchronized. The plain value
types are safe to read concurrently; the registries and the mutable collections
are not synchronized. The one exception is `StatusTransitionTimeline<T>`, which
guards its state with an internal `std::mutex` (and is therefore non-copyable /
non-movable).

**Does `FixedString` own its characters?** Yes — it stores them inline. `view()`
returns a `std::string_view` into that storage, so do not let the view outlive
the `FixedString`.

**Why won't `comms::Icons::mdi::...` compile?** Add
`#include <commons/icons.hpp>`; the predefined catalogs are intentionally left
out of the umbrella header.

**How do I get the JSON hooks?** Include `<commons/json.hpp>` (or the umbrella,
which includes it) and make sure `<nlohmann/json.hpp>` is reachable. To force the
dependency to be fetched and linked, configure with
`-DCOMMONS_WITH_NLOHMANN_JSON=ON` (CMake) or `-Djson=true` (Meson).

**How do I get the ULID `Id` repr?** `comms::Id<Tag, ulid::Ulid>` (and its
`UlidId<Tag>` / `COMMONS_DEFINE_ULID_ID` shortcuts) light up when `<ulid/ulid.h>`
is on the include path. To force the dependency to be fetched and linked,
configure with `-DCOMMONS_WITH_ULID=ON` (CMake) or `-Dulid=true` (Meson).

## Contributing

Contributions to the library are welcome! If you encounter any issues or have suggestions for
improvements,
please feel free to submit a pull request or open an issue on the project's repository.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

