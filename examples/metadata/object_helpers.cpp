// The Object lookup surface: method and free-function `contains`, throwing
// `require*` accessors, and the `get_*_if` pointer accessors that return nullptr
// on a miss or type mismatch. JSON-free, so it builds in the base config.

#include <commons/metadata.hpp>

#include <iostream>
#include <string>

namespace md = comms::md;

int main() {
    md::Object o{{"name", "widget"}, {"qty", 7}};

    // Method form
    std::cout << "contains(name): " << std::boolalpha << o.contains("name") << "\n";
    std::cout << "require_string(name): " << o.require_string("name") << "\n";

    // Free-function form
    std::cout << "comms::md::contains(qty): " << md::contains(o, "qty") << "\n";

    // Optional access — get_*_if returns nullptr on miss/type mismatch.
    if (const std::string* s = o.get_string_if("name")) {
        std::cout << "name (optional): " << *s << "\n";
    }
    if (const std::string* s = o.get_string_if("qty")) {
        std::cout << "qty as string: " << *s << "\n";
    } else {
        std::cout << "qty is not a string (good)\n";
    }

    try {
        (void)o.require("missing");
    } catch (const md::MissingKeyError& e) {
        std::cout << "expected MissingKeyError: " << e.what() << "\n";
    }

    return 0;
}
