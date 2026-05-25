#include <commons/types.hpp>

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <type_traits>

namespace {

static_assert(sizeof(comms::i8) == 1);
static_assert(sizeof(comms::i16) == 2);
static_assert(sizeof(comms::i32) == 4);
static_assert(sizeof(comms::i64) == 8);

static_assert(sizeof(comms::u8) == 1);
static_assert(sizeof(comms::u16) == 2);
static_assert(sizeof(comms::u32) == 4);
static_assert(sizeof(comms::u64) == 8);

static_assert(std::is_signed_v<comms::i32>);
static_assert(std::is_unsigned_v<comms::u32>);

static_assert(std::is_same_v<comms::i32, std::int32_t>);
static_assert(std::is_same_v<comms::u64, std::uint64_t>);

static_assert(sizeof(comms::f32) == 4);
static_assert(sizeof(comms::f64) == 8);
static_assert(std::is_floating_point_v<comms::f64>);

static_assert(std::is_same_v<comms::usize, std::size_t>);
static_assert(std::is_same_v<comms::isize, std::ptrdiff_t>);

// Complex aliases wrap std::complex of the matching component type.
static_assert(std::is_same_v<comms::cs8, std::complex<comms::i8>>);
static_assert(std::is_same_v<comms::cs16, std::complex<comms::i16>>);
static_assert(std::is_same_v<comms::cs32, std::complex<comms::i32>>);
static_assert(std::is_same_v<comms::cs64, std::complex<comms::i64>>);
static_assert(std::is_same_v<comms::cu8, std::complex<comms::u8>>);
static_assert(std::is_same_v<comms::cu16, std::complex<comms::u16>>);
static_assert(std::is_same_v<comms::cu32, std::complex<comms::u32>>);
static_assert(std::is_same_v<comms::cu64, std::complex<comms::u64>>);
static_assert(std::is_same_v<comms::cf32, std::complex<comms::f32>>);
static_assert(std::is_same_v<comms::cf64, std::complex<comms::f64>>);
static_assert(std::is_same_v<comms::cf64::value_type, comms::f64>);
static_assert(sizeof(comms::cf64) == 2 * sizeof(comms::f64));
static_assert(sizeof(comms::cs32) == 2 * sizeof(comms::i32));

#if defined(COMMONS_HAS_INT128)
static_assert(sizeof(comms::i128) == 16);
static_assert(sizeof(comms::u128) == 16);
// `__int128` is an extended integer type: libstdc++ only specializes the
// std::is_signed / is_unsigned traits for it outside strict mode (`-std=gnu++`),
// so those traits are unreliable under `-std=c++23`. Check signedness by value
// instead — a signed type keeps -1 negative, an unsigned one wraps it to its max.
static_assert(static_cast<comms::i128>(-1) < static_cast<comms::i128>(0));
static_assert(static_cast<comms::u128>(-1) > static_cast<comms::u128>(0));
#endif

// A non-empty translation unit keeps the test runner happy even though all the
// real assertions above run at compile time.
TEST(Types, AliasWidthsHold) {
    SUCCEED();
}

TEST(Types, ComplexAliasesCarryComponents) {
    constexpr comms::cf64 z{1.5, -2.5};
    EXPECT_DOUBLE_EQ(z.real(), 1.5);
    EXPECT_DOUBLE_EQ(z.imag(), -2.5);

    constexpr comms::cs32 n{3, 4};
    EXPECT_EQ(n.real(), 3);
    EXPECT_EQ(n.imag(), 4);
}

#if defined(COMMONS_HAS_INT128)
TEST(Types, Int128IsAvailable) {
    const auto max = static_cast<comms::u128>(-1);
    EXPECT_GT(max, static_cast<comms::u128>(static_cast<comms::u64>(-1)));
}
#endif

}  // namespace
