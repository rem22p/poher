#include <filesystem>
#include <string>

#include "app/game.hpp"
#include "check.hpp"
#include "ui/avatar.hpp"
#include "ui/canvas.hpp"
#include "ui/lang.hpp"
#include "ui/table_draw.hpp"

using namespace poher;
using app::Game;
using app::Key;
using game::Chips;

namespace {

const std::filesystem::path kAvatars = std::filesystem::path(POHER_SOURCE_DIR) / "avatars";

bool contains(const ui::Canvas& canvas, std::string_view text) {
    for (const std::string& line : canvas.to_text()) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A table in the middle of a hand with `seats` players, the viewer to act.
host::TableView busy_table(int seats) {
    using game::Suit;
    host::TableView view;
    view.stakes = game::Stakes{1, 2, 40, 200};
    view.hand_number = 7;
    view.stage = host::Stage::Playing;
    view.viewer = 0;
    view.button = seats - 1;
    view.acting = 0;
    view.board = {{game::kAce, Suit::Spades}, {game::kKing, Suit::Diamonds}, {7, Suit::Clubs}};
    view.pot = 12345;
    view.seats.resize(static_cast<std::size_t>(seats));
    const char* names[] = {"Hero", "Vasya", "Chad", "Bibi", "Niga", "Karp"};
    for (int i = 0; i < seats; ++i) {
        host::SeatView& seat = view.seats[static_cast<std::size_t>(i)];
        seat.taken = true;
        seat.name = names[i];
        seat.avatar = "0" + std::to_string(i + 1);
        seat.bot = i > 0;
        seat.stack = 1000 + i;
        seat.in_hand = true;
        seat.bet = i > 0 ? 40 * i : 0;
    }
    view.seats[0].cards = {{{game::kQueen, Suit::Hearts}, {10, Suit::Hearts}}};
    game::Choices choices;
    choices.to_call = 40 * (seats - 1);
    choices.can_raise = true;
    choices.min_raise_to = 80 * (seats - 1);
    choices.max_raise_to = 1000;
    view.choices = choices;
    return view;
}

}  // namespace

TEST(text_is_measured_in_cells) {
    CHECK_EQ(ui::text_width("Fold"), 4);
    CHECK_EQ(ui::text_width("Фолд"), 4);
    CHECK_EQ(ui::text_width("♠♥♦♣"), 4);
    CHECK_EQ(ui::text_width("日本"), 4);
    CHECK_EQ(ui::text_width("é"), 1);   // e with a combining accent
    CHECK(ui::fit_text("Vladislav", 5) == "Vlad…");
    CHECK(ui::fit_text("Влад", 5) == "Влад");
    CHECK_EQ(ui::text_width(ui::fit_text("日本語の名前", 5)), 5);
    // Broken UTF-8 must not crash or run past the end.
    CHECK_EQ(ui::text_width("\xff\xfe\xe2\x82"), 4);

    ui::Canvas canvas(6, 1);
    CHECK_EQ(canvas.put(0, 0, "日本語"), 6);
    CHECK(canvas.cell(1, 0).glyph == U'\0');
    CHECK(canvas.to_text()[0] == "日本語");
    // A wide glyph that would stick out is replaced by a blank.
    ui::Canvas narrow(3, 1);
    narrow.put(0, 0, "日本");
    CHECK(narrow.to_text()[0] == "日 ");
    // Writing outside the picture is harmless.
    narrow.put(-5, 0, "abcdefgh");
    narrow.put(0, 7, "x");
    narrow.fill(-3, -3, 100, 100, ui::color::kFelt);
}

TEST(words_and_numbers) {
    CHECK(ui::chips_text(0) == "0");
    CHECK(ui::chips_text(999) == "999");
    CHECK(ui::chips_text(12480) == "12 480");
    CHECK(ui::chips_text(1000000) == "1 000 000");
    CHECK(ui::fill("{} wins {}", {"Karp", "220"}) == "Karp wins 220");

    const auto hand = [](const char* cards) { return game::evaluate(game::parse_cards(cards)); };
    CHECK(ui::describe_hand(hand("As Ad 7c 7d Kh"), ui::Lang::En) == "Two pair: aces and sevens");
    CHECK(ui::describe_hand(hand("As Ad 7c 7d Kh"), ui::Lang::Ru) == "Две пары: тузы и семёрки");
    CHECK(ui::describe_hand(hand("Qs Qd Qc 7d Kh"), ui::Lang::Ru) == "Тройка дам");
    CHECK(ui::describe_hand(hand("9s 8d 7c 6d 5h"), ui::Lang::Ru) == "Стрит до девятки");
    CHECK(ui::describe_hand(hand("As Ks Qs Js Ts"), ui::Lang::En) == "Royal flush");
    CHECK(ui::describe_hand(hand("Ks Kd Kc 7d 7h"), ui::Lang::En) == "Full house: kings over sevens");
    CHECK(ui::describe_hand(hand("As Kd 9c 7d 2h"), ui::Lang::Ru) == "Старшая карта: туз");

    // The same key gives the same action in both keyboard layouts.
    CHECK(app::key_letter(U'F') == U'f');
    CHECK(app::key_letter(U'а') == U'f');
    CHECK(app::key_letter(U'С') == U'c');
    CHECK(app::key_letter(U'к') == U'r');
    CHECK(app::key_letter(U'и') == U'b');
    CHECK(app::key_letter(U'7') == U'7');
}

TEST(avatars_come_from_picture_files) {
    ui::Avatars avatars;
    avatars.open_folder(kAvatars);
    CHECK_EQ(avatars.names().size(), 20);
    CHECK(avatars.has("01") && avatars.has("20"));
    for (const int box : {16, 24, 32, 48}) {
        const ui::AvatarArt& art = avatars.art("08", box, true, ui::color::kBotTag);
        CHECK_EQ(art.box, box);
        CHECK_EQ(art.cells.size(), static_cast<std::size_t>(box) * static_cast<std::size_t>(box / 2));
        // A real picture is not one flat colour.
        bool varied = false;
        for (const ui::Quad& quad : art.cells) varied = varied || !(quad.first == art.cells[0].first);
        CHECK(varied);
    }
    // A picture that does not exist gives a drawn stand-in instead of a hole.
    const ui::AvatarArt& missing = avatars.art("no-such-picture", 16, false, ui::color::kHero);
    CHECK_EQ(missing.cells.size(), 16u * 8u);
}

TEST(table_fits_every_window) {
    ui::Avatars avatars;
    avatars.open_folder(kAvatars);
    int drawn = 0;
    for (int seats = 2; seats <= 6; ++seats) {
        const host::TableView view = busy_table(seats);
        for (const bool panel : {false, true}) {
            for (const ui::Lang lang : {ui::Lang::En, ui::Lang::Ru}) {
                for (int width = 110; width <= 270; width += 13) {
                    for (int height = 34; height <= 80; height += 5) {
                        ui::TableLook look;
                        look.lang = lang;
                        look.panel = panel;
                        look.bankroll = 9800;
                        look.portrait_seat = 1;
                        ui::Canvas canvas(width, height);
                        ui::draw_table(canvas, view, look, avatars);
                        ++drawn;
                        // Everybody's name, the pot and the buttons are on the screen whole.
                        // In a window too narrow for both, the panel lies over the right
                        // side of the table and hides who sits there.
                        const bool covered = panel && width < 138;
                        bool whole = true;
                        for (const host::SeatView& seat : view.seats) {
                            whole = whole && (covered || contains(canvas, seat.name));
                        }
                        whole = whole && contains(canvas, "12 345");
                        whole = whole && contains(canvas, lang == ui::Lang::Ru ? "Фолд" : "Fold");
                        whole = whole && contains(canvas, lang == ui::Lang::Ru ? "Рейз" : "Raise");
                        whole = whole && contains(canvas, " D ");
                        if (!whole) {
                            ++check::failures();
                            std::printf("  FAILED: %d seats, %dx%d, panel %d: something is cut off\n", seats, width,
                                        height, panel);
                            if (check::failures() > 10) return;
                        }
                    }
                }
            }
        }
    }
    // Too small a window gets the request to make it bigger.
    ui::Canvas tiny(80, 24);
    ui::draw_table(tiny, busy_table(6), ui::TableLook{}, avatars);
    CHECK(contains(tiny, "110×34"));
    std::printf("  %d pictures\n", drawn);
}

TEST(a_whole_session_without_a_terminal) {
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        app::Settings settings;
        settings.lang = seed % 2 ? ui::Lang::Ru : ui::Lang::En;
        Game game(settings, kAvatars, seed);
        game::Random random(seed * 77);
        std::int64_t now = 0;
        const auto press = [&](Key::Kind kind, char32_t letter = 0) {
            game.key(Key{kind, letter});
            game.tick(now);
        };
        const auto draw = [&] {
            const int width = 100 + static_cast<int>(random.below(170));
            const int height = 30 + static_cast<int>(random.below(50));
            ui::Canvas canvas(width, height);
            game.draw(canvas);
        };

        draw();
        press(Key::Kind::Enter);          // the menu: play with bots
        draw();
        press(Key::Kind::Enter);          // the setup: start
        CHECK(game.at_table());
        CHECK_EQ(game.bankroll(), host::kStartBankroll - 200);

        // Press keys more or less at random for a long time.
        const Key::Kind kinds[] = {Key::Kind::Enter, Key::Kind::Left, Key::Kind::Right, Key::Kind::Up,
                                   Key::Kind::Down, Key::Kind::Tab, Key::Kind::Backspace};
        const char32_t letters[] = {U'c', U'c', U'c', U'f', U'r', U'b', U'с', U'к', U'1', U'5', U'0', U'x'};
        for (int step = 0; step < 6000 && game.at_table(); ++step) {
            now += 150;
            game.tick(now);
            const std::uint32_t pick = random.below(10);
            if (pick < 3) {
                press(kinds[random.below(std::size(kinds))]);
            } else if (pick < 8) {
                press(Key::Kind::Letter, letters[random.below(std::size(letters))]);
            }
            if (step % 7 == 0) draw();
            CHECK(game.bankroll() >= 0);
            if (check::failures() > 10) return;
        }
        CHECK(game.at_table());
        CHECK(game.dealer()->view(0).hand_number > 20);

        // Esc opens the menu of the table; "leave" is the second line from the end.
        const Chips at_table = game.dealer()->stack(0);
        const Chips before = game.bankroll();
        press(Key::Kind::Escape);
        // If an amount was being chosen, the first Esc only closed that.
        if (!game.paused()) press(Key::Kind::Escape);
        CHECK(game.paused());
        draw();
        press(Key::Kind::Up);
        press(Key::Kind::Up);
        press(Key::Kind::Enter);
        CHECK(!game.at_table());
        // Whatever was at the table is back in the bankroll, give or take the hand that was cut short.
        CHECK(game.bankroll() >= before);
        CHECK(game.bankroll() <= before + at_table + 6 * 200);
        draw();
    }
}

