#pragma once

/// @file
/// @brief Optional nlohmann/json integration for the Commons types — umbrella.
///
/// This header is a thin umbrella over the per-type modules in `commons/json/`.
/// Each module self-gates behind `COMMONS_WITH_NLOHMANN_JSON` (see
/// `commons/config.hpp`): define it, or simply have `<nlohmann/json.hpp>` on the
/// include path, and these hooks light up. The header is otherwise inert.
///
/// A new Commons type adds its hooks in its own `commons/json/<name>.hpp` module
/// (which includes the type header it serializes plus its own dependency
/// sub-headers, so it compiles standalone) and is then `#include`d here.
///
/// What it adds:
///   - `FixedString<N>` ⇄ JSON string. A string that does not fit the
///     fixed `N` capacity throws on parse.
///   - `Color` ⇄ JSON **hex string** (`#RRGGBB`, or `#RRGGBBAA` when not
///     opaque). A string that does not parse throws.
///   - `Hsl` / `Hsv` ⇄ JSON objects (`{"h","s","l","a"}` / `{"h","s","v","a"}`).
///   - `Icon` ⇄ JSON **`set:name` string** (e.g. `"mdi:abacus"`). A string that
///     does not parse throws.
///   - `DisplayInfo` ⇄ JSON **object** with `name`/`description`/`icon`/`color`
///     keys; absent (`std::nullopt`) fields are omitted, and `icon`/`color`
///     reuse the `Icon`/`Color` mappings above.
///   - `FlagRef` ⇄ JSON **string** (the flag name); `FlagSet` ⇄ JSON **array**
///     of names. Reading them back resolves each name against the
///     `GlobalFlagRegistry`; an unknown name throws.
///   - `i128` / `u128` ⇄ JSON **decimal string**. Plain JSON numbers cannot
///     represent 128-bit integers without loss, so they travel as strings —
///     a genuinely useful integration rather than a silent narrowing.
///   - `std::complex<T>` (the `cs8`…`cf64` aliases) ⇄ a two-element JSON
///     `[real, imaginary]` array.
///   - `std::optional<T>` ⇄ the inner `T`'s JSON, with `nullopt` ⇄ JSON `null`.
///   - `WithPriority<T>` ⇄ JSON **object** `{"priority":N,"value":<T>}` and
///     `PrioritizedSet<T>` ⇄ JSON **array** in sorted order — both only when
///     `T` is itself json-serializable; the set's `from_json` additionally
///     requires `T`'s priority to be recoverable from the element.
///   - `SemVer` ⇄ JSON **version string**; `VersionConstraint` ⇄ JSON **range
///     string**.
///   - `Id<Tag, Repr>` ⇄ the inner `Repr`'s natural JSON (a number for the
///     uint reprs, a string for `std::string`, the ULID string when ULID is
///     also enabled).
///   - `IOrigin` / `OriginPtr` ⇄ JSON **object** `{"kind", ...fields}`;
///     `from_json` resolves `kind` against the `GlobalOriginRegistry`.
///   - `ReasonPtr` / `FailureReasonPtr` ⇄ JSON **object**
///     `{"kind","code","message","created_at"}` (the timestamp as epoch
///     milliseconds, plus a `"metadata"` object when that bag is non-empty);
///     `from_json` resolves `kind` against the
///     `GlobalReasonRegistry`, and the failure variant additionally requires the
///     resolved kind to be an `IFailureReason`. An unknown kind throws. The
///     per-field work is the virtual `IReason::write_json`/`read_json` hooks in
///     `commons/reason.hpp` (gated there too), so a sub-reason's extra fields
///     round-trip by overriding them — that module only wires up the pointers.
///   - `comms::md::Value` / `Object` / `Array` ⇄ their natural JSON shapes
///     (null/bool/number/string/array/object), recursively.
///
/// The fixed-width builtin aliases (`i8`…`u64`, `f32`, `f64`, `usize`,
/// `isize`) need nothing here: nlohmann already serializes the underlying
/// arithmetic types natively.

#include <commons/json/color.hpp>
#include <commons/json/display_info.hpp>
#include <commons/json/fixed_string.hpp>
#include <commons/json/flag.hpp>
#include <commons/json/icon.hpp>
#include <commons/json/id.hpp>
#include <commons/json/metadata.hpp>
#include <commons/json/optional.hpp>
#include <commons/json/origin.hpp>
#include <commons/json/prioritized.hpp>
#include <commons/json/reason.hpp>
#include <commons/json/semver.hpp>
#include <commons/json/types.hpp>
#include <commons/json/version_constraint.hpp>
