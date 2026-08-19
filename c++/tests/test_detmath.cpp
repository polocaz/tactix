#include <doctest/doctest.h>
#include "DetMath.hpp"
#include <cmath>
#include <cstring>

TEST_CASE("accurate against std::sin across a wide range") {
    float worst = 0.0f;
    for (int i = -100000; i <= 100000; ++i) {
        const float x = static_cast<float>(i) * 0.005f;   // +/- 500 radians
        const float err = std::fabs(detmath::sin(x) - std::sin(static_cast<double>(x)));
        if (err > worst) worst = err;
    }
    CHECK(worst < 1.0e-5f);
}

TEST_CASE("exact at the values that matter") {
    CHECK(std::fabs(detmath::sin(0.0f)) < 1.0e-6f);
    CHECK(std::fabs(detmath::sin(3.14159265f)) < 1.0e-5f);
    CHECK(std::fabs(detmath::sin(1.57079633f) - 1.0f) < 1.0e-5f);
    CHECK(std::fabs(detmath::sin(-1.57079633f) + 1.0f) < 1.0e-5f);
}

TEST_CASE("output is bitwise reproducible") {
    for (int i = 0; i < 10000; ++i) {
        const float x = static_cast<float>(i) * 0.013f;
        uint32_t a, b;
        const float fa = detmath::sin(x);
        const float fb = detmath::sin(x);
        std::memcpy(&a, &fa, 4);
        std::memcpy(&b, &fb, 4);
        CHECK(a == b);
    }
}

TEST_CASE("stays bounded far from the origin") {
    for (int i = 0; i < 1000; ++i) {
        const float x = static_cast<float>(i) * 977.0f;
        const float v = detmath::sin(x);
        CHECK(v >= -1.001f);
        CHECK(v <= 1.001f);
    }
}
