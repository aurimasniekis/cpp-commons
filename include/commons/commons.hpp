#pragma once

/// @file
/// @brief Umbrella header for Commons — pulls in every public type plus the
///        self-gating optional integrations.
///
/// Including this is always safe: `commons/json.hpp` guards its body behind
/// `COMMONS_WITH_NLOHMANN_JSON` and stays inert when nlohmann/json is absent.

#include <commons/color.hpp>
#include <commons/config.hpp>
#include <commons/display_info.hpp>
#include <commons/fixed_string.hpp>
#include <commons/flag.hpp>
#include <commons/icon.hpp>
#include <commons/literals.hpp>
#include <commons/prioritized.hpp>
#include <commons/semver.hpp>
#include <commons/types.hpp>
#include <commons/version.hpp>
#include <commons/version_constraint.hpp>

// Optional integrations — each is self-gating, so unconditional inclusion is
// safe whether or not the backing dependency is available.
#include <commons/json.hpp>
