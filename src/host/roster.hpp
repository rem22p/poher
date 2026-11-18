// The fixed numbers of the game: stake levels, bankrolls and the nine bots.
// They are explained in SPEC.md, sections 3.2 and 5.
#pragma once

#include <array>
#include <string_view>

#include "game/table.hpp"

namespace poher::host {

using game::Chips;

struct Level {
    game::Stakes stakes;
    Chips bankroll;  // needed to sit down: the biggest buy-in of the level
};

inline constexpr std::array<Level, 5> kLevels = {{
    {{1, 2, 40, 200}, 0},
    {{5, 10, 200, 1000}, 1000},
    {{25, 50, 1000, 5000}, 5000},
    {{100, 200, 4000, 20000}, 20000},
    {{500, 1000, 20000, 100000}, 100000},
}};

inline constexpr Chips kStartBankroll = 10000;
// "Refill" is offered when the player has less than this, chips at the table included.
inline constexpr Chips kRefillBelow = 5000;

enum class Strength : std::uint8_t { Weak, Medium, Strong };

struct Bot {
    std::string_view name;
    std::string_view avatar;  // the picture in the avatar set
    Strength strength;
};

inline constexpr std::array<Bot, 9> kBots = {{
    {"Vasya", "08", Strength::Weak},
    {"Chad", "02", Strength::Weak},
    {"Bibi", "17", Strength::Weak},
    {"Niga", "05", Strength::Medium},
    {"Karp", "20", Strength::Medium},
    {"Snegr", "14", Strength::Medium},
    {"Chips", "19", Strength::Strong},
    {"Slon", "10", Strength::Strong},
    {"Vlid", "04", Strength::Strong},
}};

// The bankroll a bot starts with decides the highest level it sits at: weak
// bots up to the third level, medium ones up to the fourth, strong anywhere.
constexpr Chips start_bankroll(Strength strength) {
    switch (strength) {
        case Strength::Weak: return 10000;
        case Strength::Medium: return 40000;
        case Strength::Strong: return 200000;
    }
    return 0;
}

constexpr bool bot_sits_at(const Bot& bot, std::size_t level) {
    return start_bankroll(bot.strength) >= kLevels[level].bankroll;
}

// The picture a new player gets until they choose their own.
inline constexpr std::string_view kDefaultAvatar = "01";

}  // namespace poher::host
