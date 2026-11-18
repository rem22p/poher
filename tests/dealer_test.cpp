#include <algorithm>

#include "bots/simple_bot.hpp"
#include "check.hpp"
#include "host/dealer.hpp"

using namespace poher;
using game::Chips;
using host::Dealer;
using host::Pace;
using host::Player;
using host::Stage;
using host::TableView;

namespace {

const game::Stakes kStakes{1, 2, 40, 200};

// A table with a person in seat 0 and bots in the other seats.
struct Room {
    Dealer dealer;
    game::Random random{11};
    Chips bought = 0;   // chips brought to the table
    Chips taken = 0;    // chips carried away
    std::int64_t now = 0;

    Room(int seats, Pace pace, std::uint64_t seed = 5) : dealer(kStakes, seats, seed, pace) {
        dealer.sit(0, Player{"You", "01", false}, 200);
        bought += 200;
        for (int seat = 1; seat < seats; ++seat) {
            dealer.sit(seat, Player{"Bot" + std::to_string(seat), "02", true}, 150);
            bought += 150;
        }
    }

    void count_notices() {
        for (const host::Notice& notice : dealer.take_notices()) {
            if (const auto* added = std::get_if<host::notice::ChipsAdded>(&notice)) bought += added->amount;
            if (const auto* left = std::get_if<host::notice::Left>(&notice)) taken += left->chips;
        }
    }

    // Moves time on by `step` milliseconds; the person plays like a plain bot.
    void advance(int step, bool play = true) {
        now += step;
        dealer.tick(now);
        // Out of chips: buy in again, or the person would only watch from then on.
        if (play && dealer.table().seat(0).taken && !dealer.table().seat(0).in_hand && dealer.stack(0) == 0) {
            dealer.want_chips(0, 200, 200);
        }
        if (play && dealer.view(0).choices) {
            const game::Action action = bots::simple_move(bots::situation_of(*dealer.table().hand()), random);
            CHECK(dealer.act(0, action));
        }
        count_notices();
    }

    Chips on_table() const {
        Chips sum = 0;
        for (int seat = 0; seat < dealer.table().seat_count(); ++seat) sum += dealer.table().seat(seat).stack;
        return sum;
    }
};

}  // namespace

TEST(dealer_plays_on_and_keeps_the_chips) {
    Room room(6, Pace::none());
    int own_cards_seen = 0;
    for (int step = 0; step < 4000; ++step) {
        room.advance(10);
        const TableView view = room.dealer.view(0);
        Chips in_front = 0;
        for (std::size_t seat = 0; seat < view.seats.size(); ++seat) {
            const host::SeatView& shown = view.seats[seat];
            CHECK(shown.stack >= 0);
            CHECK(shown.bet >= 0);
            in_front += shown.bet;
            // Other people's cards are visible only once they are turned over.
            if (seat != 0 && shown.cards) CHECK(shown.shown);
            if (seat == 0 && shown.cards) ++own_cards_seen;
        }
        CHECK(view.pot >= in_front);
        // The person is asked to move only when it is their turn.
        if (view.choices) CHECK_EQ(view.acting, 0);
        if (check::failures() > 20) return;
    }
    // Without pauses a hand that ends on the person's move is cleared at once,
    // so their cards are on view only while the hand waits for them.
    CHECK(own_cards_seen > 1000);
    CHECK(room.dealer.view(0).hand_number > 300);

    room.dealer.finish_now();
    room.count_notices();
    CHECK(!room.dealer.table().playing());
    CHECK_EQ(room.on_table() + room.taken, room.bought);
    std::printf("  %d hands, own cards on view %d times\n", room.dealer.view(0).hand_number, own_cards_seen);
}

TEST(dealer_shows_nothing_to_a_spectator) {
    Room room(4, Pace::none());
    for (int step = 0; step < 300; ++step) {
        room.advance(10);
        const TableView view = room.dealer.view(-1);
        CHECK(!view.choices);
        for (const host::SeatView& seat : view.seats) {
            if (seat.cards) CHECK(seat.shown);
        }
    }
}

