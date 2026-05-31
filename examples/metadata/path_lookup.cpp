// Dotted/bracketed path traversal: require_path throws on a miss, find_path
// returns nullptr, and contains_path answers a boolean. JSON-free, builds in the
// base config.

#include <commons/metadata.hpp>

#include <iostream>

namespace md = comms::md;

int main() {
    md::Object m{
        {"device", {{"name", "acme"}, {"port", 8080}}},
        {"channels",
         md::Array{
             md::Object{{"freq_hz", 2.4e9}},
             md::Object{{"freq_hz", 5.8e9}},
         }},
    };

    std::cout << "device.name = " << m.require_path("device.name").as_string() << "\n";
    std::cout << "device.port = " << m.require_path("device.port").as_int() << "\n";
    std::cout << "channels[0].freq_hz = " << m.require_path("channels[0].freq_hz").as_double()
              << "\n";
    std::cout << "channels[1].freq_hz = " << m.require_path("channels[1].freq_hz").as_double()
              << "\n";

    if (const md::Value* p = m.find_path("device.absent")) {
        std::cout << "found: " << *p << "\n";
    } else {
        std::cout << "device.absent: missing (expected)\n";
    }

    std::cout << "contains_path(channels[5]): " << std::boolalpha << m.contains_path("channels[5]")
              << "\n";

    return 0;
}
