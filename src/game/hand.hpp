// One hand of no-limit Texas hold'em, from the blinds to the last chip handed
// out. The class only applies the rules: who sits at the table, where the
// cards come from and who decides the moves is up to the caller.
#pragma once

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

#include "game/cards.hpp"

namespace poher::game {

using Chips = std::int64_t;

enum class Street : std::uint8_t { Preflop, Flop, Turn, River };

enum class Move : std::uint8_t { Fold, Check, Call, Bet, Raise };

struct Action {
    Move move = Move::Fold;
    // For Bet and Raise: how much the player will have put in on this street
    // after the move, as in "raise to 120". Not used by the other moves.
    Chips to = 0;
};

// What the player whose turn it is may do.
struct Choices {
    bool can_check = false;
    Chips to_call = 0;        // 0 when a check is possible; never more than the stack
    bool can_raise = false;   // a bet, when nobody has bet on this street yet
    Chips min_raise_to = 0;   // equals max_raise_to when the stack is too short for a full raise
    Chips max_raise_to = 0;   // all-in
};

struct HandPlayer {
    int seat = 0;             // the number the caller gave this player
    Chips stack = 0;          // chips not yet bet
    Chips bet = 0;            // put in on the current street
    Chips total = 0;          // put in during the whole hand
    bool folded = false;
    bool all_in = false;
    std::array<Card, 2> cards{};
};

// What happened, in order. The caller shows it, sends it over the network and
// writes it to the hand history.
namespace event {
struct Blind { int seat; Chips amount; bool big; };
struct Dealt { int seat; std::array<Card, 2> cards; };   // for that player's eyes only
struct Acted { int seat; Move move; Chips added; Chips to; bool all_in; };
struct Board { Street street; std::vector<Card> cards; };  // the new cards: three, then one, then one
struct Returned { int seat; Chips amount; };              // the part of a bet nobody called
struct Showed { int seat; std::array<Card, 2> cards; };
struct Mucked { int seat; };                              // threw a losing hand away unseen
struct Won { int seat; Chips amount; int pot; };          // pot 0 is the main one, 1 and up are side pots
}  // namespace event

using Event = std::variant<event::Blind, event::Dealt, event::Acted, event::Board, event::Returned, event::Showed,
                           event::Mucked, event::Won>;

struct HandSetup {
    struct Player {
        int seat = 0;
        Chips stack = 0;
    };

    Chips small_blind = 1;
    Chips big_blind = 2;
    std::vector<Player> players;  // everyone dealt in, clockwise; two or more, all with chips
    std::size_t button = 0;       // which of `players` has the dealer button
    std::vector<Card> deck;       // all 52 cards in the order they will be dealt
};

class Hand {
public:
    // Posts the blinds and deals the hole cards.
    explicit Hand(HandSetup setup);

    bool finished() const { return finished_; }
    // Index into players() of whose turn it is, or -1 when the hand is over.
    int to_act() const { return to_act_; }
    Choices choices() const;
    // Makes the move for the player to act. Returns false, changing nothing,
    // if the rules do not allow it.
    bool act(Action action);

    Street street() const { return street_; }
    const std::vector<Card>& board() const { return board_; }
    // All chips in the middle, bets of the current street included.
    Chips pot() const;
    const std::vector<HandPlayer>& players() const { return players_; }
    Chips small_blind() const { return small_blind_; }
    Chips big_blind() const { return big_blind_; }
    std::size_t button() const { return button_; }

    // Everything that happened since the previous call.
    std::vector<Event> take_events();

    // The board cards that were never dealt because the hand ended early.
    std::vector<Card> rest_of_board() const;

private:
    struct Pot {
        Chips amount = 0;
        std::vector<std::size_t> players;  // who can win it
    };

    std::size_t next(std::size_t index) const { return (index + 1) % players_.size(); }
    bool can_act(std::size_t index) const { return !players_[index].folded && !players_[index].all_in; }
    int count_not_folded() const;
    int count_can_act_except(std::size_t index) const;
    bool needs_action(std::size_t index) const;

    void put_in(std::size_t index, Chips amount);
    void post_blind(std::size_t index, Chips amount, bool big);
    void move_on(std::size_t after);
    void end_street();
    void return_uncalled();
    void deal_board(Street street, int count);
    void run_out();
    void show_down();
    void win_unopposed();
    std::vector<Pot> pots() const;
    void award(const std::vector<Pot>& pots);

    Chips small_blind_;
    Chips big_blind_;
    std::size_t button_;
    std::vector<Card> deck_;
    std::size_t dealt_ = 0;         // how many cards of the deck are used
    std::size_t board_start_ = 0;   // where the five board cards begin in the deck

    std::vector<HandPlayer> players_;
    std::vector<bool> acted_;       // made a move since the last full bet or raise
    std::vector<Chips> faced_;      // the bet to match at the moment of that move
    std::vector<Card> board_;
    std::vector<Event> events_;

    Street street_ = Street::Preflop;
    Chips current_bet_ = 0;         // the highest bet on this street
    Chips raise_size_ = 0;          // the smallest amount a raise must add
    int aggressor_ = -1;            // who bet or raised last on this street
    int to_act_ = -1;
    bool revealed_ = false;         // hands were turned face up before the river (all-in)
    bool finished_ = false;
};

}  // namespace poher::game
