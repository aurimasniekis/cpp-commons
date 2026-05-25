// Tour of comms::Flag: compile-time named flags grouped into categories, a
// runtime FlagSet that keeps insertion order and groups by category, the
// program-wide GlobalFlagRegistry the define macros populate automatically, and
// a FlagBuilderGetters-based builder read polymorphically through IHasFlags.
// JSON-free, so it builds in the base config.

#include <commons/flag.hpp>

#include <iostream>

namespace {

// A category and a handful of flags, defined + auto-registered as one-liners.
COMMONS_FLAG_CATEGORY(NetworkCategory, "network");
COMMONS_DEFINE_FLAG_IN(Ipv6, "ipv6", NetworkCategory);
COMMONS_DEFINE_FLAG_IN(KeepAlive, "keep-alive", NetworkCategory);
COMMONS_DEFINE_FLAG(Verbose, "verbose");  // default UnsetFlagCategory

// A builder that owns a FlagSet restricted to NetworkCategory flags. Deriving
// FlagBuilderGetters gives it the fluent mutators *and* a public, IHasFlags-
// backed read API, so it can be inspected through a const comms::IHasFlags&.
class NetworkConfig : public comms::FlagBuilderGetters<NetworkConfig, NetworkCategory> {};

// Read any flag owner polymorphically through the interface.
void print_owner(std::string_view label, const comms::IHasFlags& owner) {
    std::cout << label << " (size " << owner.flags().size() << "):\n";
    for (const auto& f : owner.flags()) {
        std::cout << "  - " << f.name << "\n";
    }
}

}  // namespace

int main() {
    namespace c = comms;

    // Build a set by flag type; insertion order is preserved.
    c::FlagSet set;
    set.insert<Verbose>();
    set.insert<Ipv6>();
    set.insert<KeepAlive>();
    set.insert<Ipv6>();  // duplicate — ignored

    std::cout << "FlagSet (insertion order), size " << set.size() << ":\n";
    for (const auto& f : set) {
        std::cout << "  - " << f.name << "  [" << f.category << "]\n";
    }

    std::cout << "\nGrouped by category:\n";
    for (const auto& [category, flags] : set.group_by_category()) {
        std::cout << "  " << category << ":\n";
        for (const auto& f : flags) {
            std::cout << "    - " << f.name << "\n";
        }
    }

    // The define macros registered every flag into the global registry.
    std::cout << "\nGlobalFlagRegistry knows " << c::GlobalFlagRegistry::instance().flags().size()
              << " flag(s):\n";
    for (const auto& f : c::GlobalFlagRegistry::instance().flags()) {
        std::cout << "  - " << f.name << "  [" << f.category << "]\n";
    }

    // A builder owns its own FlagSet and offers a fluent API. The typed
    // overloads only accept NetworkCategory flags; chaining returns NetworkConfig&.
    NetworkConfig cfg;
    cfg.flag<Ipv6>().set_flag<KeepAlive>(true);
    // cfg.flag<Verbose>();  // would not compile: Verbose is not NetworkCategory

    // Because NetworkConfig is an IHasFlags, it reads back through the interface.
    std::cout << "\n";
    print_owner("NetworkConfig builder", cfg);

    return 0;
}
