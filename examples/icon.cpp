// Tour of comms::Icon: predefined Iconify icons, ad-hoc construction, the
// set/name accessors, and text output — all with the zero-dependency base
// library. The predefined catalogs come from the opt-in <commons/icons.hpp>.

#include <commons/icons.hpp>
#include <commons/literals.hpp>

#include <format>
#include <iostream>

int main() {
    namespace c = comms;
    using c::Icon;

    // A predefined Material Design Icon: `comms::Icons::mdi::abacus`.
    constexpr Icon abacus = c::Icons::mdi::abacus;
    std::cout << "predefined   : " << abacus.value() << "\n";

    // The compile-time `_icon` literal — a malformed value is a compile error.
    using namespace c::literals;
    constexpr auto widgets = "mdi:widgets"_icon;
    std::cout << "literal      : " << widgets.value() << "\n";

    // The same icon built by hand — from the whole value or from the parts.
    std::cout << "from(value)  : " << Icon::from("mdi:abacus").value() << "\n";
    std::cout << "from(s, n)   : " << Icon::from("mdi", "abacus").value() << "\n";

    // Split a value into its set and name.
    constexpr Icon cog = c::Icons::mdi::cog;
    std::cout << "value/set/nm : " << cog.value() << " | " << cog.set() << " | " << cog.name()
              << "\n";

    // A keyword-named icon: the member is `delete_`, the value stays `mdi:delete`.
    std::cout << "keyword icon : " << c::Icons::mdi::delete_.value() << "\n";

    // Text output: ostream insertion and std::format.
    std::cout << "ostream <<   : " << abacus << "\n";
    std::cout << "format {}    : " << std::format("{}", abacus) << "\n";

    return 0;
}