TEST(prompts_and_the_side_panel) {
    ui::Avatars avatars;
    avatars.open_folder(kAvatars);

    // Out of chips between hands: the offer to buy in again.
    host::TableView broke = busy_table(4);
    broke.choices.reset();
    broke.acting = 2;
    broke.seats[0].stack = 0;
    broke.seats[0].in_hand = false;
    broke.seats[0].cards.reset();
    ui::TableLook look;
    look.buy_amount = 200;
    ui::Canvas canvas(140, 40);
    ui::draw_table(canvas, broke, look, avatars);
    CHECK(contains(canvas, "You are out of chips"));
    CHECK(contains(canvas, "buy in for 200"));
    CHECK(!contains(canvas, "Fold"));
    // The bot whose turn it is is shown thinking.
    CHECK(contains(canvas, "thinking"));

    // The side panel: the picked player and the record of the hand.
    host::TableView view = busy_table(6);
    host::LogLine line;
    line.kind = host::LogLine::Kind::Raise;
    line.name = "Karp";
    line.bot = true;
    line.amount = 120;
    view.log.push_back(line);
    look = ui::TableLook{};
    look.panel = true;
    look.portrait_seat = 5;
    for (const auto& [width, height] : {std::pair{138, 34}, std::pair{192, 54}, std::pair{256, 72}, std::pair{120, 36}}) {
        ui::Canvas with_panel(width, height);
        ui::draw_table(with_panel, view, look, avatars);
        CHECK(contains(with_panel, "raises to 120"));
    }

    // Choosing the amount of a raise: the amount is on the button, the limits under it.
    look = ui::TableLook{};
    look.raising = true;
    look.raise_to = 640;
    ui::Canvas raising(192, 54);
    ui::draw_table(raising, view, look, avatars);
    CHECK(contains(raising, "Raise to 640"));
    CHECK(contains(raising, "from 400 to 1 000"));

    // A winner: the win next to the stack and in the middle of the table.
    host::TableView over = busy_table(3);
    over.stage = host::Stage::Over;
    over.choices.reset();
    over.acting = -1;
    over.pot = 0;
    for (host::SeatView& seat : over.seats) seat.bet = 0;
    over.seats[1].won = 480;
    ui::Canvas result(192, 54);
    ui::draw_table(result, over, ui::TableLook{}, avatars);
    CHECK(contains(result, "Vasya wins 480"));
    CHECK(contains(result, "+480"));
}

