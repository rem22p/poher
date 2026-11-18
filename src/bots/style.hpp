// The characters of the bots as numbers. A bot decides with the same
// arithmetic as any other; its style bends the result.
#pragma once

#include <string_view>
#include <vector>

namespace poher::bots {

// Unless said otherwise a value runs from 0 (not at all) to 1 (as much as it gets).
struct Style {
    double loose = 1;      // how many starting hands it plays: 1 = a solid regular, 2 = twice as many
    double fancy = 0;      // takes any ace, any pair, anything suited for a good hand
    double pre_raise = 1;  // the share of its starting hands it raises with; the rest it just calls
    double aggr = 0.6;     // bets and raises rather than checks and calls
    double bluff = 0.1;    // bets with nothing
    double sticky = 0.2;   // does not let go of a pair or a draw
    double fear = 0.1;     // backs off when the bet is big
    double trap = 0.1;     // hides a strong hand to strike later
    double chase = 0.3;    // pays too much to see if the draw comes in
    double tilt = 0.1;     // how badly a big loss throws it off
    double guard = 0.1;    // tightens up when it is ahead for the evening
    double read = 1;       // how well it narrows down what an opponent holds
    double noise = 0.03;   // how far off its feel for its own chances can be
    double adapt = 0.5;    // how much it leans on what it remembers of an opponent
    double size = 1;       // bet size habit: 0.7 small, 1 usual, 1.35 big
    double honest = 0;     // bet size follows the strength of the hand: easy to read
    double tempo = 1;      // thinking time: 0.7 quick, 1 usual, 1.3 slow
    double talk = 0.5;     // how often it has something to say
};

struct Character {
    std::string_view name;
    // Most have one style. One with several shifts between them every
    // `shift_min` to `shift_max` hands.
    std::vector<Style> gears;
    int shift_min = 0;
    int shift_max = 0;
};

// The nine named bots (SPEC.md, 5.1). An unknown name gets a plain solid style.
const Character& character(std::string_view name);
const std::vector<Character>& characters();
// Replaces a character or adds a new one. For trying out numbers (tools/sim.cpp), not for the game.
void put_character(Character who);

}  // namespace poher::bots
