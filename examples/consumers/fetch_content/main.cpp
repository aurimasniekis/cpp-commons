// Minimal downstream consumer: build a FixedString and a couple of fixed-width
// aliases through the `comms` namespace. If this binary builds and runs, the
// FetchContent integration is wired correctly.

#include <commons/commons.hpp>

#include <iostream>

int main() {
    constexpr comms::FixedString tag{"downstream"};
    constexpr comms::u32 count = 3;

    std::cout << "commons version: " << comms::version << '\n';
    std::cout << "tag            : " << tag.view() << '\n';
    std::cout << "count          : " << count << '\n';

    return 0;
}
