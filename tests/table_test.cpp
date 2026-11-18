#include <cmath>

#include "bots/simple_bot.hpp"
#include "check.hpp"
#include "game/equity.hpp"
#include "game/table.hpp"

using namespace poher::game;
namespace bots = poher::bots;

namespace {

const Stakes kStakes{1, 2, 40, 200};

std::vector<Card> deck_from(Random& random) {
    return shuffled_deck([&](std::uint32_t bound) { return random.below(bound); });
}

// Plays the shortest possible hand: everyone folds as soon as they can.
void fold_out(Table& table) {
    while (!table.hand()->finished()) table.hand()->act(Action{Move::Fold});
    table.settle();
}

double equity(const char* cards, const char* board, int opponents) {
    Random random(99);
    const std::vector<Card> hole = parse_cards(cards);
    return estimate_equity({hole[0], hole[1]}, parse_cards(board), opponents, 40000, random);
}

bool near(double actual, double expected, double margin) {
    if (std::fabs(actual - expected) <= margin) return true;
    std::printf("  got %.3f, expected %.3f +- %.3f\n", actual, expected, margin);
    return false;
}

}  // namespace

TEST(equity_of_known_spots) {
    CHECK(near(equity("As Ah", "", 1), 0.852, 0.012));
    CHECK(near(equity("7s 2h", "", 1), 0.346, 0.012));
    CHECK(near(equity("As Ks", "", 1), 0.670, 0.012));
    CHECK(near(equity("As Ah", "", 5), 0.493, 0.015));
    // The best possible hand on the river always wins.
    CHECK(near(equity("As Ks", "Qs Js Ts 2d 2c", 3), 1.0, 0.0));
    // The board plays for everybody: the pot is split.
    CHECK(near(equity("2d 3c", "As Ks Qs Js Ts", 1), 0.5, 0.0));
    // A flush draw with two cards to come gets there about a third of the time.
    CHECK(near(equity("As 2s", "Ks 7s 8d", 1), 0.63, 0.03));
}

TEST(table_seats_and_buy_in_limits) {
    Table table(kStakes, 4);
    CHECK(!table.sit(0, 39));
    CHECK(!table.sit(0, 201));
    CHECK(!table.sit(4, 100));
    CHECK(table.sit(0, 100));
    CHECK(!table.sit(0, 100));
    CHECK(!table.can_deal());
    CHECK(table.sit(2, 150));
    CHECK(table.can_deal());

    CHECK(!table.add_chips(0, 101));
    CHECK(table.add_chips(0, 100));
    CHECK_EQ(table.seat(0).stack, 200);

    const auto chips = table.leave(2);
    CHECK(chips.has_value() && *chips == 150);
    CHECK(!table.seat(2).taken);
    CHECK(!table.can_deal());
}

TEST(table_button_skips_empty_and_away_seats) {
    Table table(kStakes, 6);
    for (int seat : {0, 2, 3, 5}) CHECK(table.sit(seat, 100));
    Random random(1);

    CHECK(table.deal(deck_from(random)));
    CHECK_EQ(table.button(), 0);
    CHECK(table.playing());
    CHECK(!table.can_deal());
    CHECK(table.seat(3).in_hand);
    CHECK(!table.seat(1).in_hand);
    // Four players: the small blind is seat 2, the big blind seat 3, seat 5 moves first.
    CHECK_EQ(table.seat_to_act(), 5);
    CHECK(!table.add_chips(0, 10));
    fold_out(table);
    CHECK(!table.playing());
    CHECK_EQ(table.seat_to_act(), -1);

    CHECK(table.deal(deck_from(random)));
    CHECK_EQ(table.button(), 2);
    fold_out(table);

    table.set_away(3, true);
    CHECK(table.deal(deck_from(random)));
    CHECK_EQ(table.button(), 5);
    CHECK(!table.seat(3).in_hand);
    fold_out(table);

    CHECK(table.deal(deck_from(random)));
    CHECK_EQ(table.button(), 0);
    fold_out(table);
}

TEST(table_leaving_in_the_middle_of_a_hand) {
    Table table(kStakes, 3);
    for (int seat : {0, 1, 2}) CHECK(table.sit(seat, 100));
    Random random(2);
    CHECK(table.deal(deck_from(random)));

    const int leaver = table.seat_to_act();
    CHECK(!table.leave(leaver).has_value());
    CHECK(table.seat(leaver).taken);
    CHECK(table.seat(leaver).leaving);
    fold_out(table);

    CHECK(!table.seat(leaver).taken);
    // With two players left the game goes on.
    CHECK(table.can_deal());
}

// Six plain bots play a long session with people coming, going, sitting out
// and buying more chips. Chips bought must equal chips on the table plus
// chips taken away.
TEST(bots_play_a_long_session) {
    Table table(kStakes, 6);
    Random random(7);
    Chips bought = 0;
    Chips taken = 0;
    int hands = 0;
    int showdowns = 0;

    for (int round = 0; round < 2500; ++round) {
        for (int seat = 0; seat < table.seat_count(); ++seat) {
            const Seat& place = table.seat(seat);
            if (!place.taken) {
                if (random.chance(0.3)) {
                    const Chips buy_in = 40 + random.below(161);
                    CHECK(table.sit(seat, buy_in));
                    bought += buy_in;
                }
            } else if (place.stack == 0) {
                if (random.chance(0.7)) {
                    CHECK(table.add_chips(seat, 100));
                    bought += 100;
                } else {
                    taken += *table.leave(seat);
                }
            } else if (random.chance(0.02)) {
                taken += *table.leave(seat);
            } else if (random.chance(0.05)) {
                table.set_away(seat, !place.away);
            }
        }
        if (!table.can_deal()) continue;

        CHECK(table.deal(deck_from(random)));
        CHECK(table.seat(table.button()).in_hand);
        ++hands;
        Hand& hand = *table.hand();
        while (!hand.finished()) {
            if (random.chance(0.01)) {
                // Somebody walks away in the middle of the hand: their cards are folded.
                CHECK(!table.leave(table.seat_to_act()).has_value());
                CHECK(hand.act(Action{Move::Fold}));
                continue;
            }
            const Action action = bots::simple_move(bots::situation_of(hand), random);
            CHECK(hand.act(action));
        }
        if (hand.board().size() == 5) ++showdowns;
        for (const Departure& gone : table.settle()) taken += gone.chips;
        if (check::failures() > 20) return;
    }

    Chips on_table = 0;
    for (int seat = 0; seat < table.seat_count(); ++seat) on_table += table.seat(seat).stack;
    CHECK_EQ(on_table + taken, bought);
    CHECK(hands > 2000);
    CHECK(showdowns > 300);
    std::printf("  %d hands, %d went to the river\n", hands, showdowns);
}
