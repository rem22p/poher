#include "game/hand_value.hpp"

#include <bit>

namespace poher::game {
namespace {

// A set of ranks as bits: bit 2 is a two, bit 14 an ace.
using RankSet = std::uint32_t;

HandValue make(Category category, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0) {
    return HandValue{(static_cast<std::uint32_t>(category) << 20) | static_cast<std::uint32_t>(a << 16) |
                     static_cast<std::uint32_t>(b << 12) | static_cast<std::uint32_t>(c << 8) |
                     static_cast<std::uint32_t>(d << 4) | static_cast<std::uint32_t>(e)};
}

// The top rank of the best straight in the set, or 0. The ace also plays low: A-2-3-4-5.
int straight_top(RankSet ranks) {
    if (ranks & (1u << kAce)) ranks |= 1u << 1;
    for (int top = kAce; top >= 5; --top) {
        const RankSet run = 0b11111u << (top - 4);
        if ((ranks & run) == run) return top;
    }
    return 0;
}

int highest(RankSet ranks) {
    return std::bit_width(ranks) - 1;
}

// Takes the `count` highest ranks out of the set into `out`.
void take_highest(RankSet ranks, int count, int* out) {
    for (int i = 0; i < count; ++i) {
        const int rank = ranks != 0 ? highest(ranks) : 0;
        out[i] = rank;
        ranks &= ~(1u << rank);
    }
}

}  // namespace

HandValue evaluate(std::span<const Card> cards) {
    RankSet by_suit[4] = {};
    int count[kAce + 1] = {};
    RankSet all = 0;
    for (const Card card : cards) {
        const RankSet bit = 1u << card.rank;
        by_suit[static_cast<int>(card.suit)] |= bit;
        all |= bit;
        ++count[card.rank];
    }

    // With at most seven cards only one suit can hold five.
    for (const RankSet suited : by_suit) {
        if (std::popcount(suited) < 5) continue;
        if (const int top = straight_top(suited)) return make(Category::StraightFlush, top);
        // Nothing with a flush possible on board beats it except quads and a full
        // house, and those cannot share seven cards with five of one suit.
        int kickers[5];
        take_highest(suited, 5, kickers);
        return make(Category::Flush, kickers[0], kickers[1], kickers[2], kickers[3], kickers[4]);
    }

    // Ranks that come four, three and two times.
    RankSet quads = 0, trips = 0, pairs = 0, singles = 0;
    for (int rank = 2; rank <= kAce; ++rank) {
        const RankSet bit = 1u << rank;
        if (count[rank] == 4) quads |= bit;
        if (count[rank] == 3) trips |= bit;
        if (count[rank] == 2) pairs |= bit;
        if (count[rank] == 1) singles |= bit;
    }

    if (quads != 0) {
        const int four = highest(quads);
        return make(Category::Quads, four, highest(all & ~(1u << four)));
    }
    if (trips != 0) {
        const int three = highest(trips);
        // The pair of a full house may come from a second set of three.
        const RankSet rest = (trips & ~(1u << three)) | pairs;
        if (rest != 0) return make(Category::FullHouse, three, highest(rest));
    }
    if (const int top = straight_top(all)) return make(Category::Straight, top);
    if (trips != 0) {
        const int three = highest(trips);
        int kickers[2];
        take_highest(all & ~(1u << three), 2, kickers);
        return make(Category::Trips, three, kickers[0], kickers[1]);
    }
    if (std::popcount(pairs) >= 2) {
        int two[2];
        take_highest(pairs, 2, two);
        return make(Category::TwoPair, two[0], two[1], highest(all & ~(1u << two[0]) & ~(1u << two[1])));
    }
    if (pairs != 0) {
        const int pair = highest(pairs);
        int kickers[3];
        take_highest(singles, 3, kickers);
        return make(Category::Pair, pair, kickers[0], kickers[1], kickers[2]);
    }
    int kickers[5];
    take_highest(all, 5, kickers);
    return make(Category::HighCard, kickers[0], kickers[1], kickers[2], kickers[3], kickers[4]);
}

}  // namespace poher::game
