#include "bots/ranges.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include "bots/preflop.hpp"
#include "game/equity.hpp"
#include "game/hand_value.hpp"

namespace poher::bots {

using game::Card;
using game::Category;
using game::HandValue;

namespace {

int index_of(Card card) {
    return static_cast<int>(card.suit) * 13 + (card.rank - 2);
}

std::uint64_t bit_of(Card card) {
    return std::uint64_t{1} << index_of(card);
}

int kind(HandValue value) {
    return static_cast<int>(value.category());
}

// What the board alone gives to everybody.
struct BoardFacts {
    int kind = 0;
    int pair = 0;     // the rank of the board's own pair, if it has one
    int top = 0;      // the highest rank on it
    int second = 0;   // the next different rank
    int suits[4] = {};
    std::uint32_t ranks = 0;  // bit per rank; the ace also as bit 1
    bool river = false;
};

BoardFacts facts_of(std::span<const Card> board) {
    BoardFacts facts;
    if (board.empty()) return facts;
    const HandValue value = game::evaluate(board);
    facts.kind = kind(value);
    if (value.category() == Category::Pair || value.category() == Category::TwoPair) facts.pair = value.ranks()[0];
    for (const Card card : board) {
        ++facts.suits[static_cast<int>(card.suit)];
        facts.ranks |= 1u << card.rank;
        if (card.rank == game::kAce) facts.ranks |= 1u << 1;
        if (card.rank > facts.top) {
            facts.second = facts.top;
            facts.top = card.rank;
        } else if (card.rank < facts.top && card.rank > facts.second) {
            facts.second = card.rank;
        }
    }
    facts.river = board.size() >= 5;
    return facts;
}

// How many different ranks would complete a straight.
int straight_ranks(std::uint32_t ranks) {
    std::uint32_t missing = 0;
    for (int low = 1; low <= 10; ++low) {
        const std::uint32_t window = 0b11111u << low;
        const std::uint32_t have = ranks & window;
        if (std::popcount(have) == 4) missing |= window & ~have;
    }
    // The ace is one rank whether it plays high or low.
    if (missing & (1u << 1)) missing = (missing & ~(1u << 1)) | (1u << game::kAce);
    return std::popcount(missing);
}

double fade_rate(Card a, Card b, std::span<const Card> board, const BoardFacts& facts) {
    std::array<Card, 7> all;
    std::copy(board.begin(), board.end(), all.begin());
    all[board.size()] = a;
    all[board.size() + 1] = b;
    const HandValue value = game::evaluate(std::span<const Card>(all.data(), board.size() + 2));
    const int now = kind(value);
    const int lead = now - facts.kind;

    const int two_pair = static_cast<int>(Category::TwoPair);
    double rate = 1.1;
    if (lead > 0) {
        const bool one_pair = (now == static_cast<int>(Category::Pair)) || (now == two_pair && facts.kind > 0);
        if (!one_pair) {
            rate = 0;
        } else {
            int pair = value.ranks()[0];
            if (now == two_pair && pair == facts.pair) pair = value.ranks()[1];
            if (pair > facts.top) {
                rate = 0.15;  // an overpair
            } else if (pair == facts.top) {
                const int kicker = a.rank == pair ? b.rank : a.rank;
                rate = kicker >= game::kJack ? 0.15 : 0.3;
            } else if (pair >= facts.second) {
                rate = 0.55;
            } else {
                rate = 0.8;
            }
        }
    }
    if (!facts.river && rate > 0.4) {
        // Cards still to come: a draw is a reason to bet and to call too.
        bool flush_draw = false;
        for (int suit = 0; suit < 4; ++suit) {
            const int held = (static_cast<int>(a.suit) == suit ? 1 : 0) + (static_cast<int>(b.suit) == suit ? 1 : 0);
            if (held > 0 && facts.suits[suit] + held == 4) flush_draw = true;
        }
        std::uint32_t ranks = facts.ranks | (1u << a.rank) | (1u << b.rank);
        if (a.rank == game::kAce || b.rank == game::kAce) ranks |= 1u << 1;
        const int fills = straight_ranks(ranks) - straight_ranks(facts.ranks);
        if (flush_draw || fills >= 2) {
            rate = 0.4;
        } else if (fills == 1) {
            rate = std::min(rate, 0.9);
        }
    }
    return rate;
}

// The hands one opponent may hold, with running totals of their weights.
struct Hands {
    std::vector<Card> first;
    std::vector<Card> second;
    std::vector<double> upto;
};

}  // namespace

double fade_rate(std::array<Card, 2> cards, std::span<const Card> board) {
    if (board.size() < 3) return 1.1;
    return fade_rate(cards[0], cards[1], board, facts_of(board));
}

double equity_against(std::array<Card, 2> cards, std::span<const Card> board, std::span<const Foe> foes, int samples,
                      game::Random& random) {
    if (foes.empty()) return 1;
    const int count = static_cast<int>(foes.size());

    std::uint64_t known = bit_of(cards[0]) | bit_of(cards[1]);
    for (const Card card : board) known |= bit_of(card);
    std::vector<Card> unseen;
    for (const Card card : game::fresh_deck()) {
        if (!(known & bit_of(card))) unseen.push_back(card);
    }

    const BoardFacts facts = facts_of(board);
    std::vector<Hands> ranges(foes.size());
    for (std::size_t f = 0; f < foes.size(); ++f) {
        const Foe& foe = foes[f];
        Hands& hands = ranges[f];
        const double edge = 1 - std::clamp(foe.range, 0.0, 1.0);
        const bool told = foe.story > 0.05 && board.size() >= 3;
        double total = 0;
        for (std::size_t i = 0; i < unseen.size(); ++i) {
            for (std::size_t j = i + 1; j < unseen.size(); ++j) {
                const double strength = start_strength({unseen[i], unseen[j]});
                // Hands below the range are not ruled out, only unlikely.
                double weight = strength >= edge ? 1.0 : std::max(0.02, std::exp(-(edge - strength) / 0.06));
                if (told) weight *= std::exp(-fade_rate(unseen[i], unseen[j], board, facts) * foe.story);
                total += weight;
                hands.first.push_back(unseen[i]);
                hands.second.push_back(unseen[j]);
                hands.upto.push_back(total);
            }
        }
    }

    const std::size_t missing = 5 - board.size();
    std::array<Card, 7> ours;
    std::array<Card, 7> theirs;
    std::copy(board.begin(), board.end(), ours.begin());
    ours[5] = cards[0];
    ours[6] = cards[1];
    std::vector<std::array<Card, 2>> dealt(foes.size());

    double share = 0;
    int valid = 0;
    for (int sample = 0; sample < samples; ++sample) {
        std::uint64_t used = known;
        bool complete = true;
        for (std::size_t f = 0; f < foes.size() && complete; ++f) {
            const Hands& hands = ranges[f];
            bool found = false;
            for (int attempt = 0; attempt < 8 && !found; ++attempt) {
                const double point = random.unit() * hands.upto.back();
                const std::size_t pick = static_cast<std::size_t>(
                    std::min<std::ptrdiff_t>(std::upper_bound(hands.upto.begin(), hands.upto.end(), point) -
                                                 hands.upto.begin(),
                                             static_cast<std::ptrdiff_t>(hands.upto.size()) - 1));
                const std::uint64_t both = bit_of(hands.first[pick]) | bit_of(hands.second[pick]);
                if (used & both) continue;
                used |= both;
                dealt[f] = {hands.first[pick], hands.second[pick]};
                found = true;
            }
            complete = found;
        }
        if (!complete) continue;

        for (std::size_t i = 0; i < missing; ++i) {
            Card card;
            do {
                card = unseen[random.below(static_cast<std::uint32_t>(unseen.size()))];
            } while (used & bit_of(card));
            used |= bit_of(card);
            ours[board.size() + i] = card;
        }
        const HandValue mine = game::evaluate(ours);
        std::copy(ours.begin(), ours.begin() + 5, theirs.begin());
        int tied = 0;
        bool beaten = false;
        for (std::size_t f = 0; f < foes.size() && !beaten; ++f) {
            theirs[5] = dealt[f][0];
            theirs[6] = dealt[f][1];
            const HandValue other = game::evaluate(theirs);
            if (other > mine) beaten = true;
            if (other == mine) ++tied;
        }
        if (!beaten) share += 1.0 / (tied + 1);
        ++valid;
    }
    // So many known cards that no deal worked out: fall back to any two cards.
    if (valid == 0) return game::estimate_equity(cards, board, count, samples, random);
    return share / valid;
}

}  // namespace poher::bots
