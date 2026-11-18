// Pictures of the screens of the game, made without a terminal. A tool for
// development: it lets the looks be checked and compared with the mockups.
//
//   poher-shot mock W H OUT [panel] [ru] [say]  the scene of the mockup (mockups/render.py);
//                                           "say" adds a line said by a bot
//   poher-shot play W H PREFIX [ru] [full] [SEED]  plays a scripted session and writes PREFIX-<moment>.svg
//                                           ("full" turns on the hint with outs and chances)
//
// OUT ends in .svg (cells with real letters; `rsvg-convert` turns it into a
// PNG), .ppm (letters as tinted cells), .ans (escape codes for `cat`) or
// .txt (the glyphs alone).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "app/game.hpp"
#include "net/loop_network.hpp"
#include "app/paths.hpp"
#include "bots/talk.hpp"
#include "ui/table_draw.hpp"

namespace {

using namespace poher;
using app::Key;

int quarters_of(char32_t glyph) {
    switch (glyph) {
        case U'▘': return 1;
        case U'▝': return 2;
        case U'▀': return 3;
        case U'▖': return 4;
        case U'▌': return 5;
        case U'▞': return 6;
        case U'▛': return 7;
        case U'▗': return 8;
        case U'▚': return 9;
        case U'▐': return 10;
        case U'▜': return 11;
        case U'▄': return 12;
        case U'▙': return 13;
        case U'▟': return 14;
        case U'█': return 15;
        default: return -1;
    }
}

std::string hex(ui::Rgb colour) {
    char text[8];
    std::snprintf(text, sizeof text, "#%02x%02x%02x", colour.r, colour.g, colour.b);
    return text;
}

// One cell is 10 x 20 units.
std::string to_svg(const ui::Canvas& canvas) {
    const int cw = 10;
    const int ch = 20;
    std::string out = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(canvas.width() * cw) +
                      "\" height=\"" + std::to_string(canvas.height() * ch) +
                      "\" font-family=\"JetBrainsMono Nerd Font Mono, DejaVu Sans Mono, monospace\" font-size=\"16\">\n";
    const auto rect = [&](int x, int y, int w, int h, ui::Rgb colour) {
        out += "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"" + std::to_string(w) +
               "\" height=\"" + std::to_string(h) + "\" fill=\"" + hex(colour) + "\"/>\n";
    };
    for (int y = 0; y < canvas.height(); ++y) {
        // Backgrounds, joined into runs of one colour.
        for (int x = 0; x < canvas.width();) {
            int end = x + 1;
            while (end < canvas.width() && canvas.cell(end, y).bg == canvas.cell(x, y).bg) ++end;
            rect(x * cw, y * ch, (end - x) * cw, ch, canvas.cell(x, y).bg);
            x = end;
        }
        for (int x = 0; x < canvas.width(); ++x) {
            const ui::Cell& cell = canvas.cell(x, y);
            if (cell.glyph == U' ' || cell.glyph == U'\0') continue;
            const int quarters = quarters_of(cell.glyph);
            if (quarters >= 0) {
                for (int i = 0; i < 4; ++i) {
                    if ((quarters >> i) & 1) rect(x * cw + (i % 2) * cw / 2, y * ch + (i / 2) * ch / 2, cw / 2, ch / 2, cell.fg);
                }
                continue;
            }
            std::string glyph;
            switch (cell.glyph) {
                case U'<': glyph = "&lt;"; break;
                case U'>': glyph = "&gt;"; break;
                case U'&': glyph = "&amp;"; break;
                default: ui::append_utf8(glyph, cell.glyph);
            }
            out += "<text x=\"" + std::to_string(x * cw + cw / 2) + "\" y=\"" + std::to_string(y * ch + 15) +
                   "\" text-anchor=\"middle\" fill=\"" + hex(cell.fg) + "\"" +
                   (cell.bold ? " font-weight=\"bold\"" : "") + ">" + glyph + "</text>\n";
        }
    }
    out += "</svg>\n";
    return out;
}

bool save(const ui::Canvas& canvas, const std::string& file) {
    std::string data;
    if (file.ends_with(".svg")) {
        data = to_svg(canvas);
    } else if (file.ends_with(".ppm")) {
        data = canvas.to_ppm();
    } else if (file.ends_with(".ans")) {
        data = canvas.to_ansi();
    } else {
        for (const std::string& line : canvas.to_text()) data += line + "\n";
    }
    std::ofstream out(file, std::ios::binary);
    out << data;
    std::printf("%s\n", file.c_str());
    return static_cast<bool>(out);
}

// The fixed scene the mockup shows.
host::TableView mock_view() {
    using game::Suit;
    host::TableView view;
    view.stakes = game::Stakes{1, 2, 40, 200};
    view.hand_number = 42;
    view.stage = host::Stage::Playing;
    view.viewer = 0;
    view.button = 1;
    view.acting = 0;
    view.aggressor = 2;
    view.last_actor = 3;
    view.board = {{game::kAce, Suit::Spades}, {game::kKing, Suit::Diamonds}, {7, Suit::Clubs}};
    view.pot = 220;
    view.seats.resize(6);
    const auto seat = [&](int index, const char* name, const char* avatar, bool bot, game::Chips stack) -> host::SeatView& {
        host::SeatView& out = view.seats[static_cast<std::size_t>(index)];
        out.taken = true;
        out.name = name;
        out.avatar = avatar;
        out.bot = bot;
        out.stack = stack;
        out.in_hand = true;
        return out;
    };
    host::SeatView& me = seat(0, "You", "03", false, 2000);
    me.cards = {{{game::kQueen, Suit::Hearts}, {10, Suit::Hearts}}};
    seat(1, "Omar", "11", false, 960);
    host::SeatView& karp = seat(2, "Karp", "20", true, 1480);
    karp.bet = 40;
    karp.note = host::Note::Bet;
    karp.note_amount = 40;
    host::SeatView& dana = seat(3, "Dana", "01", false, 2150);
    dana.bet = 40;
    dana.note = host::Note::Call;
    dana.note_amount = 40;
    host::SeatView& slon = seat(4, "Slon", "10", true, 1020);
    slon.folded = true;
    slon.note = host::Note::Fold;
    host::SeatView& snegr = seat(5, "Snegr", "14", true, 3300);
    snegr.folded = true;
    snegr.note = host::Note::Fold;

    game::Choices choices;
    choices.to_call = 40;
    choices.can_raise = true;
    choices.min_raise_to = 80;
    choices.max_raise_to = 2000;
    view.choices = choices;

    const auto line = [&](host::LogLine::Kind kind, const char* name, bool bot, game::Chips amount) {
        host::LogLine out;
        out.kind = kind;
        out.name = name;
        out.bot = bot;
        out.amount = amount;
        view.log.push_back(out);
    };
    host::LogLine board;
    board.kind = host::LogLine::Kind::Board;
    board.cards = view.board;
    view.log.push_back(board);
    line(host::LogLine::Kind::Check, "Omar", false, 0);
    line(host::LogLine::Kind::Bet, "Karp", true, 40);
    line(host::LogLine::Kind::Call, "Dana", false, 40);
    line(host::LogLine::Kind::Fold, "Slon", true, 0);
    line(host::LogLine::Kind::Fold, "Snegr", true, 0);
    return view;
}

// The same scene a moment later: somebody says something.
host::TableView mock_view_with_talk() {
    host::TableView view = mock_view();
    host::LogLine said;
    said.kind = host::LogLine::Kind::Says;
    said.name = "Karp";
    said.bot = true;
    game::Random random(1);
    said.number = bots::pick_phrase("Karp", bots::Occasion::WonBig, random);
    view.log.push_back(said);
    return view;
}

int mock(int argc, char** argv) {
    if (argc < 5) return 2;
    const int width = std::atoi(argv[2]);
    const int height = std::atoi(argv[3]);
    ui::TableLook look;
    look.title = "Kitchen";
    look.bankroll = 8000;
    for (int i = 5; i < argc; ++i) {
        if (std::string(argv[i]) == "panel") look.panel = true;
        if (std::string(argv[i]) == "ru") look.lang = ui::Lang::Ru;
    }
    look.portrait_seat = 2;
    ui::Avatars avatars;
    avatars.open_folder(app::default_avatar_folder(argv[0]));
    ui::Canvas canvas(width, height);
    bool talk = false;
    for (int i = 5; i < argc; ++i) talk = talk || std::string(argv[i]) == "say";
    ui::draw_table(canvas, talk ? mock_view_with_talk() : mock_view(), look, avatars);
    return save(canvas, argv[4]) ? 0 : 1;
}

int play(int argc, char** argv) {
    if (argc < 5) return 2;
    const int width = std::atoi(argv[2]);
    const int height = std::atoi(argv[3]);
    const std::string prefix = argv[4];
    app::Settings settings;
    std::uint64_t seed = 7;
    for (int i = 5; i < argc; ++i) {
        if (std::string(argv[i]) == "ru") {
            settings.lang = ui::Lang::Ru;
        } else if (std::string(argv[i]) == "full") {
            settings.hint = ui::Hint::Full;
        } else {
            seed = std::strtoull(argv[i], nullptr, 10);
        }
    }
    app::Game game(settings, app::default_avatar_folder(argv[0]), seed);
    std::int64_t now = 0;
    const auto shot = [&](const std::string& moment) {
        ui::Canvas canvas(width, height);
        game.tick(now);
        game.draw(canvas);
        save(canvas, prefix + "-" + moment + ".svg");
    };
    const auto press = [&](Key::Kind kind, char32_t letter = 0) {
        game.key(Key{kind, letter});
        game.tick(now);
    };
    // Lets time pass until `done` says so, at most `limit` seconds.
    const auto wait = [&](int limit, const auto& done) {
        for (int i = 0; i < limit * 20; ++i) {
            now += 50;
            game.tick(now);
            if (done()) return true;
        }
        return false;
    };
    const auto view = [&] { return game.dealer()->view(0); };

    shot("menu");
    press(Key::Kind::Enter);
    press(Key::Kind::Down);
    press(Key::Kind::Down);
    press(Key::Kind::Down);
    press(Key::Kind::Down);
    shot("setup");
    press(Key::Kind::Enter);
    if (!game.at_table()) {
        std::printf("the table did not start\n");
        return 1;
    }

    // The first decision.
    if (!wait(120, [&] { return view().choices.has_value(); })) return 1;
    shot("turn");
    if (view().choices->can_raise) {
        press(Key::Kind::Letter, U'r');
        shot("raise");
        press(Key::Kind::Up);
        press(Key::Kind::Escape);
    }
    press(Key::Kind::Tab);
    shot("panel");
    press(Key::Kind::Tab);

    // Call everything until hands are shown.
    bool shown = false;
    for (int hand = 0; hand < 40 && !shown; ++hand) {
        wait(600, [&] {
            const host::TableView now_view = view();
            if (now_view.choices) press(Key::Kind::Letter, U'c');
            for (const host::SeatView& seat : now_view.seats) {
                if (seat.taken && seat.shown && seat.won > 0) shown = true;
            }
            return shown || now_view.stage == host::Stage::Over;
        });
        if (!shown) wait(30, [&] { return view().stage != host::Stage::Over; });
    }
    shot("showdown");
    press(Key::Kind::Tab);
    shot("showdown-panel");
    press(Key::Kind::Tab);
    press(Key::Kind::Escape);
    shot("pause");
    press(Key::Kind::Down);
    press(Key::Kind::Enter);
    shot("settings");
    press(Key::Kind::Escape);
    if (game.paused()) press(Key::Kind::Escape);

    // The profile with a nick being typed, then as it is.
    press(Key::Kind::Letter, U's');
    press(Key::Kind::Enter);
    for (const char32_t letter : std::u32string_view(U"rem22p")) press(Key::Kind::Letter, letter);
    shot("nick");
    press(Key::Kind::Enter);
    press(Key::Kind::Down);
    press(Key::Kind::Right);
    shot("profile");
    press(Key::Kind::Escape);

    // The history: the list and one hand.
    press(Key::Kind::Letter, U'h');
    shot("history");
    press(Key::Kind::Enter);
    shot("hand");
    return 0;
}

// Two players through an in-memory network: the friends screen, an
// invitation, and the same table from the host's and the guest's chair.
int friends(int argc, char** argv) {
    if (argc < 5) return 2;
    const int width = std::atoi(argv[2]);
    const int height = std::atoi(argv[3]);
    const std::string prefix = argv[4];
    app::Settings settings;
    for (int i = 5; i < argc; ++i) {
        if (std::string(argv[i]) == "ru") settings.lang = ui::Lang::Ru;
    }
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "poher-shot-friends";
    std::filesystem::remove_all(base);
    const auto folder = [&](const char* name, const char* nick, const char* avatar) {
        const std::filesystem::path path = base / name;
        app::Profile profile;
        profile.nick = nick;
        profile.avatar = avatar;
        app::Storage(path).save(profile);
        return path;
    };
    net::Hub hub;
    const auto on = [&hub](std::string name) -> online::Online::Factory {
        return [&hub, name](net::Events events, std::string&) { return hub.join(name, std::move(events)); };
    };
    app::Game ann(settings, app::default_avatar_folder(argv[0]), 7, folder("ann", "rem22p", "05"), on("Ann"));
    app::Game bob(settings, app::default_avatar_folder(argv[0]), 8, folder("bob", "Tolik", "14"), on("Bob"));
    app::Game cid(settings, app::default_avatar_folder(argv[0]), 9, folder("cid", "Youssef", "17"), on("Cid"));
    std::int64_t now = 0;
    const auto tick = [&](int times = 1) {
        for (int i = 0; i < times; ++i) {
            now += 50;
            ann.tick(now);
            bob.tick(now);
            cid.tick(now);
        }
    };
    const auto shot = [&](app::Game& game, const std::string& moment) {
        ui::Canvas canvas(width, height);
        game.tick(now);
        game.draw(canvas);
        save(canvas, prefix + "-" + moment + ".svg");
    };
    const auto press = [&](app::Game& game, Key::Kind kind, char32_t letter = 0) {
        game.key(Key{kind, letter});
        tick();
    };
    const auto type = [&](app::Game& game, const std::string& text) {
        for (const char c : text) game.key(Key{Key::Kind::Letter, static_cast<char32_t>(c)});
        tick();
    };
    tick(5);
    // Bob and Cid ask Ann to be friends; Ann has accepted Cid already.
    bob.network()->add(ann.network()->code());
    cid.network()->add(ann.network()->code());
    tick(5);
    ann.network()->accept(cid.network()->id());
    tick(5);
    shot(ann, "menu");
    press(ann, Key::Kind::Down);
    press(ann, Key::Kind::Enter);
    shot(ann, "list");
    press(ann, Key::Kind::Enter);
    type(ann, bob.network()->code().substr(0, 30));
    shot(ann, "typing");
    press(ann, Key::Kind::Escape);
    press(ann, Key::Kind::Down);
    press(ann, Key::Kind::Enter);  // accepts Bob
    tick(5);
    // Cid plays with bots.
    press(cid, Key::Kind::Enter);
    press(cid, Key::Kind::Down);
    press(cid, Key::Kind::Down);
    press(cid, Key::Kind::Down);
    press(cid, Key::Kind::Right);  // invitation only
    for (int i = 0; i < 20; ++i) press(cid, Key::Kind::Down);
    press(cid, Key::Kind::Enter);
    tick(5);
    // Ann sets up a table that friends may join.
    press(ann, Key::Kind::Escape);
    press(ann, Key::Kind::Up);
    press(ann, Key::Kind::Enter);
    press(ann, Key::Kind::Down);
    press(ann, Key::Kind::Down);
    press(ann, Key::Kind::Down);
    shot(ann, "setup");
    press(ann, Key::Kind::Enter);
    tick(40);
    // Bob sees it in his list.
    press(bob, Key::Kind::Down);
    press(bob, Key::Kind::Enter);
    tick(5);
    shot(bob, "bob-list");
    // Ann calls Bob from her table.
    press(ann, Key::Kind::Escape);
    press(ann, Key::Kind::Down);
    press(ann, Key::Kind::Down);
    press(ann, Key::Kind::Enter);
    {
        const std::vector<online::Mate> mates = ann.network()->mates();
        for (std::size_t i = 0; i < mates.size(); ++i) {
            press(ann, Key::Kind::Down);
            if (mates[i].id == bob.network()->id()) break;
        }
    }
    shot(ann, "calling");
    press(ann, Key::Kind::Enter);
    press(ann, Key::Kind::Escape);
    tick(5);
    shot(bob, "invitation");
    press(bob, Key::Kind::Enter);
    shot(bob, "joining");
    // Play a while: both call whatever comes.
    for (int i = 0; i < 6000; ++i) {
        tick();
        for (app::Game* game : {&ann, &bob}) {
            const host::TableView view = game->table_view();
            if (view.choices && i % 3 == 0) game->key(Key{Key::Kind::Letter, U'c'});
        }
        if (i > 1500 && bob.table_view().choices) break;
    }
    shot(bob, "guest");
    shot(ann, "host");
    std::filesystem::remove_all(base);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    int result = 2;
    if (mode == "mock") result = mock(argc, argv);
    if (mode == "play") result = play(argc, argv);
    if (mode == "friends") result = friends(argc, argv);
    if (result == 2) {
        std::fputs("poher-shot mock W H OUT [panel] [ru] [say]\npoher-shot play W H PREFIX [ru] [full] [SEED]\n"
                   "poher-shot friends W H PREFIX [ru]\n", stderr);
    }
    return result;
}
