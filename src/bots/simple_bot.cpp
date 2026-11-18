#include "bots/simple_bot.hpp"

#include <algorithm>
#include <cmath>

#include "game/equity.hpp"

namespace poher::bots {

using game::Action;
using game::Chips;
using game::Move;

namespace {

constexpr int kSamples = 400;

// A bet or raise of the given part of the pot, kept within what the rules allow.
Action bet_of(const Situation& s, double part) {
    const Chips matched = s.bet + s.choices.to_call;  // what we would have in after just calling
    const Chips pot_after_call = s.pot + s.choices.to_call;
    const Chips to = matched + static_cast<Chips>(std::llround(part * static_cast<double>(pot_after_call)));
    return Action{Move::Raise, std::clamp(to, s.choices.min_raise_to, s.choices.max_raise_to)};
}

}  // namespace

Situation situation_of(const game::Hand& hand) {
    Situation s;
    const auto& players = hand.players();
    const auto& me = players[static_cast<std::size_t>(hand.to_act())];
    s.cards = me.cards;
    s.bet = me.bet;
    s.board = hand.board();
    s.pot = hand.pot();
    s.big_blind = hand.big_blind();
    s.choices = hand.choices();
    s.opponents = -1;
    for (const game::HandPlayer& player : players) {
        if (!player.folded) ++s.opponents;
    }
    return s;
}

Action simple_move(const Situation& s, game::Random& random) {
    const double equity = game::estimate_equity(s.cards, s.board, s.opponents, kSamples, random);
    // What an average hand would get: an equal share of the pot.
    const double fair = 1.0 / (s.opponents + 1);

    if (s.choices.can_check) {
        const bool strong = equity > fair * 1.4;
        const bool bluff = random.chance(0.07);
        if (s.choices.can_raise && ((strong && random.chance(0.75)) || bluff)) {
            return bet_of(s, 0.5 + 0.25 * random.unit());
        }
        return Action{Move::Check};
    }

    // The price of a call as a part of the pot it gives a chance to win.
    const double price = static_cast<double>(s.choices.to_call) / static_cast<double>(s.pot + s.choices.to_call);
    // Whoever bets usually holds more than a random hand: ask for a margin
    // that grows with the size of the bet.
    const double needed = price + 0.04 + 0.12 * price;

    if (s.choices.can_raise && equity > std::max(fair * 1.7, needed + 0.2) && random.chance(0.6)) {
        return bet_of(s, 0.6 + 0.3 * random.unit());
    }
    if (equity >= needed) return Action{Move::Call};
    return Action{Move::Fold};
}

}  // namespace poher::bots
