// Build a small document with the root-level comms::Metadata alias, mixing
// scalar / array / object values, then stream it as compact JSON and read a few
// fields back with typed accessors. JSON-free, so it builds in the base config.

#include <commons/metadata.hpp>

#include <iostream>

int main() {
    // comms::Metadata is the root-level alias for comms::md::Object.
    comms::Metadata m;
    m["name"] = "sensor-7";
    m["enabled"] = true;
    m["count"] = 42;
    m["weight"] = 3.14;
    m["tags"] = {"alpha", "beta"};  // braced list → Array

    std::cout << m << "\n";

    std::cout << "name=" << m.require_string("name") << "\n";
    std::cout << "count=" << m["count"].as_int() << "\n";
    std::cout << "weight=" << m["weight"].as_double() << "\n";

    return 0;
}
