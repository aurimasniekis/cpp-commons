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
directly.

## The rule for every public type

**Every public Commons type must ship its serialization hooks under the gates.**
When adding a type, also add, guarded by the matching macro:

- **nlohmann `to_json` / `from_json`** under `COMMONS_WITH_NLOHMANN_JSON`, in
  `commons/json.hpp`. Class types: free ADL functions in namespace `comms`.
  Fundamental types (e.g. the 128-bit aliases): an
  `nlohmann::adl_serializer<T>` specialization — ADL cannot find free functions
  for builtins.

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
- Version `0.1.4` is declared once in CMake `project()` and Meson `project()`.
  `commons/version.hpp` is **generated** from `commons/version.hpp.in` by the
  build (into the build tree, not the source tree): it defines the
  `COMMONS_VERSION_MAJOR/MINOR/PATCH/STRING` macros and the `comms::version` /
  `version_major|minor|patch` constants. The umbrella includes it. Bump the
  version in the two `project()` declarations only.