// The full hint: the outs and the chance to win under the name of the hand.
TEST(the_full_hint_counts_outs_and_chances) {
    ui::Avatars avatars;
    avatars.open_folder(kAvatars);
    host::TableView view = busy_table(4);
    ui::TableLook look;
    look.hint = ui::Hint::Full;
    look.hint_more = "outs: 9 · improves 35% · wins 41%";
    ui::Canvas canvas(160, 44);
    ui::draw_table(canvas, view, look, avatars);
    CHECK(contains(canvas, "outs: 9"));
    CHECK(contains(canvas, "wins 41%"));
    // The plain hint leaves the second line out.
    look.hint = ui::Hint::Name;
    ui::Canvas plain(160, 44);
    ui::draw_table(plain, view, look, avatars);
    CHECK(!contains(plain, "outs: 9"));

    // In the game: on the first decision of a hand the chance is already there.
    for (const ui::Lang lang : {ui::Lang::En, ui::Lang::Ru}) {
        app::Settings settings;
        settings.lang = lang;
        settings.hint = ui::Hint::Full;
        Game game(settings, kAvatars, 9);
        std::int64_t now = 0;
        game.key(Key{Key::Kind::Enter});
        game.key(Key{Key::Kind::Enter});
        CHECK(game.at_table());
        bool my_turn = false;
        for (int step = 0; step < 4000 && !my_turn; ++step) {
            now += 100;
            game.tick(now);
            my_turn = game.dealer()->view(0).choices.has_value();
        }
        CHECK(my_turn);
        ui::Canvas table(160, 44);
        game.draw(table);
        CHECK(contains(table, lang == ui::Lang::Ru ? "победа " : "wins "));
        CHECK(contains(table, "%"));
    }
}
