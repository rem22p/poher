#include <algorithm>
#include <random>
#include <string>

#include "check.hpp"
#include "game/hand.hpp"
#include "game/hand_value.hpp"

using namespace poher::game;

namespace {

// A deck that deals exactly these hole cards and this board. `holes` goes by
// player index; the cards nobody asked for follow in a fixed order.
std::vector<Card> stacked_deck(std::size_t button, const std::vector<std::string>& holes, const std::string& board) {
    const std::size_t count = holes.size();
    std::vector<Card> order;
    for (std::size_t round = 0; round < 2; ++round) {
        std::size_t index = button;
        for (std::size_t i = 0; i < count; ++i) {
            index = (index + 1) % count;
            order.push_back(parse_cards(holes[index])[round]);
        }
    }
    for (const Card card : parse_cards(board)) order.push_back(card);
    for (const Card card : fresh_deck()) {
        if (std::find(order.begin(), order.end(), card) == order.end()) order.push_back(card);
    }
    return order;
}

// A hand with blinds 1/2 where player i sits in seat i.
struct Game {
    Hand hand;
    std::vector<Event> log;

    Game(const std::vector<Chips>& stacks, std::size_t button, const std::vector<std::string>& holes = {},
         const std::string& board = "", Chips small = 1, Chips big = 2)
        : hand(setup(stacks, button, holes, board, small, big)) {
        collect();
    }

    static HandSetup setup(const std::vector<Chips>& stacks, std::size_t button, const std::vector<std::string>& holes,
                           const std::string& board, Chips small, Chips big) {
        HandSetup out;
        out.small_blind = small;
        out.big_blind = big;
        out.button = button;
        for (std::size_t i = 0; i < stacks.size(); ++i) out.players.push_back({static_cast<int>(i), stacks[i]});
        if (holes.empty()) {
            const auto fresh = fresh_deck();
            out.deck.assign(fresh.begin(), fresh.end());
        } else {
            out.deck = stacked_deck(button, holes, board);
        }
        return out;
    }

    void collect() {
        for (Event& e : hand.take_events()) log.push_back(std::move(e));
    }

    bool act(Move move, Chips to = 0) {
        const bool ok = hand.act(Action{move, to});
        collect();
        return ok;
    }
    bool fold() { return act(Move::Fold); }
    bool check() { return act(Move::Check); }
    bool call() { return act(Move::Call); }
    bool raise(Chips to) { return act(Move::Raise, to); }

    Chips stack(std::size_t index) const { return hand.players()[index].stack; }

    Chips won(int seat) const {
        Chips sum = 0;
        for (const Event& e : log) {
            if (const auto* win = std::get_if<event::Won>(&e); win != nullptr && win->seat == seat) sum += win->amount;
        }
        return sum;
    }

    Chips returned(int seat) const {
        Chips sum = 0;
        for (const Event& e : log) {
            if (const auto* back = std::get_if<event::Returned>(&e); back != nullptr && back->seat == seat) {
                sum += back->amount;
            }
        }
        return sum;
    }

    template <typename Kind>
    std::vector<int> seats_of() const {
        std::vector<int> seats;
        for (const Event& e : log) {
            if (const auto* found = std::get_if<Kind>(&e)) seats.push_back(found->seat);
        }
        return seats;
    }
};

const std::string kDryBoard = "2c 7d 9h Jc 3s";

}  // namespace

TEST(heads_up_button_is_small_blind) {
    Game game({100, 100}, 0);
    const auto blinds = game.seats_of<event::Blind>();
    CHECK(blinds == std::vector<int>({0, 1}));
    CHECK_EQ(game.stack(0), 99);
    CHECK_EQ(game.stack(1), 98);
    // The button acts first before the flop and last after it.
    CHECK_EQ(game.hand.to_act(), 0);
    CHECK(game.call());
    CHECK_EQ(game.hand.to_act(), 1);
    CHECK(game.check());
    CHECK(game.hand.street() == Street::Flop);
    CHECK_EQ(game.hand.board().size(), 3);
    CHECK_EQ(game.hand.to_act(), 1);
}

