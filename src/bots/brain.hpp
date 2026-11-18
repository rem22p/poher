// The mind of one named bot: it turns what the bot sees into a move and into
// the time the move takes. It also carries the bot's mood from hand to hand.
#pragma once

#include <array>
#include <vector>

#include "bots/ranges.hpp"
#include "bots/style.hpp"
#include "bots/watch.hpp"
#include "game/hand.hpp"
#include "game/random.hpp"

namespace poher::bots {

// What a bot knows when its turn comes, besides the public record of the
// hand: its own two cards and its place in the betting.
struct Spot {
    int seat = 0;
    std::array<game::Card, 2> cards{};
    std::vector<game::Card> board;
    game::Street street = game::Street::Preflop;
    Chips pot = 0;        // all chips in the middle, bets of this street included
    Chips bet = 0;        // our own chips in on this street
    Chips stack = 0;      // our chips not yet bet
    Chips big_blind = 2;
    game::Choices choices;
    bool last = false;    // after the flop: nobody who can still bet acts after us
};

// Reads the spot of the player to act off a hand. Nothing else of the hand
// reaches a bot: no other cards, no deck.
Spot spot_of(const game::Hand& hand);

struct Decision {
    game::Action action;
    double effort = 0;  // 0 = no thought needed ... 1 = the hardest kind of decision
};

// How a hand ended for the bot.
struct Outcome {
    double won = 0;         // in big blinds; negative for a loss
    bool showdown = false;  // the bot's cards went to the showdown
    double biggest = 0;     // the most anybody won in the hand, in big blinds
};

// What a bot has got out of the hands it played at this table, in big blinds.
struct Record {
    int hands = 0;
    int showdowns = 0;      // hands in which its cards were turned over
    double at_showdown = 0; // won or lost in those
    double without = 0;     // won or lost in all the others
};

class Brain {
public:
    Brain(const Character& who, std::uint64_t seed);

    Decision decide(const Spot& spot, const Watch& watch);
    // Once after every hand the bot was dealt into. Returns the number of a
    // line the bot wants to say about it (bots/talk.hpp), or -1: mostly -1.
    int hand_over(const Outcome& outcome);
    // A greeting on sitting down, or -1.
    int hello();

    const Character& who() const { return *who_; }
    // The style right now: the gear the bot is in, bent by its mood.
    Style style() const;
    // 0 = calm, 1 = steaming.
    double tilt() const { return tilt_; }
    // Won or lost since it sat down, in big blinds.
    double session() const { return session_; }
    const Record& record() const { return record_; }

private:
    Decision before_flop(const Spot& spot, const Watch& watch, const Style& style);
    Decision all_in_spot(const Spot& spot, const Watch& watch, const Style& style);
    Decision after_flop(const Spot& spot, const Watch& watch, const Style& style);
    double believed_range(const Line& line, const Watch& watch, const Style& style) const;
    std::vector<Foe> foes(const Spot& spot, const Watch& watch, const Style& style, bool only_committed) const;
    double fold_habit(const Spot& spot, const Watch& watch, const Style& style) const;
    double pick_part(const Style& style, double single, bool bluffing, int to_come);
    double some(double from, double to) { return from + (to - from) * random_.unit(); }

    const Character* who_;
    game::Random random_;
    std::size_t gear_ = 0;
    int shift_in_ = 0;       // hands until the next change of gear
    double tilt_ = 0;
    double session_ = 0;
    Record record_;
    int quiet_ = 100;        // hands since it last said something

    // Within one hand.
    bool trapping_ = false;  // checked or just called a strong hand to raise later
    bool had_draw_ = false;
    bool fired_ = false;     // bet without a hand on an earlier street
    double peak_ = 0;        // the best it felt about the hand when chips went in
};

}  // namespace poher::bots
