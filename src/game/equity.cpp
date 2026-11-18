#include "game/equity.hpp"

#include <algorithm>
#include <utility>

#include "game/hand_value.hpp"

namespace poher::game {

double estimate_equity(std::array<Card, 2> cards, std::span<const Card> board, int opponents, int samples,
                       Random& random) {
    // The cards nobody has seen yet.
    std::array<Card, kDeckSize> unseen;
    std::size_t unseen_count = 0;
    for (const Card card : fresh_deck()) {
        const bool known = card == cards[0] || card == cards[1] ||
                           std::find(board.begin(), board.end(), card) != board.end();
        if (!known) unseen[unseen_count++] = card;
    }

    const std::size_t board_missing = 5 - board.size();
    const std::size_t needed = board_missing + 2 * static_cast<std::size_t>(opponents);

    std::array<Card, 7> ours;
    std::array<Card, 7> theirs;
    std::copy(board.begin(), board.end(), ours.begin());
    std::copy(board.begin(), board.end(), theirs.begin());

    double share = 0;
    for (int sample = 0; sample < samples; ++sample) {
        // Shuffle only as many cards to the front as this deal needs.
        for (std::size_t i = 0; i < needed; ++i) {
            const std::size_t other = i + random.below(static_cast<std::uint32_t>(unseen_count - i));
            std::swap(unseen[i], unseen[other]);
        }
        for (std::size_t i = 0; i < board_missing; ++i) {
            ours[board.size() + i] = unseen[i];
            theirs[board.size() + i] = unseen[i];
        }
        ours[5] = cards[0];
        ours[6] = cards[1];
        const HandValue mine = evaluate(ours);

        int tied = 0;
        bool beaten = false;
        for (int opponent = 0; opponent < opponents && !beaten; ++opponent) {
            theirs[5] = unseen[board_missing + 2 * static_cast<std::size_t>(opponent)];
            theirs[6] = unseen[board_missing + 2 * static_cast<std::size_t>(opponent) + 1];
            const HandValue other = evaluate(theirs);
            if (other > mine) beaten = true;
            if (other == mine) ++tied;
        }
        if (!beaten) share += 1.0 / (tied + 1);
    }
    return samples > 0 ? share / samples : 0;
}

}  // namespace poher::game
