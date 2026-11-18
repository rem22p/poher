#include "game/hand.hpp"

#include <algorithm>
#include <utility>

#include "game/hand_value.hpp"

namespace poher::game {

Hand::Hand(HandSetup setup)
    : small_blind_(setup.small_blind),
      big_blind_(setup.big_blind),
      button_(setup.button),
      deck_(std::move(setup.deck)) {
    for (const HandSetup::Player& entry : setup.players) {
        HandPlayer player;
        player.seat = entry.seat;
        player.stack = entry.stack;
        players_.push_back(player);
    }
    const std::size_t count = players_.size();
    acted_.assign(count, false);
    faced_.assign(count, 0);

    // One on one the button posts the small blind; otherwise the player to its left does.
    const std::size_t small = count == 2 ? button_ : next(button_);
    const std::size_t big = next(small);
    post_blind(small, small_blind_, false);
    post_blind(big, big_blind_, true);
    // Even if the big blind could not post in full, the others still have to match a full one.
    current_bet_ = big_blind_;
    raise_size_ = big_blind_;

    // One card at a time, starting to the left of the button, twice around.
    for (std::size_t round = 0; round < 2; ++round) {
        std::size_t index = button_;
        for (std::size_t i = 0; i < count; ++i) {
            index = next(index);
            players_[index].cards[round] = deck_[dealt_++];
        }
    }
    for (const HandPlayer& player : players_) events_.push_back(event::Dealt{player.seat, player.cards});
    board_start_ = dealt_;

    move_on(big);
}

Chips Hand::pot() const {
    Chips sum = 0;
    for (const HandPlayer& player : players_) sum += player.total;
    return sum;
}

std::vector<Event> Hand::take_events() {
    return std::exchange(events_, {});
}

std::vector<Card> Hand::rest_of_board() const {
    const auto begin = deck_.begin() + static_cast<std::ptrdiff_t>(board_start_ + board_.size());
    const auto end = deck_.begin() + static_cast<std::ptrdiff_t>(board_start_ + 5);
    return std::vector<Card>(begin, end);
}

int Hand::count_not_folded() const {
    int count = 0;
    for (const HandPlayer& player : players_) {
        if (!player.folded) ++count;
    }
    return count;
}

int Hand::count_can_act_except(std::size_t index) const {
    int count = 0;
    for (std::size_t i = 0; i < players_.size(); ++i) {
        if (i != index && can_act(i)) ++count;
    }
    return count;
}

bool Hand::needs_action(std::size_t index) const {
    if (!can_act(index)) return false;
    if (players_[index].bet < current_bet_) return true;
    // With nobody left to bet against, a check or a bet would change nothing.
    return !acted_[index] && count_can_act_except(index) > 0;
}

void Hand::put_in(std::size_t index, Chips amount) {
    HandPlayer& player = players_[index];
    player.stack -= amount;
    player.bet += amount;
    player.total += amount;
    if (player.stack == 0) player.all_in = true;
}

void Hand::post_blind(std::size_t index, Chips amount, bool big) {
    amount = std::min(amount, players_[index].stack);
    put_in(index, amount);
    events_.push_back(event::Blind{players_[index].seat, amount, big});
}

Choices Hand::choices() const {
    Choices out;
    if (to_act_ < 0) return out;
    const auto index = static_cast<std::size_t>(to_act_);
    const HandPlayer& player = players_[index];

    out.can_check = player.bet >= current_bet_;
    out.to_call = out.can_check ? 0 : std::min(current_bet_ - player.bet, player.stack);

    // A player who has already moved may raise again only when the bet has
    // grown by a full raise since then. All-ins for less do not reopen the betting.
    const bool reopened = !acted_[index] || current_bet_ - faced_[index] >= raise_size_;
    const Chips all_in = player.bet + player.stack;
    if (all_in > current_bet_ && reopened && count_can_act_except(index) > 0) {
        out.can_raise = true;
        out.max_raise_to = all_in;
        out.min_raise_to = std::min(current_bet_ + raise_size_, all_in);
    }
    return out;
}

bool Hand::act(Action action) {
    if (finished_ || to_act_ < 0) return false;
    const auto index = static_cast<std::size_t>(to_act_);
    HandPlayer& player = players_[index];
    const Choices allowed = choices();

    switch (action.move) {
        case Move::Fold:
            player.folded = true;
            events_.push_back(event::Acted{player.seat, Move::Fold, 0, player.bet, false});
            break;

        case Move::Check:
            if (!allowed.can_check) return false;
            events_.push_back(event::Acted{player.seat, Move::Check, 0, player.bet, false});
            break;

        case Move::Call:
            if (allowed.to_call <= 0) return false;
            put_in(index, allowed.to_call);
            events_.push_back(event::Acted{player.seat, Move::Call, allowed.to_call, player.bet, player.all_in});
            break;

        case Move::Bet:
        case Move::Raise: {
            if (!allowed.can_raise || action.to < allowed.min_raise_to || action.to > allowed.max_raise_to) {
                return false;
            }
            const Move move = current_bet_ == 0 ? Move::Bet : Move::Raise;
            const Chips added = action.to - player.bet;
            const Chips increase = action.to - current_bet_;
            put_in(index, added);
            if (increase >= raise_size_) {
                // A full raise: everyone else has to answer it and may raise again.
                raise_size_ = increase;
                std::fill(acted_.begin(), acted_.end(), false);
            }
            current_bet_ = action.to;
            aggressor_ = to_act_;
            events_.push_back(event::Acted{player.seat, move, added, player.bet, player.all_in});
            break;
        }
    }

    acted_[index] = true;
    faced_[index] = current_bet_;
    to_act_ = -1;
    move_on(index);
    return true;
}

// Finds who moves next after the given player, or closes the street.
void Hand::move_on(std::size_t after) {
    if (count_not_folded() == 1) {
        win_unopposed();
        return;
    }
    std::size_t index = after;
    for (std::size_t i = 0; i < players_.size(); ++i) {
        index = next(index);
        if (needs_action(index)) {
            to_act_ = static_cast<int>(index);
            return;
        }
    }
    end_street();
}

void Hand::end_street() {
    return_uncalled();
    for (HandPlayer& player : players_) player.bet = 0;

    if (street_ == Street::River) {
        show_down();
        return;
    }
    int able = 0;
    for (std::size_t i = 0; i < players_.size(); ++i) {
        if (can_act(i)) ++able;
    }
    if (able <= 1) {
        // Nobody left to bet against each other: deal the rest and compare.
        run_out();
        return;
    }

    street_ = static_cast<Street>(static_cast<int>(street_) + 1);
    deal_board(street_, street_ == Street::Flop ? 3 : 1);
    current_bet_ = 0;
    raise_size_ = big_blind_;
    aggressor_ = -1;
    std::fill(acted_.begin(), acted_.end(), false);
    // After the flop the first player to the left of the button starts.
    move_on(button_);
}

// Gives back the part of the highest bet that nobody matched.
void Hand::return_uncalled() {
    std::size_t top = 0;
    Chips highest = 0;
    Chips second = 0;
    for (std::size_t i = 0; i < players_.size(); ++i) {
        const Chips bet = players_[i].bet;
        if (bet > highest) {
            second = highest;
            highest = bet;
            top = i;
        } else if (bet > second) {
            second = bet;
        }
    }
    const Chips extra = highest - second;
    if (extra <= 0) return;

    HandPlayer& player = players_[top];
    player.bet -= extra;
    player.total -= extra;
    player.stack += extra;
    player.all_in = false;
    events_.push_back(event::Returned{player.seat, extra});
}

void Hand::deal_board(Street street, int count) {
    std::vector<Card> cards;
    for (int i = 0; i < count; ++i) cards.push_back(deck_[dealt_++]);
    board_.insert(board_.end(), cards.begin(), cards.end());
    events_.push_back(event::Board{street, std::move(cards)});
}

void Hand::run_out() {
    // The betting is over while cards are still to come: hands go face up first.
    for (const HandPlayer& player : players_) {
        if (!player.folded) events_.push_back(event::Showed{player.seat, player.cards});
    }
    revealed_ = true;
    while (street_ != Street::River) {
        street_ = static_cast<Street>(static_cast<int>(street_) + 1);
        deal_board(street_, street_ == Street::Flop ? 3 : 1);
    }
    show_down();
}

// Splits the chips into the main pot and side pots. A player can win from
// each opponent no more than they put in themselves.
std::vector<Hand::Pot> Hand::pots() const {
    std::vector<Chips> levels;
    for (const HandPlayer& player : players_) {
        if (!player.folded) levels.push_back(player.total);
    }
    std::sort(levels.begin(), levels.end());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

    std::vector<Pot> out;
    Chips below = 0;
    Chips placed = 0;
    for (const Chips level : levels) {
        Pot pot;
        for (std::size_t i = 0; i < players_.size(); ++i) {
            const HandPlayer& player = players_[i];
            pot.amount += std::min(player.total, level) - std::min(player.total, below);
            if (!player.folded && player.total >= level) pot.players.push_back(i);
        }
        below = level;
        placed += pot.amount;
        if (pot.amount > 0) out.push_back(std::move(pot));
    }
    // Chips of folded players above the last level, if any, stay in the last pot.
    if (!out.empty()) out.back().amount += pot() - placed;
    return out;
}

void Hand::show_down() {
    const std::vector<Pot> all = pots();

    std::vector<HandValue> values(players_.size());
    for (std::size_t i = 0; i < players_.size(); ++i) {
        if (players_[i].folded) continue;
        std::vector<Card> seven = board_;
        seven.push_back(players_[i].cards[0]);
        seven.push_back(players_[i].cards[1]);
        values[i] = evaluate(seven);
    }

    if (!revealed_) {
        // The last player to bet or raise on the river shows first; if nobody
        // bet, the first one to the left of the button. A player after that
        // shows only a hand that wins or ties some pot, and throws away the rest.
        std::vector<HandValue> best(all.size());
        std::vector<bool> shown(all.size(), false);
        bool anyone = false;
        std::size_t index = aggressor_ >= 0 ? static_cast<std::size_t>(aggressor_) : next(button_);
        for (std::size_t i = 0; i < players_.size(); ++i, index = next(index)) {
            const HandPlayer& player = players_[index];
            if (player.folded) continue;

            bool shows = !anyone;
            for (std::size_t k = 0; k < all.size(); ++k) {
                const bool in_pot = std::find(all[k].players.begin(), all[k].players.end(), index) != all[k].players.end();
                if (in_pot && (!shown[k] || values[index] >= best[k])) shows = true;
            }
            if (!shows) {
                events_.push_back(event::Mucked{player.seat});
                continue;
            }
            events_.push_back(event::Showed{player.seat, player.cards});
            anyone = true;
            for (std::size_t k = 0; k < all.size(); ++k) {
                const bool in_pot = std::find(all[k].players.begin(), all[k].players.end(), index) != all[k].players.end();
                if (in_pot && (!shown[k] || values[index] > best[k])) {
                    best[k] = values[index];
                    shown[k] = true;
                }
            }
        }
    }

    for (std::size_t k = 0; k < all.size(); ++k) {
        const Pot& pot = all[k];
        HandValue best;
        for (const std::size_t i : pot.players) best = std::max(best, values[i]);

        // Winners in table order starting to the left of the button: chips
        // that do not divide evenly go to the first of them.
        std::vector<std::size_t> winners;
        std::size_t index = button_;
        for (std::size_t i = 0; i < players_.size(); ++i) {
            index = next(index);
            const bool in_pot = std::find(pot.players.begin(), pot.players.end(), index) != pot.players.end();
            if (in_pot && values[index] == best) winners.push_back(index);
        }
        const Chips share = pot.amount / static_cast<Chips>(winners.size());
        Chips odd = pot.amount % static_cast<Chips>(winners.size());
        for (const std::size_t winner : winners) {
            const Chips amount = share + (odd > 0 ? 1 : 0);
            if (odd > 0) --odd;
            players_[winner].stack += amount;
            events_.push_back(event::Won{players_[winner].seat, amount, static_cast<int>(k)});
        }
    }
    finished_ = true;
    to_act_ = -1;
}

void Hand::win_unopposed() {
    return_uncalled();
    const Chips all = pot();
    for (HandPlayer& player : players_) {
        player.bet = 0;
        if (player.folded) continue;
        player.stack += all;
        events_.push_back(event::Won{player.seat, all, 0});
    }
    finished_ = true;
    to_act_ = -1;
}

}  // namespace poher::game
