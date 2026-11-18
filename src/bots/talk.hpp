// What the bots say at the table: rare ready-made lines, each in the voice of
// its character. A line travels as a number, so every player reads it in
// their own language.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "game/random.hpp"

namespace poher::bots {

enum class Occasion : std::uint8_t {
    Hello,     // sat down at a new table
    WonBig,    // took a big pot at showdown
    LostBig,   // lost one
    BadBeat,   // lost one as the favourite
    Bluffed,   // took a pot with nothing, unseen
    NiceHand,  // somebody else took a big pot
};

struct Phrase {
    std::string_view who;
    Occasion occasion;
    std::string_view en;
    std::string_view ru;
};

// All lines. The number of a line is its place in this list, so new ones go
// to the end: older versions of the game must keep reading the old numbers.
const std::vector<Phrase>& phrases();

// A line of this character for the occasion, picked at random; -1 if it has none.
int pick_phrase(std::string_view who, Occasion occasion, game::Random& random);

// The words of a line; empty for a number that is not known.
std::string_view phrase_text(int number, bool russian);

}  // namespace poher::bots
