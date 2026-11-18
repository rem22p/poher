// How strong a poker hand is: the best five cards out of five, six or seven.
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <span>

#include "game/cards.hpp"

namespace poher::game {

enum class Category : std::uint8_t {
    HighCard,
    Pair,
    TwoPair,
    Trips,
    Straight,
    Flush,
    FullHouse,
    Quads,
    StraightFlush,
};

// The strength of a hand. A greater value wins, equal values split the pot.
struct HandValue {
    // The category, then up to five ranks that break ties, most important first.
    std::uint32_t code = 0;

    Category category() const { return static_cast<Category>(code >> 20); }

    // The tie-breaking ranks in order of importance; 0 where there is none.
    // Pair of kings with A-7-4: 13, 14, 7, 4, 0. Straight to the nine: 9, 0, 0, 0, 0.
    std::array<int, 5> ranks() const {
        std::array<int, 5> out;
        for (int i = 0; i < 5; ++i) out[i] = static_cast<int>((code >> (16 - 4 * i)) & 15);
        return out;
    }

    friend auto operator<=>(const HandValue&, const HandValue&) = default;
};

// The best five-card hand among 5 to 7 cards.
HandValue evaluate(std::span<const Card> cards);

}  // namespace poher::game
