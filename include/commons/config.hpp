#pragma once

/// @file
/// @brief Central feature-gate header for Commons' optional integrations.
///
/// Each optional integration is controlled by a `COMMONS_WITH_*` macro that
/// resolves to `1` (enabled) or `0` (disabled). A macro predefined by the
/// build system (CMake/Meson) or the consumer always wins; otherwise it is
/// auto-detected via `__has_include`, so the base library carries no forced
/// dependency.
///
/// Supported gates:
///   - `COMMONS_WITH_NLOHMANN_JSON` — nlohmann/json `to_json`/`from_json`
///     hooks, enabled when `<nlohmann/json.hpp>` is on the include path.
///   - `COMMONS_WITH_ULID` — the `ulid::Ulid` representation for `comms::Id`,
///     enabled when `<ulid/ulid.h>` is on the include path.

// nlohmann/json integration -------------------------------------------------
#if !defined(COMMONS_WITH_NLOHMANN_JSON)
#if defined(__has_include)
#if __has_include(<nlohmann/json.hpp>)
#define COMMONS_WITH_NLOHMANN_JSON 1
#else
#define COMMONS_WITH_NLOHMANN_JSON 0
#endif
#else
#define COMMONS_WITH_NLOHMANN_JSON 0
#endif
#endif

// ulid integration ----------------------------------------------------------
#if !defined(COMMONS_WITH_ULID)
#if defined(__has_include)
#if __has_include(<ulid/ulid.h>)
#define COMMONS_WITH_ULID 1
#else
#define COMMONS_WITH_ULID 0
#endif
#else
#define COMMONS_WITH_ULID 0
#endif
#endif
