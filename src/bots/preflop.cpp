#include "bots/preflop.hpp"

#include <algorithm>

namespace poher::bots {

namespace {

constexpr double kWorse[13][13] = {
#include "bots/preflop_table.inc"
};

}  // namespace

double start_strength(const std::array<game::Card, 2>& cards) {
    const int high = std::max(cards[0].rank, cards[1].rank) - 2;
    const int low = std::min(cards[0].rank, cards[1].rank) - 2;
    const bool suited = cards[0].suit == cards[1].suit && high != low;
    return suited ? kWorse[low][high] : kWorse[high][low];
}

}  // namespace poher::bots
