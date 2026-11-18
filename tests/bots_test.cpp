#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "bots/brain.hpp"
#include "bots/preflop.hpp"
#include "bots/ranges.hpp"
#include "bots/talk.hpp"
#include "check.hpp"
#include "game/outs.hpp"
#include "host/dealer.hpp"
#include "host/roster.hpp"

using namespace poher;
using game::Card;
using game::Chips;
using game::parse_cards;

namespace {

std::array<Card, 2> two(const char* text) {
    const std::vector<Card> cards = parse_cards(text);
    return {cards.at(0), cards.at(1)};
}

game::Outs outs(const char* hand, const char* board) {
    return game::count_outs(two(hand), parse_cards(board));
}

}  // namespace

TEST(starting_hands_are_ranked) {
    CHECK(bots::start_strength(two("As Ad")) > 0.99);
    CHECK(bots::start_strength(two("7s 2d")) < 0.03);
    CHECK(bots::start_strength(two("As Ks")) > bots::start_strength(two("As Kd")));
    CHECK(bots::start_strength(two("As Kd")) > bots::start_strength(two("Ts 9d")));
    CHECK(bots::start_strength(two("Kd As")) == bots::start_strength(two("As Kd")));
    // Every hand has a place, and the places are spread over the whole scale.
    int top_fifth = 0;
    int total = 0;
    const auto deck = game::fresh_deck();
    for (std::size_t i = 0; i < deck.size(); ++i) {
        for (std::size_t j = i + 1; j < deck.size(); ++j) {
            const double strength = bots::start_strength({deck[i], deck[j]});
            CHECK(strength >= 0 && strength <= 1);
            if (strength >= 0.8) ++top_fifth;
            ++total;
        }
    }
    CHECK_EQ(total, 1326);
    CHECK(std::abs(top_fifth - 1326 / 5) < 40);
}

TEST(outs_are_counted) {
    // Four to a flush: nine cards.
    game::Outs draw = outs("As 7s", "Ks 9s 2d");
    CHECK_EQ(draw.to_flush, 9);
    CHECK(draw.flush_draw);
    CHECK_EQ(draw.unseen, 47);
    // Plus three aces for the top pair.
    CHECK_EQ(draw.count, 12);
    CHECK_EQ(draw.to_pair, 3);

    // Open at both ends: eight cards.
    draw = outs("9c 8d", "7s 6h 2d");
    CHECK_EQ(draw.to_straight, 8);
    CHECK(draw.open_ended && !draw.gutshot);
    // A hole in the middle: four cards.
    draw = outs("9c 8d", "6s 5h 2d");
    CHECK_EQ(draw.to_straight, 4);
    CHECK(draw.gutshot && !draw.open_ended);

    // A pair: two cards for three of a kind, three for two pair.
    CHECK_EQ(outs("Ac Kd", "Ks 7h 2d").count, 5);
    // The board pairing itself helps everybody: not an out.
    CHECK_EQ(outs("Ac Ad", "Ks 7h 2d").count, 2);
    // Three of a kind on the flop: one card for four, six to pair the board.
    CHECK_EQ(outs("7c 7d", "Ks 7h 2d").count, 7);
    // Two overcards: six cards for a top pair.
    CHECK_EQ(outs("Ac Qd", "9s 7h 2d").count, 6);
    // A low card pairing is not worth waiting for.
    CHECK_EQ(outs("4c 3d", "Ks Qh 9d").count, 0);

    // Only on the flop and the turn.
    CHECK_EQ(outs("As 7s", "Ks 9s 2d 3c 4h").count, 0);
    CHECK_EQ(game::count_outs(two("As 7s"), {}).count, 0);

    const game::Outs nine = outs("As 7s", "Ks 9s 2d 3c");
    CHECK_EQ(nine.unseen, 46);
    CHECK(std::abs(nine.next_card() - 12.0 / 46) < 1e-9);
    // Eight outs with two cards to come: about one time in three.
    const game::Outs flop = outs("5c 4d", "7s 6h Kd");
    CHECK_EQ(flop.count, 8);
    CHECK(std::abs(flop.by_the_river(2) - 0.315) < 0.01);
}