TEST(three_players_order) {
    Game game({100, 100, 100}, 0);
    CHECK(game.seats_of<event::Blind>() == std::vector<int>({1, 2}));
    CHECK_EQ(game.hand.to_act(), 0);
    CHECK(game.call());
    CHECK_EQ(game.hand.to_act(), 1);
    CHECK(game.call());
    CHECK_EQ(game.hand.to_act(), 2);
    // The big blind was only called and still has a move.
    CHECK(game.hand.street() == Street::Preflop);
    CHECK(game.hand.choices().can_check);
    CHECK(game.hand.choices().can_raise);
    CHECK(game.check());
    CHECK(game.hand.street() == Street::Flop);
    CHECK_EQ(game.hand.to_act(), 1);
    CHECK_EQ(game.hand.pot(), 6);
}

TEST(everyone_folds_to_the_big_blind) {
    Game game({100, 100, 100}, 0);
    CHECK(game.fold());
    CHECK(game.fold());
    CHECK(game.hand.finished());
    CHECK_EQ(game.hand.to_act(), -1);
    CHECK_EQ(game.returned(2), 1);
    CHECK_EQ(game.won(2), 2);
    CHECK_EQ(game.stack(0), 100);
    CHECK_EQ(game.stack(1), 99);
    CHECK_EQ(game.stack(2), 101);
    CHECK(game.seats_of<event::Showed>().empty());
    CHECK_EQ(game.hand.rest_of_board().size(), 5);
}

TEST(smallest_raise) {
    Game game({1000, 1000, 1000}, 0);
    Choices choices = game.hand.choices();
    CHECK_EQ(choices.to_call, 2);
    CHECK_EQ(choices.min_raise_to, 4);
    CHECK_EQ(choices.max_raise_to, 1000);
    CHECK(!game.raise(3));
    CHECK(!game.raise(1001));
    CHECK(!game.check());
    CHECK(game.raise(4));
    // A raise must add at least as much as the previous one did.
    CHECK_EQ(game.hand.choices().min_raise_to, 6);
    CHECK(game.raise(10));
    choices = game.hand.choices();
    CHECK_EQ(choices.to_call, 8);
    CHECK_EQ(choices.min_raise_to, 16);
    // After the flop the smallest bet is the big blind.
    CHECK(game.call());  // seat 2
    CHECK(game.call());  // seat 0
    CHECK(game.hand.street() == Street::Flop);
    choices = game.hand.choices();
    CHECK(choices.can_check);
    CHECK_EQ(choices.min_raise_to, 2);
    CHECK(!game.act(Move::Bet, 1));
    CHECK(game.act(Move::Bet, 30));
    CHECK_EQ(game.hand.choices().min_raise_to, 60);
}

TEST(short_all_in_does_not_reopen_betting) {
    // Seat 0 has 132: 2 go in before the flop, 130 are left.
    Game game({132, 1000, 1000}, 0);
    CHECK(game.call());
    CHECK(game.call());
    CHECK(game.check());
    // Flop: seat 1 bets 100, seat 2 calls, seat 0 goes all-in for 130.
    CHECK(game.act(Move::Bet, 100));
    CHECK(game.call());
    CHECK(game.raise(130));
    // 30 more is not a full raise: those who already moved may only call or fold.
    CHECK_EQ(game.hand.to_act(), 1);
    Choices choices = game.hand.choices();
    CHECK_EQ(choices.to_call, 30);
    CHECK(!choices.can_raise);
    CHECK(!game.raise(400));
    CHECK(game.call());
    CHECK_EQ(game.hand.to_act(), 2);
    CHECK(!game.hand.choices().can_raise);
    CHECK(game.call());
    // Two players with chips remain and play on.
    CHECK(game.hand.street() == Street::Turn);
    CHECK_EQ(game.hand.pot(), 6 + 390);
}