TEST(dealer_takes_its_time) {
    const Pace pace;
    Room room(3, pace);

    // Nothing is dealt before the first pause is over; it starts with the first tick.
    room.advance(0);
    room.advance(pace.first_hand - 100);
    CHECK(room.dealer.view(0).stage == Stage::Idle);
    room.advance(200);
    CHECK(room.dealer.view(0).stage == Stage::Playing);

    std::int64_t bot_turn_since = -1;
    int bot_seat = -1;
    std::int64_t fastest_bot = 1 << 30;
    std::int64_t over_since = -1;
    std::int64_t shortest_break = 1 << 30;
    std::int64_t longest_break = 0;
    int hand = room.dealer.view(0).hand_number;
    for (int step = 0; step < 40000 && room.dealer.view(0).hand_number < 12; ++step) {
        room.advance(50);
        const TableView view = room.dealer.view(0);
        // How long a bot holds the turn.
        if (view.acting > 0 && view.acting != bot_seat) {
            bot_seat = view.acting;
            bot_turn_since = room.now;
        } else if (view.acting != bot_seat && bot_seat > 0) {
            fastest_bot = std::min(fastest_bot, room.now - bot_turn_since);
            bot_seat = -1;
        }
        // How long the result of a hand stays before the next one.
        if (view.stage == Stage::Over && over_since < 0) over_since = room.now;
        if (view.hand_number != hand) {
            if (over_since >= 0) {
                shortest_break = std::min(shortest_break, room.now - over_since);
                longest_break = std::max(longest_break, room.now - over_since);
            }
            over_since = -1;
            hand = view.hand_number;
        }
    }
    CHECK(room.dealer.view(0).hand_number >= 12);
    // The clock of the test moves in steps of 50 ms, so allow that much slack.
    CHECK(fastest_bot >= pace.think_min - 50);
    CHECK(shortest_break >= pace.after_hand - 50);
    CHECK(longest_break <= pace.after_showdown + 50);
    std::printf("  fastest bot move %lld ms, break between hands %lld..%lld ms\n",
                static_cast<long long>(fastest_bot), static_cast<long long>(shortest_break),
                static_cast<long long>(longest_break));
}

TEST(dealer_lets_a_player_leave_in_the_middle_of_a_hand) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        Room room(4, Pace::none(), seed);
        // Play a little, then get up at a random moment.
        const int steps = 5 + static_cast<int>(room.random.below(60));
        for (int step = 0; step < steps; ++step) room.advance(10);
        room.dealer.leave(0);
        room.dealer.finish_now();
        room.count_notices();

        CHECK(!room.dealer.table().seat(0).taken);
        CHECK(!room.dealer.table().playing());
        CHECK(room.dealer.stopped());
        CHECK_EQ(room.on_table() + room.taken, room.bought);
        // No more hands after that.
        const int hands = room.dealer.view(-1).hand_number;
        room.advance(1000, false);
        room.advance(10000, false);
        CHECK_EQ(room.dealer.view(-1).hand_number, hands);
    }
}