TEST(chances_depend_on_what_the_opponent_holds) {
    game::Random random(3);
    const std::vector<Card> board = parse_cards("Ks 8d 3c");
    const std::array<Card, 2> top_pair = two("Kd Jc");

    const bots::Foe anyone;
    const double against_anyone = bots::equity_against(top_pair, board, std::span(&anyone, 1), 3000, random);
    CHECK(against_anyone > 0.8);

    // Someone who raised before the flop and keeps betting has much more.
    bots::Foe raiser;
    raiser.range = 0.15;
    raiser.story = 2.5;
    const double against_raiser = bots::equity_against(top_pair, board, std::span(&raiser, 1), 3000, random);
    CHECK(against_raiser < against_anyone - 0.15);
    CHECK(against_raiser > 0.3);

    // More opponents, less of the pot.
    const bots::Foe three[] = {anyone, anyone, anyone};
    CHECK(bots::equity_against(top_pair, board, three, 3000, random) < against_anyone - 0.1);

    // Before the flop: aces against the best tenth of hands.
    bots::Foe tight;
    tight.range = 0.1;
    const double aces = bots::equity_against(two("As Ad"), {}, std::span(&tight, 1), 3000, random);
    CHECK(aces > 0.75 && aces < 0.9);
    const double rags = bots::equity_against(two("9s 4d"), {}, std::span(&tight, 1), 3000, random);
    CHECK(rags < 0.33);

    // How well a hand holds on to a board.
    CHECK(bots::fade_rate(two("8s 8h"), board) == 0);          // three of a kind
    CHECK(bots::fade_rate(two("Kd Qc"), board) < 0.2);         // top pair, good kicker
    CHECK(bots::fade_rate(two("Kd 4c"), board) > 0.2);         // top pair, weak kicker
    CHECK(bots::fade_rate(two("8s 7s"), board) > bots::fade_rate(two("Kd 4c"), board));  // second pair
    CHECK(bots::fade_rate(two("Qs Jd"), board) > 1);           // nothing
    CHECK(bots::fade_rate(two("Qs Js"), parse_cards("Ks 8s 3c")) < 0.5);  // a flush draw
    CHECK(bots::fade_rate(two("Ad Ac"), parse_cards("Ks Kd 3c")) < 0.2);  // an overpair on a paired board
    CHECK(bots::fade_rate(two("Qd Jc"), parse_cards("Ks Kd 3c")) > 1);    // the board's pair is everybody's
}

