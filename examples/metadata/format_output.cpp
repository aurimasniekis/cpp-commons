// Compact-JSON output two ways: via std::format (the std::formatter
// specialization) and via operator<<. Both are hand-rolled, so this is JSON-free
// and builds in the base config.

#include <commons/metadata.hpp>

#include <format>
#include <iostream>

namespace md = comms::md;

int main() {
    md::Value v = md::Object{
        {"name", "radio"},
        {"power_dbm", -10.5},
        {"channels", md::Array{1, 2, 3}},
    };

    std::cout << "via std::format: " << std::format("{}", v) << "\n";
    std::cout << "via operator<<:  " << v << "\n";

    return 0;
}
