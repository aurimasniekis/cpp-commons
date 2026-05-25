// Tour of comms::DisplayInfo: presentation metadata attached to a type two
// ways — an intrusive static member, and a non-intrusive HasDisplayInfo<T>
// specialization — retrieved through the free comms::display_info<T>() and
// gated on the Displayable concept. JSON-free, so it builds in the base config.

#include <commons/display_info.hpp>

#include <iostream>
#include <string>

namespace {

// Intrusive: a type that carries its own display metadata.
struct Widget {
    [[maybe_unused]] static const comms::DisplayInfo& display_info() {
        static const comms::DisplayInfo info{
            .name = "Widget",
            .description = "A reusable UI building block",
            .icon = comms::Icon::from("mdi:widgets"),
            .color = comms::Colors::css::indigo,
        };
        return info;
    }
};

// A third-party-style enum we cannot edit — metadata attaches non-intrusively.
enum class Severity { Info, Warning, Error };

// A plain type with no metadata at all.
struct Plain {};

// Print whatever fields a type's DisplayInfo carries.
template <typename T>
void describe(std::string_view label)
    requires comms::Displayable<T>
{
    const comms::DisplayInfo& d = comms::display_info<T>();
    std::cout << label << ":\n";
    std::cout << "  name        : " << d.name.value_or("(none)") << "\n";
    std::cout << "  description : " << d.description.value_or("(none)") << "\n";
    std::cout << "  icon        : " << (d.icon ? d.icon->value() : "(none)") << "\n";
    std::cout << "  color       : " << (d.color ? d.color->to_hex_string() : "(none)") << "\n";
}

}  // namespace

// Non-intrusive: specialize HasDisplayInfo for the enum (namespace comms).
template <>
struct comms::HasDisplayInfo<Severity> {
    [[nodiscard]] static const DisplayInfo& display_info() {
        static const DisplayInfo info{
            .name = "Severity",
            .description = "How bad is it",
            .icon = Icon::from("mdi:alert"),
            .color = Colors::css::orange,
        };
        return info;
    }
};

int main() {
    describe<Widget>("Widget (intrusive member)");
    describe<Severity>("Severity (trait specialization)");

    // The concept reports that Plain has no metadata; calling
    // comms::display_info<Plain>() would be a compile error.
    static_assert(!comms::Displayable<Plain>);
    std::cout << "Plain is Displayable? " << std::boolalpha << comms::Displayable<Plain> << "\n";

    return 0;
}
