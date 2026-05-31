// Nested objects and arrays-of-objects: braced pairs build nested Objects, while
// a collection of objects uses an explicit comms::md::Array. Then reach into the
// tree with require_object / require_array. JSON-free, builds in the base config.

#include <commons/metadata.hpp>

#include <iostream>

namespace md = comms::md;

int main() {
    md::Object root;
    root["device"] = {
        {"id", "abc-123"},
        {"firmware", {{"major", 1}, {"minor", 4}}},
    };
    // Mix of objects-in-array — use comms::md::Array for the outer collection of
    // objects (raw braces-of-objects would not disambiguate cleanly).
    root["channels"] = md::Array{
        md::Object{{"name", "left"}, {"gain_db", 0.5}},
        md::Object{{"name", "right"}, {"gain_db", -1.0}},
    };

    std::cout << root << "\n";

    // Reach into the nested object.
    const md::Object& fw = root.require_object("device").require_object("firmware");
    std::cout << "firmware: " << fw.at("major").as_int() << "." << fw.at("minor").as_int() << "\n";

    // Walk the array.
    for (const md::Value& ch : root.require_array("channels")) {
        const md::Object& c = ch.as_object();
        std::cout << c.at("name").as_string() << " gain=" << c.at("gain_db").as_double() << "\n";
    }

    return 0;
}
