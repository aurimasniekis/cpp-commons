#pragma once

/// @file
/// @brief `comms::Exception` — the root of the Commons exception hierarchy.
///
/// Every exception type Commons throws derives, directly or transitively, from
/// `comms::Exception`, so a consumer can catch the whole family with a single
/// `catch (const comms::Exception&)`. It is a thin extension of
/// `std::runtime_error` (so it is also catchable as `std::exception` and carries
/// a `what()` message), adding nothing but a distinct type to anchor the
/// hierarchy.
///
/// Concrete subtypes live next to the feature that throws them — e.g. the
/// reason exceptions (`ReasonException` → `FailureReasonException` /
/// `RejectException` / `CancelException`) are declared in `commons/reason.hpp`.

#include <stdexcept>

namespace comms {

/// Base class for all exceptions thrown by Commons. Derive feature-specific
/// exceptions from this (or from a more specific Commons exception) so callers
/// can catch the whole family at once.
class Exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace comms
