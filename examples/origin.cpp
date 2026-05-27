// Tour of comms::IOrigin: a polymorphic provenance envelope whose `kind` is a
// compile-time FixedString, with built-in Core/Internal/External/Unknown kinds,
// a DisplayInfo description, deep clone(), and resolution by kind through the
// GlobalOriginRegistry. JSON-free, so it builds in the base library.

#include <commons/origin.hpp>

#include <format>
#include <iostream>
#include <memory>

int main() {
    namespace c = comms;

    // Each built-in carries a compile-time kind and a DisplayInfo description.
    const c::CoreOrigin core;
    std::cout << "kind/desc    : " << core.kind() << " | " << *core.info().description << "\n";

    // The description is the type's static DisplayInfo (also via Displayable).
    std::cout << "display_info : " << *c::display_info<c::ExternalOrigin>().name << "\n";

    // External carries a typed field; clone() is an independent deep copy.
    c::ExternalOrigin npm{"npm"};
    const c::OriginPtr copy = npm.clone();
    npm.source = "changed";
    std::cout << "clone source : " << dynamic_cast<c::ExternalOrigin&>(*copy).source
              << " (original now '" << npm.source << "')\n";

    // Hold any origin as an OriginPtr; text output emits the kind.
    const c::OriginPtr origin = std::make_unique<c::InternalOrigin>();
    std::cout << "ostream <<   : " << *origin << "\n";
    std::cout << "format {}    : " << std::format("{}", *origin) << "\n";

    // The registry turns a runtime `kind` string back into the right type — the
    // open-set hook a JSON `from_json` uses.
    if (const c::OriginPtr made = c::GlobalOriginRegistry::instance().create("external")) {
        std::cout << "create(kind) : " << made->kind() << "\n";
    }
    std::cout << "create(?)    : "
              << (c::GlobalOriginRegistry::instance().create("nope") ? "found" : "null") << "\n";

    return 0;
}
