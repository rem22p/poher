// A small, fast random number generator for bots and simulations. The same
// seed always gives the same numbers on every computer, which keeps tests
// repeatable. It is not meant for dealing cards between people over the
// network: that deck comes from secrets of all players.
#pragma once

#include <cstdint>

namespace poher::game {

class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}

    // SplitMix64: passes the usual statistical tests and needs one word of state.
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // A number from 0 to bound-1, each equally likely.
    std::uint32_t below(std::uint32_t bound) {
        // Throw away the few values that would make the low numbers more frequent.
        const std::uint64_t limit = (std::uint64_t{1} << 32) / bound * bound;
        std::uint64_t value;
        do {
            value = next() >> 32;
        } while (value >= limit);
        return static_cast<std::uint32_t>(value % bound);
    }

    // A number from 0 up to, but not including, 1.
    double unit() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

    // True with the given probability.
    bool chance(double probability) { return unit() < probability; }

private:
    std::uint64_t state_;
};

}  // namespace poher::game
