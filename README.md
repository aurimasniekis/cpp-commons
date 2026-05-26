# Commons

[![CI](https://github.com/aurimasniekis/cpp-commons/actions/workflows/ci.yml/badge.svg)](https://github.com/aurimasniekis/cpp-commons/actions/workflows/ci.yml)
[![Docs](https://github.com/aurimasniekis/cpp-commons/actions/workflows/docs.yml/badge.svg)](https://aurimasniekis.github.io/cpp-commons/)

A header-only C++23 library of small, shared building-block types — a
compile-time fixed-size string, Rust-flavoured fixed-width numeric aliases, an
RGBA `Color` with full HSL/HSV manipulation and CSS/Material-UI palettes, an
Iconify `Icon` identifier, presentation metadata (`DisplayInfo`), a compile-time
named-`Flag` system, and a Spring-style `Prioritized` ordering toolkit. Every
type carries optional nlohmann/json serialization that turns on by itself when
the dependency is available. The namespace is `comms`; headers live under
`<commons/...>`.

## Why use this library?

- **Good for** sharing one definition of common vocabulary types across several
  projects instead of re-implementing them per repository.
- **Good for** UI-adjacent backend code: colors, icons, and display metadata
  that need to round-trip to JSON for a frontend.
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
    URL      https://github.com/aurimasniekis/cpp-commons/archive/refs/tags/v0.1.2.tar.gz
    URL_HASH SHA256=1894f675102f12a51cec5b657129efa8d6651d462c2fd4f9c96f4027338eb00c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(commons)

target_link_libraries(example PRIVATE commons::commons)
```

By default no optional dependency is fetched: the JSON hooks auto-detect
nlohmann/json. To *force* the integration on — which additionally fetches
nlohmann/json (version 3.12.0 or newer) and hard-defines the gate macro —
configure with `-DCOMMONS_WITH_NLOHMANN_JSON=ON`.

### CMake — installed package

After `cmake --install`, the package is consumable via `find_package`:

```cmake
find_package(commons 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE commons::commons)
```

Install rules are automatically disabled when nlohmann/json was fetched (a
fetched dependency cannot be re-exported). For a clean install, leave
`COMMONS_WITH_NLOHMANN_JSON=OFF` (the default) or provide nlohmann/json through a
system package.

### Meson

```meson
commons_dep = dependency('commons', version: '>=0.1.0',
    fallback: ['commons', 'commons_dep'])
```

Meson options mirror the CMake ones: `-Djson=true|false`, `-Dtests=true|false`,
`-Dexamples=true|false`. A `pkg-config` file is generated on install.

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
- **Optional dependency:** [nlohmann/json](https://github.com/nlohmann/json)
  3.12.0 or newer, which enables `<commons/json.hpp>`.

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

### `comms::Prioritized`

Attaches integer priorities to orderable things and sorts them deterministically,
mirroring Spring's `Ordered`: **lower value sorts first** (higher precedence).
`HIGHEST_PRECEDENCE` is `INT_MIN`, `LOWEST_PRECEDENCE` is `INT_MAX`, and the
neutral `DEFAULT_PRIORITY` is `0`.

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
array of names; `i128`/`u128` ⇄ decimal strings; the complex aliases ⇄
`[real, imaginary]` arrays; `WithPriority<T>` ⇄ `{"priority":N,"value":<T>}` and
`PrioritizedSet<T>` ⇄ a sorted array — both only when `T` is itself
JSON-serializable.

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

| Header                     | Provides                                                                                                                                                                                  |
|----------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `commons/commons.hpp`      | Umbrella header (all core types + JSON hooks).                                                                                                                                            |
| `commons/version.hpp`      | Generated from `version.hpp.in` by the build: `COMMONS_VERSION_MAJOR/MINOR/PATCH/STRING` macros and the `comms::version` / `version_major` / `version_minor` / `version_patch` constants. |
| `commons/types.hpp`        | `i8`…`u64`, `f32`/`f64`, `usize`/`isize`, complex aliases (`cs8`…`cs64`, `cu8`…`cu64`, `cf32`/`cf64`), and `i128`/`u128` (gated by `COMMONS_HAS_INT128`).                                 |
| `commons/fixed_string.hpp` | `comms::FixedString<N>` — structural, NTTP-friendly fixed string.                                                                                                                         |
| `commons/color.hpp`        | `comms::Color`, `comms::Hsl`/`comms::Hsv`, and `comms::Colors::css` / `comms::Colors::mui` palettes.                                                                                      |
| `commons/icon.hpp`         | `comms::Icon` — an Iconify `set:name` identifier; `Icon::from` / `Icon::parse`.                                                                                                           |
| `commons/icons.hpp`        | Opt-in predefined catalogs: `comms::Icons::mdi::...`. Not pulled by the umbrella.                                                                                                         |
| `commons/literals.hpp`     | The `comms::literals` user-defined literals: `"#6366f1"_color` and `"mdi:home"_icon` (both `consteval`).                                                                                  |
| `commons/display_info.hpp` | `comms::DisplayInfo`, the `comms::HasDisplayInfo<T>` trait, free `comms::display_info<T>()`, and the `comms::Displayable<T>` concept.                                                     |
| `commons/flag.hpp`         | `comms::Flag`/`FlagCategory`, `FlagRef`, `FlagSet`, `GlobalFlagRegistry`, the `IHasFlags`/`HasFlags`/`FlagBuilderMixin`/`FlagBuilderGetters` mixins, and the `COMMONS_*_FLAG*` macros.    |
| `commons/prioritized.hpp`  | `comms::Prioritized`, `get_priority`, the comparators, `PrioritizedSet<T>`, `PrioritizedBuilder<Derived>`, and `WithPriority<T>` / `with_priority` / `make_prioritized`.                  |
| `commons/config.hpp`       | The `COMMONS_WITH_*` feature-gate macros.                                                                                                                                                 |
| `commons/json.hpp`         | Optional nlohmann/json hooks (inert unless the dependency is present).                                                                                                                    |

## Examples

Each example is a self-contained program under `examples/`.

| Example                             | Demonstrates                                                                                 |
|-------------------------------------|----------------------------------------------------------------------------------------------|
| `examples/hello.cpp`                | `FixedString`, the numeric aliases, and `version`.                                           |
| `examples/color.cpp`                | Parsing, hex/CSS output, HSL transforms, palettes, WCAG, and the formatter specs.            |
| `examples/icon.cpp`                 | Predefined icons, ad-hoc construction, the `set`/`name` accessors, and text output.          |
| `examples/display_info.cpp`         | Intrusive and non-intrusive `DisplayInfo` attachment and the `Displayable` concept.          |
| `examples/flag.cpp`                 | `FlagSet`, the global registry, and a category-constrained builder read through `IHasFlags`. |
| `examples/prioritized.cpp`          | The builder mixin, both `WithPriority` flavors, `PrioritizedSet`, and the comparators.       |
| `examples/json_integration.cpp`     | The optional nlohmann/json round-trips (requires the integration).                           |
| `examples/consumers/fetch_content/` | A standalone downstream project that pulls `commons` via FetchContent.                       |

## Testing

The test suite uses GoogleTest. With the bundled `Makefile`:

```bash
make test           # base library: configure + build + run ctest
make integrations   # same, with nlohmann/json forced on (runs the JSON tests)
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

Add `-Djson=true` to force the JSON integration under Meson.

## FAQ

**Do I need to link a library?** No. `commons` is header-only; linking
`commons::commons` only adds the include path and the C++23 requirement.

**What happens if I give `Color::parse` or `Icon::parse` bad input?** They return
`std::nullopt`. The `from` factory on `Icon` throws instead, and the `_color`
literal fails to compile.

**Can I use it in multiple threads?** Thread safety is not documented. The plain
value types are safe to read concurrently; the registries and the mutable
collections are not synchronized.

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

## Contributing

Contributions to the library are welcome! If you encounter any issues or have suggestions for
improvements,
please feel free to submit a pull request or open an issue on the project's repository.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

