// Writes src/bots/preflop_table.inc: how good each of the 169 kinds of
// starting hand is, as the share of all starting hands that are worse.
//
// The measure is the chance to win when all cards are simply dealt out,
// averaged over one opponent and four opponents: against one, high cards
// matter most; against several, suited and connected cards gain. Run it once;
// the result is kept in the source tree.
//
//   build/poher-preflop > src/bots/preflop_table.inc
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#include "game/equity.hpp"

using namespace poher::game;

int main() {
    struct Kind {
        int high;
        int low;
        bool suited;
        double score;
        int combos;
        double worse;
    };
    std::vector<Kind> kinds;
    Random random(2026);
    const int samples = 150000;
    for (int high = 2; high <= kAce; ++high) {
        for (int low = 2; low <= high; ++low) {
            for (const bool suited : {false, true}) {
                if (suited && high == low) continue;
                const std::array<Card, 2> cards = {Card{static_cast<std::uint8_t>(high), Suit::Spades},
                                                   Card{static_cast<std::uint8_t>(low), suited ? Suit::Spades : Suit::Hearts}};
                const double one = estimate_equity(cards, {}, 1, samples, random);
                const double four = estimate_equity(cards, {}, 4, samples, random);
                // Against four the fair share is 0.2, against one 0.5: bring both to the same scale.
                const double score = (one / 0.5 + four / 0.2) / 2;
                kinds.push_back({high, low, suited, score, high == low ? 6 : suited ? 4 : 12, 0});
            }
        }
    }
    std::sort(kinds.begin(), kinds.end(), [](const Kind& a, const Kind& b) { return a.score < b.score; });
    int below = 0;
    for (Kind& kind : kinds) {
        // Half of the hand's own combinations count as worse, so that the best hand is close to 1 and the worst close to 0.
        kind.worse = (below + kind.combos / 2.0) / 1326.0;
        below += kind.combos;
    }

    std::array<std::array<double, 13>, 13> table{};
    for (const Kind& kind : kinds) {
        // Suited hands above the diagonal (row = high card), the rest on and below it.
        const int a = kind.high - 2;
        const int b = kind.low - 2;
        if (kind.suited) {
            table[static_cast<std::size_t>(b)][static_cast<std::size_t>(a)] = kind.worse;
        } else {
            table[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)] = kind.worse;
        }
    }
    std::printf("// Made by tools/preflop_table.cpp; do not edit by hand.\n");
    std::printf("// Row and column are ranks from the two to the ace. [high][low] is an unsuited hand or a pair,\n");
    std::printf("// [low][high] the same two ranks suited. The value is the share of starting hands that are worse.\n");
    for (const auto& row : table) {
        std::printf("{");
        for (std::size_t i = 0; i < row.size(); ++i) std::printf("%s%.3f", i ? ", " : "", row[i]);
        std::printf("},\n");
    }
    return 0;
}
