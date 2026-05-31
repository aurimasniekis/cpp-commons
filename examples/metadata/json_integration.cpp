// nlohmann/json round-trip: serialize an Object to nlohmann::json and read it
// back into a comms::md::Value via the ADL hook. Gated on COMMONS_WITH_NLOHMANN_JSON
// (this example is only built when the integration is enabled).

#include <commons/json.hpp>
#include <commons/metadata.hpp>

#include <nlohmann/json.hpp>

#include <iostream>

namespace md = comms::md;

int main() {
    md::Object m{
        {"name", "sensor"},
        {"enabled", true},
        {"readings", md::Array{1.0, 2.5, 3.75}},
    };

    nlohmann::json j = md::to_json(m);
    std::cout << "to nlohmann::json: " << j.dump() << "\n";

    // Round-trip back into comms::md::Value via the ADL hook.
    auto back = j.get<md::Value>();
    std::cout << "back to metadata:  " << back << "\n";

    return 0;
}
