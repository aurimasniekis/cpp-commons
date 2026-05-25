// Minimal tour of the base (zero-dependency) Commons types: a compile-time
// FixedString and a few fixed-width numeric aliases.

#include <commons/commons.hpp>

#include <iostream>

int main() {
    namespace c = comms;

    constexpr c::FixedString greeting{"hello, commons"};
    std::cout << "greeting : " << greeting.view() << " (len " << greeting.view().size() << ")\n";

    constexpr c::u32 answer = 42;
    constexpr c::f64 ratio = 1.0 / 3.0;
    std::cout << "answer   : " << answer << "\n";
    std::cout << "ratio    : " << ratio << "\n";

    constexpr c::cf64 signal{0.0, 1.0};  // the imaginary unit, as std::complex<f64>
    std::cout << "signal   : " << signal.real() << " + " << signal.imag() << "i\n";

#if defined(COMMONS_HAS_INT128)
    const c::u128 big = static_cast<c::u128>(answer) << 100;  // way past 64 bits
    // 128-bit ints have no default ostream operator; show the high/low halves.
    std::cout << "u128 hi  : " << static_cast<c::u64>(big >> 64) << "\n";
#endif

    std::cout << "version  : " << c::version << "\n";
    return 0;
}