TEST(dealer_adds_chips_between_hands) {
    Room room(3, Pace::none());
    // Wait for a hand in which the person has put chips in and still has a move.
    for (int step = 0; step < 2000; ++step) {
        room.advance(10, false);
        const TableView view = room.dealer.view(0);
        if (!view.choices) continue;
        if (view.seats[0].stack < 200 - 1) break;
        CHECK(room.dealer.act(0, game::Action{view.choices->can_check ? game::Move::Check : game::Move::Call}));
    }
    TableView view = room.dealer.view(0);
    CHECK(view.choices.has_value());
    const Chips before = room.bought;

    // Asked for in the middle of the hand: nothing happens yet.
    room.dealer.want_chips(0, 200, 5);
    room.count_notices();
    CHECK_EQ(room.bought, before);
    CHECK(room.dealer.view(0).chips_coming > 0);

    // Fold; the hand ends and at most the 5 chips asked for are added.
    const int hand = view.hand_number;
    CHECK(room.dealer.act(0, game::Action{game::Move::Fold}));
    Chips mine = 0;
    for (int step = 0; step < 2000; ++step) {
        for (const host::Notice& notice : room.dealer.take_notices()) {
            const auto* added = std::get_if<host::notice::ChipsAdded>(&notice);
            if (added != nullptr && added->seat == 0) mine += added->amount;
        }
        if (room.dealer.view(0).hand_number != hand) break;
        room.now += 10;
        room.dealer.tick(room.now);
    }
    CHECK(mine > 0);
    CHECK(mine <= 5);
    CHECK_EQ(room.dealer.view(0).chips_coming, 0);
}

namespace {

// Two people and two bots, with a clock on the moves of the people.
struct ClockRoom {
    Dealer dealer{kStakes, 4, 3, Pace::none()};
    std::int64_t now = 0;

    explicit ClockRoom(host::TurnClock clock) {
        dealer.set_clock(clock);
        dealer.sit(0, Player{"Ann", "01", false}, 200);
        dealer.sit(1, Player{"Bob", "03", false}, 200);
        dealer.sit(2, Player{"Bot2", "02", true}, 200);
        dealer.sit(3, Player{"Bot3", "02", true}, 200);
    }

    void advance(int step) {
        now += step;
        dealer.tick(now);
    }
    // Moves time on until one of the people has to move; -1 if nobody had to for a long time.
    int wait_for_person() {
        for (int i = 0; i < 20000; ++i) {
            const int acting = dealer.view(0).acting;
            if (acting == 0 || acting == 1) return acting;
            advance(10);
        }
        return -1;
    }
};

}  // namespace

TEST(the_clock_moves_for_whoever_runs_out_of_time) {
    host::TurnClock clock;
    clock.turn_ms = 15000;
    ClockRoom room(clock);
    const int seat = room.wait_for_person();
    CHECK(seat >= 0);
    if (seat < 0) return;
    TableView view = room.dealer.view(seat);
    CHECK_EQ(view.turn_ms, 15000);
    CHECK_EQ(view.bank_ms, 60000);
    const int hand = view.hand_number;

    // The move time runs out first, then the reserve.
    room.advance(20000);
    view = room.dealer.view(seat);
    CHECK_EQ(view.acting, seat);
    CHECK_EQ(view.turn_ms, 0);
    CHECK_EQ(view.bank_ms, 55000);
    // The reserve and two more seconds, and the move is made for the player.
    room.advance(55000 + 1999);
    CHECK_EQ(room.dealer.view(seat).acting, seat);
    room.advance(2);
    view = room.dealer.view(seat);
    CHECK(view.acting != seat || view.hand_number != hand);
    // A check when it was free, a fold otherwise.
    const host::Note note = view.seats[static_cast<std::size_t>(seat)].note;
    CHECK(note == host::Note::Fold || note == host::Note::Check || view.hand_number != hand);
}