TEST(short_all_in_before_a_player_moved) {
    // Seat 2 has 132 and sits between the bettor and seat 0.
    Game game({1000, 1000, 132}, 0);
    CHECK(game.call());
    CHECK(game.call());
    CHECK(game.check());
    CHECK(game.act(Move::Bet, 100));  // seat 1
    CHECK(game.raise(130));           // seat 2, all-in
    // Seat 0 has not moved yet on this street, so a raise is allowed: at least 100 more.
    CHECK_EQ(game.hand.to_act(), 0);
    const Choices choices = game.hand.choices();
    CHECK(choices.can_raise);
    CHECK_EQ(choices.min_raise_to, 230);
}

TEST(several_short_all_ins_add_up_to_a_raise) {
    // The textbook case, blinds 50/100. After the flop: A bets 100, B is all-in
    // for 125, C calls, D is all-in for 200, E calls. A now faces 100 more,
    // a full raise, and may raise again; C faces only 75 more and may not.
    Game game({5000, 225, 5000, 300, 5000}, 4, {}, "", 50, 100);
    CHECK(game.call());   // 2
    CHECK(game.call());   // 3
    CHECK(game.call());   // 4
    CHECK(game.call());   // 0, small blind
    CHECK(game.check());  // 1, big blind
    CHECK(game.hand.street() == Street::Flop);
    CHECK(game.act(Move::Bet, 100));  // A = 0
    CHECK(game.raise(125));           // B = 1, all-in
    CHECK(game.call());               // C = 2
    CHECK(game.raise(200));           // D = 3, all-in
    CHECK(game.call());               // E = 4
    CHECK_EQ(game.hand.to_act(), 0);
    Choices choices = game.hand.choices();
    CHECK(choices.can_raise);
    CHECK_EQ(choices.to_call, 100);
    CHECK_EQ(choices.min_raise_to, 300);
    CHECK(game.call());
    CHECK_EQ(game.hand.to_act(), 2);
    choices = game.hand.choices();
    CHECK_EQ(choices.to_call, 75);
    CHECK(!choices.can_raise);
}

TEST(side_pots) {
    // Stacks 100, 300, 500; all three get their chips in before the flop.
    // The shortest stack has the best hand, the middle one the second best.
    Game game({100, 300, 500}, 0, {"As Ah", "Ks Kh", "Qs Qh"}, kDryBoard);
    CHECK(game.raise(100));
    CHECK(game.raise(300));
    // The last player cannot raise: nobody could answer.
    CHECK(!game.hand.choices().can_raise);
    CHECK(game.call());
    CHECK(game.hand.finished());
    CHECK_EQ(game.won(0), 300);   // main pot: 100 from each
    CHECK_EQ(game.won(1), 400);   // side pot: 200 from two players
    CHECK_EQ(game.won(2), 0);
    CHECK_EQ(game.stack(0), 300);
    CHECK_EQ(game.stack(1), 400);
    CHECK_EQ(game.stack(2), 200);
    // All-in before the river: every hand is shown, nothing is thrown away.
    CHECK_EQ(game.seats_of<event::Showed>().size(), 3);
    CHECK(game.seats_of<event::Mucked>().empty());
    CHECK_EQ(game.hand.board().size(), 5);
}

TEST(biggest_stack_wins_every_pot) {
    Game game({100, 300, 500}, 0, {"Qs Qh", "Ks Kh", "As Ah"}, kDryBoard);
    CHECK(game.raise(100));
    CHECK(game.raise(300));
    CHECK(game.call());
    CHECK_EQ(game.won(2), 700);
    CHECK_EQ(game.stack(0), 0);
    CHECK_EQ(game.stack(1), 0);
    CHECK_EQ(game.stack(2), 900);
}

TEST(uncalled_part_of_an_all_in_comes_back) {
    Game game({500, 100}, 0, {"Ks Kh", "As Ah"}, kDryBoard);
    CHECK(game.raise(500));
    CHECK(game.call());
    CHECK(game.hand.finished());
    CHECK_EQ(game.returned(0), 400);
    CHECK_EQ(game.won(1), 200);
    CHECK_EQ(game.stack(0), 400);
    CHECK_EQ(game.stack(1), 200);
}

