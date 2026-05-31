// Tour of comms::IReason: a polymorphic "why" envelope (code/message/created_at)
// whose `kind` is a compile-time FixedString, with built-in Generic/Unknown
// kinds and their FailureReason counterparts, deep clone(), resolution by kind
// through the GlobalReasonRegistry, and the reason exceptions rooted at
// comms::Exception. JSON-free, so it builds in the base library.

#include <commons/reason.hpp>

#include <format>
#include <iostream>
#include <memory>

// A domain-specific failure reason joins the open set in one line — defined and
// registered by the macro, with the ReasonKind constructors inherited.
COMMONS_DEFINE_FAILURE_REASON(RateLimitReason, "rate_limit");

int main() {
    namespace c = comms;

    // A reason carries a code, a message, and a creation timestamp.
    const c::GenericReason rejected{403, "not allowed"};
    std::cout << "to_string    : " << c::to_string(rejected) << "\n";
    std::cout << "format {}    : " << std::format("{}", rejected) << "\n";

    // Factory helpers default to the generic kinds; pass another explicitly.
    const c::ReasonPtr unknown = c::make_reason<c::UnknownReason>();
    std::cout << "unknown      : " << *unknown << "\n";

    // A FailureReason is a developer-submitted failure. throw_as_exception()
    // packages it for a site with no failure channel; a handler catches it.
    try {
        RateLimitReason{429, "Rate limit exceeded"}.throw_as_exception();
    } catch (const c::FailureReasonException& e) {
        std::cout << "caught       : " << e.what() << " (code " << e.failure_reason()->code
                  << ", kind " << e.failure_reason()->kind() << ")\n";
    }

    // Reject/Cancel exceptions carry any reason; all derive from comms::Exception.
    try {
        throw c::CancelException(c::GenericReason{"user cancelled"});
    } catch (const c::Exception& e) {
        std::cout << "cancelled    : " << e.what() << "\n";
    }

    // The registry can be filtered by family: all kinds, just the failure
    // reasons, or (via kinds_of<Base>) a custom sub-interface.
    const auto& reg = c::GlobalReasonRegistry::instance();
    std::cout << "all kinds    : " << reg.kinds().size() << " registered\n";
    std::cout << "failure kinds: ";
    for (const auto& k : reg.failure_kinds()) {
        std::cout << k << " ";
    }
    std::cout << "\n";

    // The registry turns a runtime `kind` string back into the right type — the
    // open-set hook a JSON `from_json` uses.
    if (const c::ReasonPtr made = reg.create("rate_limit")) {
        std::cout << "create(kind) : " << made->title() << "\n";
    }

    return 0;
}