TEST(the_record_of_a_hand_is_kept) {
    game::HandSetup setup;
    setup.small_blind = 1;
    setup.big_blind = 2;
    setup.players = {{0, 200}, {1, 200}, {2, 200}, {3, 200}};
    setup.button = 0;
    const auto fresh = game::fresh_deck();
    setup.deck.assign(fresh.begin(), fresh.end());
    game::Hand hand(setup);

    bots::Notebook notebook;
    bots::Watch watch(&notebook);
    const std::vector<std::string> names = {"A", "B", "C", "D"};
    watch.begin(hand, names);
    const auto pass_on = [&] {
        for (const game::Event& event : hand.take_events()) watch.see(event);
    };
    pass_on();
    CHECK_EQ(watch.pot(), 3);
    CHECK_EQ(watch.dealt(), 4);
    // Seat 1 is the small blind, seat 2 the big blind, seat 3 acts first, the button last.
    CHECK_EQ(watch.line(1)->after, 1);
    CHECK_EQ(watch.line(2)->after, 0);
    CHECK_EQ(watch.line(3)->after, 3);
    CHECK_EQ(watch.line(0)->after, 2);
    CHECK_EQ(watch.line(2)->stack, 198);

    CHECK(hand.act({game::Move::Call}));           // seat 3 just calls
    CHECK(hand.act({game::Move::Raise, 10}));      // the button raises
    CHECK(hand.act({game::Move::Fold}));           // small blind
    CHECK(hand.act({game::Move::Call}));           // big blind
    pass_on();
    CHECK_EQ(watch.limpers(), 1);
    CHECK_EQ(watch.raises(), 1);
    CHECK_EQ(watch.opener(), 0);
    CHECK(watch.line(0)->raised && watch.line(0)->level == 1);
    CHECK(watch.line(2)->entered && !watch.line(2)->raised && watch.line(2)->level == 1);
    CHECK(watch.line(1)->folded && !watch.line(1)->entered);
    CHECK(watch.line(3)->entered && watch.line(3)->level == 0);
    CHECK(hand.act({game::Move::Call}));           // seat 3 calls the raise
    pass_on();
    CHECK(watch.street() == game::Street::Flop);
    CHECK_EQ(watch.raises(), 0);
    CHECK_EQ(watch.pot(), 31);

    CHECK(hand.act({game::Move::Check}));          // big blind
    CHECK(hand.act({game::Move::Check}));          // seat 3
    CHECK(hand.act({game::Move::Raise, 20}));      // the button bets on
    CHECK(hand.act({game::Move::Raise, 60}));      // big blind check-raises
    CHECK(hand.act({game::Move::Fold}));           // seat 3
    pass_on();
    // A raise tells more than the expected bet of the one who raised before the flop.
    CHECK(watch.line(0)->story > 0.3);
    CHECK(watch.line(2)->story > watch.line(0)->story * 2);
    CHECK_EQ(watch.aggressor(), 2);
    CHECK_EQ(watch.raises(), 2);
    CHECK(hand.act({game::Move::Call}));
    pass_on();
    CHECK(watch.line(0)->story > 0.8);

    // What went into the notebook.
    CHECK_EQ(notebook.of("A").hands, 1);
    CHECK_EQ(notebook.of("A").entered, 1);
    CHECK_EQ(notebook.of("A").raised, 1);
    CHECK_EQ(notebook.of("B").entered, 0);
    CHECK_EQ(notebook.of("C").raised, 0);
    CHECK_EQ(notebook.of("C").bets, 1);
    CHECK_EQ(notebook.of("D").faced, 1);
    CHECK_EQ(notebook.of("D").gave_up, 1);
    CHECK_EQ(notebook.of("A").calls, 1);
}

