// A cash-game table: seats, stacks and the dealer button between hands.
// Who the players are (people, bots, their names) is not known here: the
// caller keeps that by seat number.
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "game/hand.hpp"

namespace poher::game {

struct Stakes {
    Chips small_blind = 1;
    Chips big_blind = 2;
    Chips min_buy_in = 40;
    Chips max_buy_in = 200;
};

struct Seat {
    bool taken = false;
    Chips stack = 0;
    bool away = false;      // keeps the seat but is not dealt in
    bool in_hand = false;   // was dealt into the hand that is being played
    bool leaving = false;   // gets up as soon as that hand is over
};

// A player who got up: which seat and how many chips they take with them.
struct Departure {
    int seat = 0;
    Chips chips = 0;
};

class Table {
public:
    // `seats` is from 2 to 6.
    Table(Stakes stakes, int seats);

    const Stakes& stakes() const { return stakes_; }
    int seat_count() const { return static_cast<int>(seats_.size()); }
    const Seat& seat(int index) const { return seats_[static_cast<std::size_t>(index)]; }
    // The seat with the dealer button, or -1 before the first hand.
    int button() const { return button_; }
    // Puts the button somewhere before the first hand, so that the caller can pick it at random.
    void set_button(int seat) { button_ = seat; }

    // Takes an empty seat. The buy-in must be within the limits of the table.
    // The player is dealt in from the next hand.
    bool sit(int seat, Chips buy_in);
    // Gets the player up. If they are in a hand, they stay until it ends (the
    // caller folds for them) and the result is empty; otherwise the seat is
    // free at once and the result is the chips they take away.
    std::optional<Chips> leave(int seat);
    // Adds chips between hands, up to the maximum buy-in in total.
    bool add_chips(int seat, Chips amount);
    void set_away(int seat, bool away);

    // A seat that will get cards in the next hand.
    bool ready(int seat) const;
    // No hand is being played and at least two players are ready.
    bool can_deal() const;
    // Moves the button to the next ready player and starts a hand with this deck.
    bool deal(std::vector<Card> deck);

    // The hand being played or just finished; nullptr before the first one.
    Hand* hand() { return hand_.get(); }
    const Hand* hand() const { return hand_.get(); }
    bool playing() const { return hand_ != nullptr && !settled_; }
    // The seat whose turn it is, or -1.
    int seat_to_act() const;

    // Call once the hand is finished: stacks return to the seats, and those
    // who asked to leave get up. The result lists them.
    std::vector<Departure> settle();

private:
    Stakes stakes_;
    std::vector<Seat> seats_;
    int button_ = -1;
    std::unique_ptr<Hand> hand_;
    bool settled_ = true;
};

}  // namespace poher::game
