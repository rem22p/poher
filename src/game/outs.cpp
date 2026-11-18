#include "game/outs.hpp"

#include <algorithm>
#include <vector>

namespace poher::game {

namespace {

int kind(HandValue value) {
    return static_cast<int>(value.category());
}

}  // namespace

double Outs::by_the_river(int cards_to_come) const {
    if (unseen <= 0 || count <= 0) return 0;
    const double miss_first = static_cast<double>(unseen - count) / unseen;
    if (cards_to_come <= 1 || unseen < 2) return 1 - miss_first;
    const double miss_second = static_cast<double>(unseen - count - 1) / (unseen - 1);
    return 1 - miss_first * std::max(miss_second, 0.0);
}

Outs count_outs(std::array<Card, 2> cards, std::span<const Card> board) {
    Outs outs;
    if (board.size() != 3 && board.size() != 4) return outs;

    std::vector<Card> all(board.begin(), board.end());
    std::vector<Card> table(board.begin(), board.end());
    all.push_back(cards[0]);
    all.push_back(cards[1]);
    // How far the hand is above what lies on the board for everybody.
    const int now = kind(evaluate(all));
    const int lead = now - kind(evaluate(table));

    all.emplace_back();
    table.emplace_back();
    for (const Card card : fresh_deck()) {
        if (std::find(all.begin(), all.end() - 1, card) != all.end() - 1) continue;
        ++outs.unseen;
        all.back() = card;
        table.back() = card;
        const HandValue value = evaluate(all);
        const int then = kind(value);
        if (then <= now || then - kind(evaluate(table)) <= lead) continue;
        if (value.category() == Category::Pair) {
            // Only the top pair is worth waiting for.
            const int pair = value.ranks()[0];
            const bool top = std::all_of(table.begin(), table.end(), [&](Card c) { return c.rank <= pair; });
            if (!top) continue;
            ++outs.to_pair;
        }
        ++outs.count;
        if (value.category() == Category::Flush || value.category() == Category::StraightFlush) ++outs.to_flush;
        if (value.category() == Category::Straight) ++outs.to_straight;
    }
    outs.flush_draw = outs.to_flush >= 7;
    // Flush cards that also fill the straight were counted as flush outs.
    outs.open_ended = outs.to_straight >= 6;
    outs.gutshot = !outs.open_ended && outs.to_straight >= 3;
    return outs;
}

}  // namespace poher::game
