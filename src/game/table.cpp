#include "game/table.hpp"

#include <utility>

namespace poher::game {

Table::Table(Stakes stakes, int seats) : stakes_(stakes), seats_(static_cast<std::size_t>(seats)) {}

bool Table::sit(int seat, Chips buy_in) {
    if (seat < 0 || seat >= seat_count()) return false;
    Seat& place = seats_[static_cast<std::size_t>(seat)];
    if (place.taken || buy_in < stakes_.min_buy_in || buy_in > stakes_.max_buy_in) return false;
    place = Seat{};
    place.taken = true;
    place.stack = buy_in;
    return true;
}

std::optional<Chips> Table::leave(int seat) {
    Seat& place = seats_[static_cast<std::size_t>(seat)];
    if (!place.taken) return Chips{0};
    if (place.in_hand) {
        place.leaving = true;
        return std::nullopt;
    }
    const Chips chips = place.stack;
    place = Seat{};
    return chips;
}

bool Table::add_chips(int seat, Chips amount) {
    Seat& place = seats_[static_cast<std::size_t>(seat)];
    if (!place.taken || place.in_hand || amount <= 0) return false;
    if (place.stack + amount > stakes_.max_buy_in) return false;
    place.stack += amount;
    return true;
}

void Table::set_away(int seat, bool away) {
    seats_[static_cast<std::size_t>(seat)].away = away;
}

bool Table::ready(int seat) const {
    const Seat& place = seats_[static_cast<std::size_t>(seat)];
    return place.taken && !place.away && !place.leaving && place.stack > 0;
}

bool Table::can_deal() const {
    if (playing()) return false;
    int count = 0;
    for (int i = 0; i < seat_count(); ++i) {
        if (ready(i)) ++count;
    }
    return count >= 2;
}

bool Table::deal(std::vector<Card> deck) {
    if (!can_deal() || deck.size() != kDeckSize) return false;

    // The button goes to the next ready player clockwise.
    do {
        button_ = (button_ + 1) % seat_count();
    } while (!ready(button_));

    HandSetup setup;
    setup.small_blind = stakes_.small_blind;
    setup.big_blind = stakes_.big_blind;
    setup.deck = std::move(deck);
    for (int i = 0; i < seat_count(); ++i) {
        if (!ready(i)) continue;
        if (i == button_) setup.button = setup.players.size();
        setup.players.push_back({i, seats_[static_cast<std::size_t>(i)].stack});
        seats_[static_cast<std::size_t>(i)].in_hand = true;
    }
    hand_ = std::make_unique<Hand>(std::move(setup));
    settled_ = false;
    return true;
}

int Table::seat_to_act() const {
    if (!playing() || hand_->to_act() < 0) return -1;
    return hand_->players()[static_cast<std::size_t>(hand_->to_act())].seat;
}

std::vector<Departure> Table::settle() {
    std::vector<Departure> gone;
    if (!playing() || !hand_->finished()) return gone;

    for (const HandPlayer& player : hand_->players()) {
        seats_[static_cast<std::size_t>(player.seat)].stack = player.stack;
    }
    for (int i = 0; i < seat_count(); ++i) {
        Seat& place = seats_[static_cast<std::size_t>(i)];
        place.in_hand = false;
        if (place.taken && place.leaving) {
            gone.push_back({i, place.stack});
            place = Seat{};
        }
    }
    settled_ = true;
    return gone;
}

}  // namespace poher::game