TEST(split_pot_and_the_odd_chip) {
    // The small blind folds a chip into the pot; the other two tie for 5 chips.
    Game game({100, 100, 100}, 0, {"As Kd", "4h 5d", "Ah Kc"}, kDryBoard);
    CHECK(game.call());   // seat 0
    CHECK(game.fold());   // seat 1
    CHECK(game.check());  // seat 2
    for (int street = 0; street < 3; ++street) {
        CHECK(game.check());  // seat 2
        CHECK(game.check());  // seat 0
    }
    CHECK(game.hand.finished());
    // The chip that does not divide goes to the first winner left of the button.
    CHECK_EQ(game.won(2), 3);
    CHECK_EQ(game.won(0), 2);
    CHECK_EQ(game.stack(0), 100);
    CHECK_EQ(game.stack(1), 99);
    CHECK_EQ(game.stack(2), 101);
    CHECK_EQ(game.seats_of<event::Showed>().size(), 2);
}

TEST(river_bettor_shows_first_and_loser_mucks) {
    Game game({100, 100}, 0, {"Ks Kh", "As Ah"}, kDryBoard);
    CHECK(game.call());
    CHECK(game.check());
    for (int street = 0; street < 2; ++street) {
        CHECK(game.check());
        CHECK(game.check());
    }
    CHECK(game.hand.street() == Street::River);
    CHECK(game.act(Move::Bet, 10));  // seat 1
    CHECK(game.call());              // seat 0
    CHECK(game.hand.finished());
    CHECK(game.seats_of<event::Showed>() == std::vector<int>({1}));
    CHECK(game.seats_of<event::Mucked>() == std::vector<int>({0}));
    CHECK_EQ(game.won(1), 24);
    CHECK_EQ(game.stack(1), 112);
    CHECK_EQ(game.stack(0), 88);
}

TEST(caller_with_the_better_hand_shows_too) {
    Game game({100, 100}, 0, {"As Ah", "Ks Kh"}, kDryBoard);
    CHECK(game.call());
    CHECK(game.check());
    for (int street = 0; street < 2; ++street) {
        CHECK(game.check());
        CHECK(game.check());
    }
    CHECK(game.act(Move::Bet, 10));
    CHECK(game.call());
    CHECK(game.seats_of<event::Showed>() == std::vector<int>({1, 0}));
    CHECK(game.seats_of<event::Mucked>().empty());
    CHECK_EQ(game.won(0), 24);
}

TEST(checked_down_first_left_of_button_shows_first) {
    Game game({100, 100, 100}, 0, {"As Ah", "Ks Kh", "Qs Qh"}, kDryBoard);
    CHECK(game.call());
    CHECK(game.call());
    CHECK(game.check());
    for (int street = 0; street < 3; ++street) {
        CHECK(game.check());
        CHECK(game.check());
        CHECK(game.check());
    }
    CHECK(game.hand.finished());
    // Seat 1 shows kings, seat 2 cannot beat them, seat 0 shows aces.
    CHECK(game.seats_of<event::Showed>() == std::vector<int>({1, 0}));
    CHECK(game.seats_of<event::Mucked>() == std::vector<int>({2}));
    CHECK_EQ(game.won(0), 6);
}

TEST(blind_bigger_than_the_stack) {
    // The big blind has a single chip. The small blind still has to complete
    // to a full big blind, and then gets the unmatched chip back.
    Game game({100, 1}, 0, {"Ks Kh", "As Ah"}, kDryBoard);
    CHECK_EQ(game.hand.to_act(), 0);
    CHECK_EQ(game.hand.choices().to_call, 1);
    CHECK(!game.hand.choices().can_raise);
    CHECK(game.call());
    CHECK(game.hand.finished());
    CHECK_EQ(game.returned(0), 1);
    CHECK_EQ(game.won(1), 2);
    CHECK_EQ(game.stack(0), 99);
    CHECK_EQ(game.stack(1), 2);
}

TEST(rest_of_board_after_an_early_end) {
    Game game({100, 100}, 0, {"Ks Kh", "As Ah"}, kDryBoard);
    CHECK(game.call());
    CHECK(game.check());
    CHECK(game.act(Move::Bet, 5));
    CHECK(game.fold());
    CHECK(game.hand.finished());
    CHECK_EQ(game.hand.board().size(), 3);
    const std::vector<Card> rest = game.hand.rest_of_board();
    CHECK(rest == parse_cards("Jc 3s"));
    CHECK(!game.check());
}

