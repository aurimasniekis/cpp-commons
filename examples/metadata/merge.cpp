// Deep merge semantics: nested objects recurse, scalars overwrite, and arrays
// are replaced wholesale (not concatenated). JSON-free, builds in the base
// config.

#include <commons/metadata.hpp>

#include <iostream>

namespace md = comms::md;

int main() {
    md::Object base{
        {"name", "default"},
        {"options", {{"retries", 3}, {"timeout_ms", 1000}}},
        {"tags", md::Array{"a", "b"}},
    };

    const md::Object overlay{
        {"options", {{"timeout_ms", 500}, {"strict", true}}},
        {"tags", md::Array{"x"}},  // arrays are replaced, not merged
        {"description", "overridden"},
    };

    std::cout << "before:  " << base << "\n";
    base.merge(overlay);
    std::cout << "after :  " << base << "\n";

    return 0;
}