// The nine play each other. No chip may get lost, and the characters must
// show in the numbers.
TEST(characters_play_differently) {
    std::map<std::string, bots::Notes> totals;
    std::map<std::string, double> won;
    game::Random random(8);
    const game::Stakes stakes{1, 2, 40, 200};
    for (int session = 0; session < 24; ++session) {
        std::vector<std::string> names;
        for (const host::Bot& bot : host::kBots) names.emplace_back(bot.name);
        for (std::size_t i = names.size(); i > 1; --i) std::swap(names[i - 1], names[random.below(static_cast<std::uint32_t>(i))]);
        names.resize(6);
        host::Dealer dealer(stakes, 6, random.next(), host::Pace::none());
        std::vector<Chips> paid(6, 200);
        for (int seat = 0; seat < 6; ++seat) dealer.sit(seat, host::Player{names[static_cast<std::size_t>(seat)], "01", true}, 200);
        int hands = 0;
        for (std::int64_t now = 1; hands < 250; ++now) {
            dealer.tick(now);
            for (const host::Notice& notice : dealer.take_notices()) {
                if (const auto* added = std::get_if<host::notice::ChipsAdded>(&notice)) paid[static_cast<std::size_t>(added->seat)] += added->amount;
                if (std::holds_alternative<host::notice::HandOver>(notice)) ++hands;
            }
        }
        dealer.finish_now();
        Chips on_table = 0;
        Chips brought = 0;
        for (int seat = 0; seat < 6; ++seat) {
            const std::string& name = names[static_cast<std::size_t>(seat)];
            on_table += dealer.table().seat(seat).stack;
            brought += paid[static_cast<std::size_t>(seat)];
            won[name] += static_cast<double>(dealer.table().seat(seat).stack - paid[static_cast<std::size_t>(seat)]) / 2;
            const bots::Notes* notes = dealer.notebook().find(name);
            CHECK(notes != nullptr);
            if (notes == nullptr) continue;
            bots::Notes& total = totals[name];
            total.hands += notes->hands;
            total.entered += notes->entered;
            total.raised += notes->raised;
            total.bets += notes->bets;
            total.calls += notes->calls;
            total.faced += notes->faced;
            total.gave_up += notes->gave_up;
        }
        CHECK_EQ(on_table, brought);
    }
    for (const host::Bot& bot : host::kBots) {
        const bots::Notes& notes = totals[std::string(bot.name)];
        std::printf("  %-6s enters %2.0f%%  raises %2.0f%%  bets per call %.2f  folds to a bet %2.0f%%  %+5.0f bb/100\n",
                    std::string(bot.name).c_str(), 100 * notes.enters(), 100 * notes.raises(), notes.pushes(),
                    100 * notes.gives_up(), 100 * won[std::string(bot.name)] / std::max(notes.hands, 1));
        CHECK(notes.hands > 1500);
    }
    const auto of = [&](const char* name) -> const bots::Notes& { return totals[name]; };
    // Vasya: in every other pot, almost never with a raise, calls rather than bets, hard to push out.
    CHECK(of("Vasya").enters() > 0.38);
    CHECK(of("Vasya").raises() < 0.12);
    CHECK(of("Vasya").pushes() < 0.9);
    CHECK(of("Vasya").gives_up() < of("Chips").gives_up() - 0.08);
    // Chad: raises more hands than the tight ones even play, and keeps betting.
    CHECK(of("Chad").raises() > 0.24);
    CHECK(of("Chad").raises() > of("Niga").enters());
    CHECK(of("Chad").pushes() > 1.8);
    // Bibi: the fewest hands of all, gives way to bets.
    for (const host::Bot& bot : host::kBots) {
        if (bot.name != "Bibi" && bot.name != "Niga") CHECK(of("Bibi").enters() < totals[std::string(bot.name)].enters());
    }
    CHECK(of("Bibi").enters() < 0.2);
    // Niga: few hands, nearly all of them raised.
    CHECK(of("Niga").enters() < 0.23);
    CHECK(of("Niga").raises() > of("Niga").enters() * 0.6);
    // Karp is the least pushy of the solid ones: he waits.
    CHECK(of("Karp").pushes() < of("Niga").pushes());
    CHECK(of("Karp").pushes() < of("Chips").pushes());
    // Snegr plays far more hands than the regulars.
    CHECK(of("Snegr").enters() > 0.32);
    // Slon bets the most of the strong ones.
    CHECK(of("Slon").pushes() > of("Chips").pushes());
    CHECK(of("Slon").pushes() > of("Vlid").pushes());
    // Who wins from whom takes far more hands to show than a test should play:
    // that is what tools/sim.cpp is for.
}

// A bot's mood: a run of big losses makes the easily upset ones play wilder.
TEST(a_big_loss_throws_some_off) {
    bots::Brain chad(bots::character("Chad"), 1);
    bots::Brain chips(bots::character("Chips"), 1);
    const double calm_bluff = chad.style().bluff;
    const double calm_loose = chad.style().loose;
    for (int i = 0; i < 3; ++i) {
        chad.hand_over(bots::Outcome{-90, true});
        chips.hand_over(bots::Outcome{-90, true});
    }
    CHECK(chad.tilt() > 0.6);
    CHECK(chips.tilt() < 0.1);
    CHECK(chad.style().bluff > calm_bluff * 1.4);
    CHECK(chad.style().loose > calm_loose * 1.2);
    // It wears off after a quiet stretch.
    for (int i = 0; i < 40; ++i) chad.hand_over(bots::Outcome{-1, false});
    CHECK(chad.tilt() < 0.05);
    CHECK_EQ(chad.record().hands, 43);
    CHECK_EQ(chad.record().showdowns, 3);

    // Vlid does not stay the same player.
    bots::Brain vlid(bots::character("Vlid"), 4);
    double least = 10;
    double most = 0;
    for (int i = 0; i < 400; ++i) {
        vlid.hand_over(bots::Outcome{0, false});
        least = std::min(least, vlid.style().loose);
        most = std::max(most, vlid.style().loose);
    }
    CHECK(most > least * 1.5);
}

