#pragma once
#include <cstdint>

// Platform-independent transcendentals.
//
// libm's sin() is NOT specified to be bit-identical across implementations
// (glibc, MSVC CRT and Apple libm all differ in the last bits), which breaks
// cross-platform digest comparison. These use only +, -, *, / and float
// comparison, every one of which IEEE-754 pins down exactly.
//
// std::sqrt is deliberately NOT reimplemented here: IEEE-754 requires it to be
// correctly rounded, so it is already portable.
namespace detmath {

constexpr float PI      = 3.14159265358979323846f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float TWO_PI  = 6.28318530717958647692f;
constexpr float INV_TWO_PI = 0.15915494309189533577f;

// Reduce to [-PI, PI]. Range reduction is done in double to avoid
// cancellation error: at x=500, float subtraction of ~502 - ~502 loses ~3e-5.
// Double arithmetic is IEEE-754 specified like float, and FMA contraction is
// disabled via tactix_fp_flags, so bit-reproducibility is preserved.
inline float wrapPi(float x) {
    constexpr double INV_TWO_PI_D = 0.15915494309189533577;
    constexpr double TWO_PI_D     = 6.28318530717958647692;
    const double xd = static_cast<double>(x);
    const double k  = xd * INV_TWO_PI_D;
    // Round to nearest without llround(): truncation plus a signed half.
    const double rounded = static_cast<double>(
        static_cast<int32_t>(k + (k >= 0.0 ? 0.5 : -0.5)));
    return static_cast<float>(xd - rounded * TWO_PI_D);
}

// Max absolute error ~3.6e-6 (Taylor polynomial truncation near ±π/2, combined
// with double-precision range reduction to avoid cancellation). Actual measured
// worst error across ±500 radians: 3.63615e-06.
//
// ponytail: accuracy degrades for |x| beyond ~1.3e10 as the int32_t cast in
// wrapPi's rounding overflows. Upgrade to Payne-Hanek reduction only if a caller
// ever needs it; the simulation's elapsed-time arguments stay far below that.
inline float sin(float x) {
    x = wrapPi(x);

    // Fold [-PI,-PI/2] and [PI/2,PI] onto [-PI/2,PI/2] using sin(PI-x)=sin(x).
    // Taylor error at PI/2 is ~4e-6; at PI it would be ~7e-3.
    if (x > HALF_PI)       x = PI - x;
    else if (x < -HALF_PI) x = -PI - x;

    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f
              + x2 * ( 0.008333333f
              + x2 * (-0.00019841270f
              + x2 *  0.0000027557319f))));
}

} // namespace detmath
