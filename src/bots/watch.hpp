// The public record of a hand: who put in how much and when. It is built
// only from what every player at the table sees, and it is all a bot knows
// about its opponents.
#pragma once

#include <string>
#include <vector>

#include "bots/notes.hpp"
#include "game/hand.hpp"

namespace poher::bots {

using game::Chips;

// One player in the current hand.
struct Line {
    int seat = -1;
    std::string name;
    bool folded = false;
    bool all_in = false;
    Chips stack = 0;      // chips not yet bet
    Chips bet = 0;        // put in on this street
    int after = 0;        // players who act after this one before the flop: 0 for the big blind

    // Before the flop.
    bool entered = false;  // put chips in by choice
    bool raised = false;   // and the last time it was a raise
    int level = 0;         // raises in the pot when they last put chips in, their own included
    double shove = 0;      // went all-in with a raise before the flop for this many big blinds

    // After the flop.
    double story = 0;      // how much strength their bets and calls have told
    int bets = 0;          // bets and raises made
    int checks = 0;
};

class Watch {
public:
    // The notebook may be null: then nothing is remembered between hands.
    explicit Watch(Notebook* notebook = nullptr) : notebook_(notebook) {}

    // Call right after the hand was created, before its first events are
    // passed to see(). `names` are indexed by seat number.
    void begin(const game::Hand& hand, const std::vector<std::string>& names);
    // Every event of the hand, in order. Private ones (hole cards) are ignored.
    void see(const game::Event& event);

    const std::vector<Line>& lines() const { return lines_; }
    const Line* line(int seat) const;
    game::Street street() const { return street_; }
    int dealt() const { return static_cast<int>(lines_.size()); }
    // Bets and raises on this street so far; the blinds are not counted.
    int raises() const { return raises_; }
    // Players who just called the big blind.
    int limpers() const { return limpers_; }
    // Who raised last before the flop; -1 if nobody did.
    int opener() const { return opener_; }
    // Who bet or raised last on this street; -1 if nobody did.
    int aggressor() const { return aggressor_; }
    Chips pot() const { return pot_; }
    Chips big_blind() const { return big_blind_; }
    const Notes* notes(int seat) const;

private:
    Line* find(int seat);
    Notes* page(const Line& line);

    Notebook* notebook_;
    std::vector<Line> lines_;
    game::Street street_ = game::Street::Preflop;
    Chips big_blind_ = 2;
    Chips pot_ = 0;
    Chips to_match_ = 0;   // the highest bet on this street
    int raises_ = 0;
    int limpers_ = 0;
    int opener_ = -1;
    int aggressor_ = -1;
};

}  // namespace poher::bots