TEST(two_missed_moves_and_the_player_sits_out) {
    host::TurnClock clock;
    clock.turn_ms = 1000;
    clock.bank_ms = 0;
    clock.bank_back_ms = 0;
    clock.grace_ms = 0;
    clock.away_seat_ms = 60000;
    ClockRoom room(clock);
    // Bob plays at once; Ann never moves.
    for (int i = 0; i < 200000 && !room.dealer.table().seat(0).away; ++i) {
        room.advance(10);
        if (room.dealer.view(1).choices) {
            CHECK(room.dealer.act(1, game::Action{room.dealer.view(1).choices->can_check ? game::Move::Check
                                                                                      : game::Move::Fold}));
        }
    }
    CHECK(room.dealer.table().seat(0).away);
    CHECK(room.dealer.view(1).seats[0].away);
    // Sitting out: no cards, until a key is pressed.
    const int hands = room.dealer.view(0).hand_number;
    for (int i = 0; i < 3000; ++i) {
        room.advance(10);
        if (room.dealer.view(1).choices) {
            room.dealer.act(1, game::Action{room.dealer.view(1).choices->can_check ? game::Move::Check
                                                                                 : game::Move::Fold});
        }
    }
    CHECK(room.dealer.view(0).hand_number > hands + 1);
    CHECK(!room.dealer.table().seat(0).in_hand);
    room.dealer.come_back(0);
    CHECK(!room.dealer.table().seat(0).away);

    // Away for longer than the seat is kept: the player is got up with their chips.
    room.dealer.take_notices();
    for (int i = 0; i < 40000 && room.dealer.table().seat(0).taken; ++i) {
        room.advance(100);
        if (room.dealer.view(1).choices) {
            room.dealer.act(1, game::Action{room.dealer.view(1).choices->can_check ? game::Move::Check
                                                                                 : game::Move::Fold});
        }
    }
    CHECK(!room.dealer.table().seat(0).taken);
    bool left = false;
    for (const host::Notice& notice : room.dealer.take_notices()) {
        if (const auto* gone = std::get_if<host::notice::Left>(&notice)) left = left || gone->seat == 0;
    }
    CHECK(left);
}

TEST(no_clock_for_a_person_alone_with_bots) {
    host::TurnClock clock;
    clock.turn_ms = 1000;
    Dealer dealer(kStakes, 3, 4, Pace::none());
    dealer.set_clock(clock);
    dealer.sit(0, Player{"Ann", "01", false}, 200);
    dealer.sit(1, Player{"Bot1", "02", true}, 200);
    dealer.sit(2, Player{"Bot2", "02", true}, 200);
    std::int64_t now = 0;
    for (int i = 0; i < 5000 && dealer.view(0).acting != 0; ++i) dealer.tick(now += 10);
    CHECK_EQ(dealer.view(0).acting, 0);
    CHECK_EQ(dealer.view(0).turn_ms, -1);
    dealer.tick(now += 10 * 60 * 1000);
    CHECK_EQ(dealer.view(0).acting, 0);
}

TEST(an_offline_player_checks_or_folds_and_is_got_up_later) {
    host::TurnClock clock;
    clock.turn_ms = 30000;
    ClockRoom room(clock);
    const int seat = room.wait_for_person();
    CHECK(seat >= 0);
    if (seat < 0) return;
    const int other = 1 - seat;
    room.dealer.set_offline(seat, true);
    room.advance(10);
    CHECK(room.dealer.view(other).acting != seat);
    CHECK(room.dealer.view(other).seats[static_cast<std::size_t>(seat)].offline);
    // Not dealt in while offline.
    for (int i = 0; i < 100000 && room.dealer.view(other).hand_number < 3; ++i) {
        room.advance(10);
        if (room.dealer.view(other).choices) {
            room.dealer.act(other, game::Action{room.dealer.view(other).choices->can_check ? game::Move::Check
                                                                                         : game::Move::Fold});
        }
        if (room.dealer.view(other).hand_number >= 2) CHECK(!room.dealer.table().seat(seat).in_hand);
    }
    // Back in time: dealt in again.
    room.dealer.set_offline(seat, false);
    CHECK(!room.dealer.table().seat(seat).away);
    // Gone for good: the seat is freed after three minutes.
    room.dealer.set_offline(seat, true);
    room.advance(3 * 60 * 1000 + 10);
    for (int i = 0; i < 1000 && room.dealer.table().seat(seat).taken; ++i) {
        room.advance(10);
        if (room.dealer.view(other).choices) {
            room.dealer.act(other, game::Action{game::Move::Fold});
        }
    }
    CHECK(!room.dealer.table().seat(seat).taken);
}