// Thousands of hands with random stacks and random legal moves. Whatever
// happens, chips must neither appear nor vanish and the hand must end.
TEST(random_play_keeps_the_chips) {
    std::mt19937 random(20261115);
    const auto pick = [&](std::uint32_t bound) { return static_cast<std::uint32_t>(random() % bound); };
    int showdowns = 0;
    int side_pots = 0;
    int returns = 0;
    long long all_moves = 0;

    for (int round = 0; round < 20000; ++round) {
        HandSetup setup;
        setup.small_blind = 1;
        setup.big_blind = 2;
        const std::size_t count = 2 + pick(5);
        Chips before = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const Chips stack = 1 + pick(pick(2) == 0 ? 12 : 400);
            setup.players.push_back({static_cast<int>(i), stack});
            before += stack;
        }
        setup.button = pick(static_cast<std::uint32_t>(count));
        setup.deck = shuffled_deck(pick);

        Hand hand(std::move(setup));
        Chips won = 0;
        int moves = 0;
        while (!hand.finished() && moves < 500) {
            const Choices choices = hand.choices();
            const std::uint32_t roll = pick(100);
            bool ok = false;
            if (choices.can_raise && roll < 30) {
                const Chips span = choices.max_raise_to - choices.min_raise_to;
                const Chips to = pick(4) == 0 ? choices.max_raise_to
                                              : choices.min_raise_to + pick(static_cast<std::uint32_t>(span + 1));
                if (choices.min_raise_to > 1) CHECK(!hand.act(Action{Move::Raise, choices.min_raise_to - 1}));
                ok = hand.act(Action{Move::Raise, to});
            } else if (roll < 45 && !choices.can_check) {
                ok = hand.act(Action{Move::Fold});
            } else if (choices.can_check) {
                CHECK(!hand.act(Action{Move::Call}));
                ok = hand.act(Action{Move::Check});
            } else {
                CHECK(!hand.act(Action{Move::Check}));
                ok = hand.act(Action{Move::Call});
            }
            CHECK(ok);
            ++moves;
        }
        CHECK(hand.finished());

        Chips after = 0;
        for (const HandPlayer& player : hand.players()) {
            CHECK(player.stack >= 0);
            CHECK_EQ(player.bet, 0);
            after += player.stack;
        }
        bool side = false;
        std::vector<Chips> won_by(count, 0);
        for (const Event& e : hand.take_events()) {
            if (const auto* win = std::get_if<event::Won>(&e)) {
                won += win->amount;
                won_by[static_cast<std::size_t>(win->seat)] += win->amount;
                if (win->pot > 0) side = true;
            }
            if (std::holds_alternative<event::Returned>(e)) ++returns;
        }
        CHECK_EQ(after, before);
        CHECK_EQ(won, hand.pot());
        if (hand.board().size() == 5) {
            ++showdowns;
            // Whoever holds the best hand at the table always wins something,
            // and nobody who folded wins anything.
            std::vector<HandValue> values(count);
            HandValue best;
            for (std::size_t i = 0; i < count; ++i) {
                const HandPlayer& player = hand.players()[i];
                if (player.folded) {
                    CHECK_EQ(won_by[i], 0);
                    continue;
                }
                std::vector<Card> seven = hand.board();
                seven.insert(seven.end(), player.cards.begin(), player.cards.end());
                values[i] = evaluate(seven);
                best = std::max(best, values[i]);
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (!hand.players()[i].folded && values[i] == best) CHECK(won_by[i] > 0);
            }
        }
        if (side) ++side_pots;
        all_moves += moves;
        if (check::failures() > 20) return;
    }
    // Make sure the random play really reaches the hard cases.
    CHECK(showdowns > 5000);
    CHECK(side_pots > 1000);
    CHECK(returns > 1000);
    CHECK(all_moves > 100000);
    std::printf("  20000 hands: %lld moves, %d showdowns, %d with side pots, %d returned bets\n", all_moves,
                showdowns, side_pots, returns);
}
