// What one person sees of a table at a given moment. The screen is drawn from
// this and nothing else, so the same picture can later be built on another
// computer from what the host of the table sends.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/hand.hpp"
#include "game/table.hpp"

namespace poher::host {

using game::Card;
using game::Chips;

// The last thing a player did, shown under their stack.
enum class Note : std::uint8_t {
    None,
    Waiting,     // sat down, gets cards from the next hand
    Away,
    SmallBlind,
    BigBlind,
    Fold,
    Check,
    Call,
    Bet,
    Raise,
    AllIn,
    Show,
    Muck,
    Win,
};

struct SeatView {
    bool taken = false;
    std::string name;
    std::string avatar;    // the name of the picture in the avatar set
    bool bot = false;
    Chips stack = 0;       // chips behind, not counting the bet in front
    Chips bet = 0;         // chips put in on this street
    bool in_hand = false;  // was dealt cards in this hand
    bool folded = false;
    bool all_in = false;
    bool away = false;
    bool offline = false;  // lost the connection
    Note note = Note::None;
    Chips note_amount = 0;
    // The cards, if the viewer may see them: their own, or shown to everybody.
    std::optional<std::array<Card, 2>> cards;
    bool shown = false;    // the cards lie face up for everybody
    Chips won = 0;         // taken from the pot in the hand that just ended
};

enum class Stage : std::uint8_t {
    Idle,     // no hand: waiting for players
    Playing,
    Over,     // the hand has ended and its result is on the screen
};

// One line of the record of what happened at the table.
struct LogLine {
    enum class Kind : std::uint8_t {
        NewHand,   // number
        Blind,
        Fold,
        Check,
        Call,
        Bet,
        Raise,
        AllIn,
        Board,     // cards: the whole board so far
        Shows,     // cards
        Mucks,
        Returned,
        Wins,
        Rebuy,
        Says,      // number: which line a bot says (bots/talk.hpp)
    };
    Kind kind = Kind::NewHand;
    std::string name;
    bool bot = false;
    Chips amount = 0;
    int number = 0;
    std::vector<Card> cards;
};

struct TableView {
    game::Stakes stakes;
    int hand_number = 0;
    Stage stage = Stage::Idle;
    std::vector<SeatView> seats;   // by seat number
    int viewer = -1;               // the seat of whoever looks; -1 for a spectator
    int button = -1;
    int acting = -1;               // whose turn it is
    // The clock of the acting player, in milliseconds: time left for the move,
    // then the reserve. -1 when there is no clock.
    int turn_ms = -1;
    int bank_ms = -1;
    int aggressor = -1;            // who made the last bet or raise of this street
    int last_actor = -1;           // who moved last
    std::vector<Card> board;
    Chips pot = 0;                 // all chips in the middle, bets in front of players included
    // Set when it is the viewer's turn.
    std::optional<game::Choices> choices;
    // The viewer asked for more chips and gets them when the hand is over.
    Chips chips_coming = 0;
    std::vector<LogLine> log;      // the latest lines, oldest first
    std::int64_t log_total = 0;    // lines written since the table opened; `log` holds the last of them
};

}  // namespace poher::host
