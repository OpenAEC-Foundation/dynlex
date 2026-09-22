#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>

// Test reference for DynLex's documented random-number algorithms. C++ standard
// distributions do not specify one sequence across standard-library vendors.
namespace random_reference {

// Consume enough base-2^w engine digits for the floating-point significand,
// least-significant digit first, then normalize to [0, 1). Rounding to 1 is
// clamped to the immediately preceding representable value.
template <class Real, class Engine> Real unit_fraction(Engine &engine) {
    static_assert(Engine::min() == 0);
    constexpr int width = std::bit_width(Engine::max());
    constexpr int draws = (std::numeric_limits<Real>::digits + width - 1) / width;
    const Real radix = std::ldexp(Real(1), width);
    Real sum = 0;
    Real scale = 1;
    for (int draw = 0; draw < draws; ++draw) {
        sum += static_cast<Real>(engine()) * scale;
        scale *= radix;
    }
    return std::min(sum / scale, std::nextafter(Real(1), Real(0)));
}

// Marsaglia polar sampling in single precision. Return the vertical component
// first and cache the horizontal component. Mean/deviation do not affect cache.
class NormalDistribution {
    std::optional<float> cached;

  public:
    void reset() { cached.reset(); }

    template <class Engine> float operator()(Engine &engine, float mean = 0, float deviation = 1) {
        if (cached) {
            const float value = *cached;
            cached.reset();
            return value * deviation + mean;
        }
        for (;;) {
            const float x = 2 * unit_fraction<float>(engine) - 1;
            const float y = 2 * unit_fraction<float>(engine) - 1;
            const float radius = x * x + y * y;
            if (radius > 0 && radius <= 1) {
                const float scale = std::sqrt(-2 * std::log(radius) / radius);
                cached = x * scale;
                return y * scale * deviation + mean;
            }
        }
    }
};

} // namespace random_reference
