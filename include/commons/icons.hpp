#pragma once

/// @file
/// @brief Opt-in catalog of predefined Iconify icons.
///
/// This header is **not** pulled by the umbrella `commons/commons.hpp`: the
/// generated icon tables are large (the MDI set alone is 7,447 entries), so only
/// translation units that ask for them pay the parse cost. Include it to get
/// `comms::Icons::mdi::abacus` and friends; the core `Icon` type stays in
/// `commons/icon.hpp`.

#include <commons/icon.hpp>
#include <commons/icons/mdi.hpp>

namespace comms {

/// Top-level collection of predefined Iconify sets: currently the Material
/// Design Icons (`comms::Icons::mdi::abacus`). Kept as a nesting point for
/// future sets.
struct Icons {
    using mdi = MdiSet;
};

}  // namespace comms