// Thinking time follows the decision: most moves are quick, a few are long.
TEST(bots_take_longer_over_hard_decisions) {
    const host::Pace pace;
    const game::Stakes stakes{1, 2, 40, 200};
    host::Dealer dealer(stakes, 6, 21, pace);
    const char* names[] = {"Vasya", "Chad", "Karp", "Chips", "Slon", "Bibi"};
    for (int seat = 0; seat < 6; ++seat) dealer.sit(seat, host::Player{names[seat], "01", true}, 200);
    std::vector<std::int64_t> times;
    std::int64_t since = 0;
    int acting = -1;
    for (std::int64_t now = 0; now < 3 * 60 * 60 * 1000 && times.size() < 1500; now += 50) {
        dealer.tick(now);
        const int who = dealer.view(-1).acting;
        if (who != acting) {
            if (acting >= 0) times.push_back(now - since);
            acting = who;
            since = now;
        }
    }
    CHECK(times.size() >= 1500);
    std::sort(times.begin(), times.end());
    const auto at = [&](double part) { return times[static_cast<std::size_t>(part * static_cast<double>(times.size() - 1))]; };
    std::printf("  thinking time: least %.1f s, half under %.1f s, nine in ten under %.1f s, most %.1f s\n",
                static_cast<double>(at(0)) / 1000, static_cast<double>(at(0.5)) / 1000, static_cast<double>(at(0.9)) / 1000,
                static_cast<double>(at(1)) / 1000);
    CHECK(at(0) >= pace.think_min - 50);
    CHECK(at(1) <= pace.think_max + 100);
    CHECK(at(0.5) < 3000);
    CHECK(at(0.9) < 7000);
    CHECK(at(1) > 6000);
}

// Lines are rare, come from the one who says them, and stop when switched off.
TEST(bots_say_a_line_now_and_then) {
    // Everybody but Vlid has something for every occasion that can happen to them.
    for (const host::Bot& bot : host::kBots) {
        game::Random random(1);
        const int hello = bots::pick_phrase(bot.name, bots::Occasion::Hello, random);
        const int lost = bots::pick_phrase(bot.name, bots::Occasion::LostBig, random);
        if (bot.name == "Vlid") {
            CHECK_EQ(hello, -1);
            CHECK_EQ(lost, -1);
        } else {
            CHECK(hello >= 0 && lost >= 0);
            CHECK(bots::phrases()[static_cast<std::size_t>(hello)].who == bot.name);
        }
    }
    for (std::size_t i = 0; i < bots::phrases().size(); ++i) {
        CHECK(!bots::phrase_text(static_cast<int>(i), false).empty());
        CHECK(!bots::phrase_text(static_cast<int>(i), true).empty());
    }
    CHECK(bots::phrase_text(-1, false).empty());
    CHECK(bots::phrase_text(100000, true).empty());

    const game::Stakes stakes{1, 2, 40, 200};
    const char* names[] = {"Vasya", "Chad", "Karp", "Snegr", "Slon", "Vlid"};
    for (const bool talk : {true, false}) {
        host::Dealer dealer(stakes, 6, 33, host::Pace::none());
        dealer.set_talk(talk);
        for (int seat = 0; seat < 6; ++seat) dealer.sit(seat, host::Player{names[seat], "01", true}, 200);
        int said = 0;
        int hands = 0;
        for (std::int64_t now = 1; hands < 1500; ++now) {
            dealer.tick(now);
            // Without pauses a whole hand is played per call: its lines are the tail of the record.
            const host::TableView view = dealer.view(-1);
            for (auto line = view.log.rbegin(); line != view.log.rend(); ++line) {
                if (line->kind == host::LogLine::Kind::NewHand) break;
                if (line->kind != host::LogLine::Kind::Says) continue;
                ++said;
                CHECK(line->bot);
                CHECK(line->name != "Vlid");
                const int number = line->number;
                CHECK(number >= 0 && static_cast<std::size_t>(number) < bots::phrases().size());
                if (number >= 0 && static_cast<std::size_t>(number) < bots::phrases().size()) {
                    CHECK(bots::phrases()[static_cast<std::size_t>(number)].who == line->name);
                }
            }
            hands = view.hand_number;
        }
        if (talk) {
            std::printf("  %d lines in %d hands\n", said, hands);
            // Now and then: not in every other hand, not once an evening.
            CHECK(said > hands / 150);
            CHECK(said < hands / 6);
        } else {
            CHECK_EQ(said, 0);
        }
    }
}
