#pragma once
#include <cstdint>
#include <cstring>

// FNV-1a over raw IEEE-754 bits. Bitwise on purpose: a tolerance-based
// comparison would silently accept the scheduling bugs this exists to catch.
//
// Note: -0.0f and +0.0f hash differently, and a NaN appearing anywhere will
// almost certainly diverge. Both are desirable — either one signals a bug.
class StateDigest {
public:
    void mix(uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            h ^= static_cast<uint8_t>(v >> (i * 8));
            h *= 1099511628211ull;
        }
    }

    void mix(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        mix(bits);
    }

    uint64_t value() const { return h; }

private:
    uint64_t h = 14695981039346656037ull;
};
