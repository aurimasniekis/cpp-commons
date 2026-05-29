// Tour of comms::Id: a strong-typed identifier that wraps a narrow set of
// representations (uint, std::string, and — under COMMONS_WITH_ULID — ulid::Ulid)
// in a phantom Tag so that ids of different kinds cannot be mixed. JSON-free,
// so it builds in the base config (the ULID section self-gates on the macro).

#include <commons/config.hpp>
#include <commons/id.hpp>

#include <format>
#include <iostream>
#include <string>

#if COMMONS_WITH_ULID
#include <ulid/ulid.h>
#endif

namespace c = comms;

// A bare tag (no `name`) — display_string falls back to the bare representation.
struct DeviceTag {};
using DeviceId = c::Uint32Id<DeviceTag>;

// Named tags generated through the macros — display_string prefixes with the
// tag's `name`. The macros declare static data members, so they have to be
// invoked at namespace (or class) scope, not inside a function.
COMMONS_DEFINE_UINT64_ID(UserId, "user");
COMMONS_DEFINE_STRING_ID(OrderId, "order");

#if COMMONS_WITH_ULID
COMMONS_DEFINE_ULID_ID(EventId, "event");
#endif

int main() {
    const DeviceId device{42u};
    const UserId user{1234567u};
    const OrderId order{std::string{"o-abc-1"}};

    // to_string + display_string. The bare tag falls back to just the value;
    // the named tags prefix the value with their `name`.
    std::cout << "device     : " << c::to_string(device) << "\n";
    std::cout << "  display  : " << c::display_string(device) << "\n";
    std::cout << "user       : " << c::to_string(user) << "\n";
    std::cout << "  display  : " << c::display_string(user) << "\n";
    std::cout << "order      : " << c::to_string(order) << "\n";
    std::cout << "  display  : " << c::display_string(order) << "\n";

    // std::format inherits the underlying Repr's formatter, so any spec that
    // the wrapped type accepts works transparently.
    std::cout << "\nformat specs (inherited from underlying Repr):\n";
    std::cout << "  user as decimal : " << std::format("{}", user) << "\n";
    std::cout << "  user as hex     : " << std::format("{:#x}", user) << "\n";
    std::cout << "  device 6-wide   : " << std::format("{:6}", device) << "\n";

    // Strong typing: a DeviceId is not a UserId, even though both wrap a uint.
    static_assert(!std::is_same_v<DeviceId, UserId>);

#if COMMONS_WITH_ULID
    if (const auto parsed = ::ulid::Ulid::from_string("01H8XGQZ8E4Q7M5GZP9X8R3D7K")) {
        const EventId ev{*parsed};
        std::cout << "\nulid event : " << c::to_string(ev) << "\n";
        std::cout << "  display  : " << c::display_string(ev) << "\n";
        std::cout << "  format   : " << std::format("{}", ev) << "\n";
    }
#endif

    return 0;
}
