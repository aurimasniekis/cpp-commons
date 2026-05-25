// Tour of comms::Color: parsing, hex/CSS output, HSL transforms, palettes, and
// WCAG contrast — all with the zero-dependency base library.

#include <commons/commons.hpp>

#include <format>
#include <iostream>

int main() {
    namespace c = comms;
    using c::Color;

    // Construct from a compile-time hex literal.
    using namespace c::literals;
    constexpr auto indigo = "#6366f1"_color;

    std::cout << "indigo hex   : " << indigo.to_hex_string() << "\n";
    std::cout << "indigo css   : " << indigo.to_css_rgb_string() << "\n";
    std::cout << "rgba int     : 0x" << std::hex << indigo.to_rgba_int() << std::dec << "\n";

    // Parse the various textual forms.
    std::cout << "parse named  : " << Color::parse("rebeccapurple")->to_hex_string() << "\n";
    std::cout << "parse rgb()  : " << Color::parse("rgb(255 0 0)")->to_hex_string() << "\n";
    std::cout << "parse hsl()  : " << Color::parse("hsl(120, 100%, 50%)")->to_hex_string() << "\n";

    // HSL transforms.
    std::cout << "lighter      : " << indigo.lighten(0.15).to_hex_string() << "\n";
    std::cout << "complement   : " << indigo.complement().to_hex_string() << "\n";

    // A triadic palette.
    std::cout << "triadic      :";
    for (const Color t : indigo.triadic()) {
        std::cout << " " << t.to_hex_string();
    }
    std::cout << "\n";

    // Material UI palette: flat aliases or indexed/accent accessors.
    std::cout << "mui red 500  : " << c::Colors::mui::red_500.to_hex_string() << "\n";
    std::cout << "mui red[700] : " << c::Colors::mui::red[700].to_hex_string() << "\n";
    std::cout << "mui blue A200: " << c::Colors::mui::blue.accent(200).to_hex_string() << "\n";

    // WCAG: pick a readable text color and report the contrast ratio.
    const Color text = indigo.readable_text_color();
    std::cout << "readable text: " << text.to_hex_string() << " (contrast "
              << indigo.contrast_ratio(text) << ")\n";

    // Text output: ostream insertion and std::format (with the h/H/r specs).
    std::cout << "ostream <<   : " << indigo << "\n";
    std::cout << "format {:H}  : " << std::format("{:H}", indigo) << "\n";
    std::cout << "format {:r}  : " << std::format("{:r}", indigo.fade(0.5)) << "\n";

    return 0;
}
