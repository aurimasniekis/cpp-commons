// Demonstrates the optional nlohmann/json integration: the Commons types pick
// up to_json/from_json hooks once <nlohmann/json.hpp> is available. Build with
// `make integrations` (or -DCOMMONS_WITH_NLOHMANN_JSON=ON).

#include <commons/commons.hpp>
#include <commons/json.hpp>

#include <nlohmann/json.hpp>

#include <iostream>

int main() {
    namespace c = comms;
    using json = nlohmann::json;

    c::FixedString id{"order.created"};
    const json jid = id;
    std::cout << "FixedString json : " << jid.dump() << "\n";
    const auto id_back = jid.get<c::FixedString<14>>();
    std::cout << "round-trip view  : " << id_back.view() << "\n";

#if defined(COMMONS_HAS_INT128)
    // 128-bit integers travel as decimal strings — JSON numbers cannot hold
    // them without loss.
    const auto huge = static_cast<c::u128>(-1);  // UINT128_MAX
    const json jhuge = huge;
    std::cout << "u128 json        : " << jhuge.dump() << "\n";
    std::cout << "round-trip ok    : " << std::boolalpha << (jhuge.get<c::u128>() == huge) << "\n";

    c::i128 neg = -c::i128{170141183460469231};
    const json jneg = neg;
    std::cout << "i128 json        : " << jneg.dump() << "\n";
#endif

    // Complex aliases travel as a [real, imaginary] array.
    c::cf64 signal{0.5, -1.25};
    const json jsig = signal;
    std::cout << "cf64 json        : " << jsig.dump() << "\n";
    const auto sig_back = jsig.get<c::cf64>();
    std::cout << "round-trip ok    : " << std::boolalpha << (sig_back == signal) << "\n";

    return 0;
}
