#pragma once

/// @file
/// @brief Fixed-width numeric aliases shared across the C++ libraries.
///
/// Lowercase, Rust-flavoured spellings (`i32`, `u64`, `f64`, `usize`, …) for
/// the standard fixed-width integer and floating-point types. The 128-bit
/// aliases are only defined when the compiler provides 128-bit integers; the
/// `COMMONS_HAS_INT128` feature macro signals their availability.

#include <complex>
#include <cstddef>
#include <cstdint>

namespace comms {

using i8 = std::int8_t;    ///< Signed 8-bit integer.
using i16 = std::int16_t;  ///< Signed 16-bit integer.
using i32 = std::int32_t;  ///< Signed 32-bit integer.
using i64 = std::int64_t;  ///< Signed 64-bit integer.

using u8 = std::uint8_t;    ///< Unsigned 8-bit integer.
using u16 = std::uint16_t;  ///< Unsigned 16-bit integer.
using u32 = std::uint32_t;  ///< Unsigned 32-bit integer.
using u64 = std::uint64_t;  ///< Unsigned 64-bit integer.

using f32 = float;   ///< 32-bit floating point.
using f64 = double;  ///< 64-bit floating point.

using cs8 = std::complex<i8>;    ///< Complex with signed 8-bit components.
using cs16 = std::complex<i16>;  ///< Complex with signed 16-bit components.
using cs32 = std::complex<i32>;  ///< Complex with signed 32-bit components.
using cs64 = std::complex<i64>;  ///< Complex with signed 64-bit components.

using cu8 = std::complex<u8>;    ///< Complex with unsigned 8-bit components.
using cu16 = std::complex<u16>;  ///< Complex with unsigned 16-bit components.
using cu32 = std::complex<u32>;  ///< Complex with unsigned 32-bit components.
using cu64 = std::complex<u64>;  ///< Complex with unsigned 64-bit components.

using cf32 = std::complex<f32>;  ///< Complex with 32-bit floating-point components.
using cf64 = std::complex<f64>;  ///< Complex with 64-bit floating-point components.

using usize = std::size_t;     ///< Unsigned size type (`std::size_t`).
using isize = std::ptrdiff_t;  ///< Signed size/difference type (`std::ptrdiff_t`).

#if defined(__SIZEOF_INT128__)
/// Defined when the compiler provides 128-bit integer types. Headers that use
/// the 128-bit aliases gate their declarations behind this macro so the
/// library still compiles on platforms that lack 128-bit support.
#define COMMONS_HAS_INT128 1

/// Signed 128-bit integer alias for `__int128_t`.
using i128 = __int128_t;

/// Unsigned 128-bit integer alias for `__uint128_t`.
using u128 = __uint128_t;
#endif

}  // namespace comms
