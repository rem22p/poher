// The profile, the settings and the history of hands: what is kept on the
// player's computer between runs of the game.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/game.hpp"
#include "app/profile.hpp"
#include "check.hpp"
#include "ui/canvas.hpp"

using namespace poher;
using app::Game;
using app::Key;
using game::Chips;

namespace {

const std::filesystem::path kAvatars = std::filesystem::path(POHER_SOURCE_DIR) / "avatars";

// A folder of its own for one test, removed when the test ends.
class Folder {
public:
    explicit Folder(const char* name)
        : path_(std::filesystem::temp_directory_path() / (std::string("poher-test-") + name)) {
        std::filesystem::remove_all(path_);
    }
    ~Folder() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool contains(const ui::Canvas& canvas, std::string_view text) {
    for (const std::string& line : canvas.to_text()) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// Everything the player and the bots own.
Chips all_chips(const app::Profile& profile) {
    Chips total = profile.bankroll;
    for (const app::BotAccount& account : profile.bots) total += account.bankroll;
    return total;
}

int refills(const app::Profile& profile) {
    int count = profile.refills;
    for (const app::BotAccount& account : profile.bots) count += account.refills;
    return count;
}

// The profile as it is in the files right now: what a new run would start from.
app::Profile kept(const std::filesystem::path& folder) {
    app::Profile profile;
    app::Storage(folder).load(profile);
    return profile;
}

// Types keys into a game whose clock moves on by itself.
struct Hands {
    Game& game;
    std::int64_t now = 0;

    void press(Key::Kind kind, char32_t letter = 0) {
        game.key(Key{kind, letter});
        game.tick(now);
    }
    void type(std::u32string_view text) {
        for (const char32_t letter : text) press(Key::Kind::Letter, letter);
    }
    // Sits down at the default table and calls or checks nearly everything for a while.
    void play(int steps) {
        press(Key::Kind::Enter);  // the menu: play with bots
        press(Key::Kind::Enter);  // the setup: start
        for (int step = 0; step < steps; ++step) {
            now += 2000;
            game.tick(now);
            press(Key::Kind::Letter, step % 5 == 4 ? U'f' : U'c');
            if (step % 20 == 0) press(Key::Kind::Letter, U'b');  // whoever calls everything needs more chips now and then
        }
    }
    void leave() {
        press(Key::Kind::Escape);
        if (!game.paused()) press(Key::Kind::Escape);
        press(Key::Kind::Up);
        press(Key::Kind::Up);
        press(Key::Kind::Enter);
    }
};

}  // namespace

TEST(the_profile_is_written_and_read_back) {
    const Folder folder("files");
    const app::Storage storage(folder.path());

    app::Profile profile;
    profile.nick = "Рем 22";
    profile.avatar = "11";
    profile.bankroll = 12345;
    profile.refills = 2;
    profile.stats.hands_won = 40;
    profile.stats.biggest_pot = 987;
    profile.stats.from_bots = -655;
    profile.stats.from_people = 3000;
    profile.stats.showdowns = 31;
    profile.stats.showdowns_won = 17;
    profile.stats.play = bots::Notes{200, 61, 33, 70, 44, 90, 38};
    profile.seen = bots::Notes{120, 40, 20, 30, 20, 50, 21};
    profile.bots[3].bankroll = 41250;
    profile.bots[3].refills = 1;
    profile.bots[3].hands = 77;
    profile.bots[3].won = -300;
    profile.bots[3].seen = bots::Notes{77, 14, 10, 22, 11, 30, 13};
    storage.save(profile);

    app::Profile read;
    storage.load(read);
    CHECK(read.nick == profile.nick);
    CHECK(read.avatar == "11");
    CHECK_EQ(read.bankroll, 12345);
    CHECK_EQ(read.refills, 2);
    CHECK_EQ(read.stats.hands_won, 40);
    CHECK_EQ(read.stats.biggest_pot, 987);
    CHECK_EQ(read.stats.from_bots, -655);
    CHECK_EQ(read.stats.from_people, 3000);
    CHECK_EQ(read.stats.showdowns, 31);
    CHECK_EQ(read.stats.showdowns_won, 17);
    CHECK_EQ(read.stats.play.hands, 200);
    CHECK_EQ(read.stats.play.gave_up, 38);
    CHECK_EQ(read.seen.entered, 40);
    CHECK_EQ(read.bots[3].bankroll, 41250);
    CHECK_EQ(read.bots[3].refills, 1);
    CHECK_EQ(read.bots[3].hands, 77);
    CHECK_EQ(read.bots[3].won, -300);
    CHECK_EQ(read.bots[3].seen.faced, 30);
    // The others keep what they start with.
    CHECK_EQ(read.bots[0].bankroll, host::start_bankroll(host::Strength::Weak));
    CHECK_EQ(read.bots[8].bankroll, host::start_bankroll(host::Strength::Strong));

    app::Settings settings;
    settings.lang = ui::Lang::Ru;
    settings.hint = ui::Hint::Full;
    settings.sound = false;
    settings.talk = false;
    storage.save(settings);
    app::Settings back;
    storage.load(back);
    CHECK(back.lang == ui::Lang::Ru);
    CHECK(back.hint == ui::Hint::Full);
    CHECK(!back.sound);
    CHECK(!back.talk);

    // No file is left half-written under another name.
    for (const auto& entry : std::filesystem::directory_iterator(folder.path())) {
        CHECK(entry.path().extension() == ".txt");
    }

    // Without a folder nothing is kept and nothing fails.
    const app::Storage nowhere;
    nowhere.save(profile);
    nowhere.add(app::PastHand{});
    CHECK(nowhere.history(10).empty());
}

TEST(the_history_keeps_hands_line_by_line) {
    const Folder folder("history");
    const app::Storage storage(folder.path());
    CHECK(storage.history(10).empty());

    for (int number = 1; number <= 5; ++number) {
        app::PastHand hand;
        hand.number = number;
        hand.when = 1'800'000'000 + number;
        hand.level = 1;
        hand.result = number % 2 ? 120 : -40;
        if (number != 3) hand.cards = {game::Card{game::kAce, game::Suit::Spades}, game::Card{10, game::Suit::Hearts}};
        host::LogLine line;
        line.kind = host::LogLine::Kind::NewHand;
        line.number = number;
        hand.lines.push_back(line);
        line = host::LogLine{};
        line.kind = host::LogLine::Kind::Board;
        line.cards = game::parse_cards("Ah Kd 7c");
        hand.lines.push_back(line);
        line = host::LogLine{};
        line.kind = host::LogLine::Kind::Raise;
        line.name = "Рем 22";
        line.amount = 60;
        hand.lines.push_back(line);
        line = host::LogLine{};
        line.kind = host::LogLine::Kind::Says;
        line.name = "Chad";
        line.bot = true;
        line.number = 7;
        hand.lines.push_back(line);
        storage.add(hand);
    }

    const std::vector<app::PastHand> latest = storage.history(3);
    CHECK_EQ(latest.size(), 3);
    if (latest.size() != 3) return;
    CHECK_EQ(latest[0].number, 5);  // the newest first
    CHECK_EQ(latest[2].number, 3);
    CHECK(!latest[2].cards.has_value());
    CHECK(latest[0].cards.has_value());
    CHECK(latest[0].cards && (*latest[0].cards)[0] == (game::Card{game::kAce, game::Suit::Spades}));
    CHECK_EQ(latest[0].result, 120);
    CHECK_EQ(latest[1].result, -40);
    CHECK_EQ(latest[0].level, 1);
    CHECK_EQ(latest[0].when, 1'800'000'005);
    CHECK_EQ(latest[0].lines.size(), 4);
    if (latest[0].lines.size() != 4) return;
    CHECK(latest[0].lines[0].kind == host::LogLine::Kind::NewHand);
    CHECK_EQ(latest[0].lines[1].cards.size(), 3);
    CHECK(latest[0].lines[2].name == "Рем 22");
    CHECK_EQ(latest[0].lines[2].amount, 60);
    CHECK(latest[0].lines[3].bot);
    CHECK_EQ(latest[0].lines[3].number, 7);
    CHECK_EQ(storage.history(100).size(), 5);
}

TEST(a_session_is_there_after_a_restart) {
    const Folder folder("session");
    app::Settings settings;
    settings.lang = ui::Lang::Ru;
    Chips home = 0;
    Chips total = 0;
    long long hands = 0;
    {
        Game game(settings, kAvatars, 5, folder.path());
        total = all_chips(game.profile());
        Hands hands_on{game};
        // A nick first: the profile, "type a nick".
        hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Enter);
        hands_on.press(Key::Kind::Enter);
        hands_on.type(U"Рем");
        hands_on.press(Key::Kind::Enter);
        hands_on.press(Key::Kind::Escape);
        hands_on.press(Key::Kind::Up);
        CHECK(game.profile().nick == "Рем");

        hands_on.play(700);
        CHECK(game.at_table());
        CHECK(game.dealer()->view(0).seats[0].name == "Рем");
        CHECK(game.dealer()->view(0).hand_number > 10);
        // In the middle of a table the files hold every chip: the ones at the table count as brought home.
        CHECK_EQ(refills(kept(folder.path())), 0);
        CHECK_EQ(all_chips(kept(folder.path())), total);
        hands = game.profile().stats.play.hands;
        CHECK(hands > 10);
        CHECK(!game.history().empty());
        // Play on until a hand has just ended, then close the window without leaving the table.
        for (int step = 0; step < 400 && game.dealer()->view(0).stage != host::Stage::Over; ++step) {
            hands_on.now += 100;
            hands_on.press(Key::Kind::Letter, U'c');
        }
        CHECK(game.dealer()->view(0).stage == host::Stage::Over);
        home = game.bankroll() + game.dealer()->stack(0);
        hands = game.profile().stats.play.hands;
    }
    {
        Game game(settings, kAvatars, 6, folder.path());
        CHECK(!game.at_table());
        CHECK(game.profile().nick == "Рем");
        // The chips that were at the table are back in the bankroll, for the bots as well.
        CHECK_EQ(game.bankroll(), home);
        CHECK_EQ(all_chips(game.profile()), total);
        CHECK_EQ(game.profile().stats.play.hands, hands);
        CHECK(game.profile().stats.play.entered > 0);
        CHECK(!game.history().empty());
        CHECK(game.history().size() <= static_cast<std::size_t>(hands));
        long long bot_hands = 0;
        Chips bots_won = 0;
        for (const app::BotAccount& account : game.profile().bots) {
            bot_hands += account.hands;
            bots_won += account.won;
        }
        CHECK(bot_hands >= hands);
        // What the bots won the player lost: nobody else was at the table.
        CHECK_EQ(bots_won, -game.profile().stats.from_bots);
        CHECK_EQ(game.bankroll() - host::kStartBankroll, game.profile().stats.from_bots);

        // The history: the list, then a hand opened.
        Hands hands_on{game};
        ui::Canvas canvas(120, 40);
        hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Enter);
        game.draw(canvas);
        CHECK(contains(canvas, "История раздач"));
        CHECK(contains(canvas, "раздача №"));
        hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Enter);
        game.draw(canvas);
        CHECK(contains(canvas, "Рем"));
        hands_on.press(Key::Kind::Left);
        hands_on.press(Key::Kind::Down);
        game.draw(canvas);
        hands_on.press(Key::Kind::Escape);
        hands_on.press(Key::Kind::Escape);

        // A second session goes on from the first: leaving the table brings everything home.
        hands_on.press(Key::Kind::Up);
        hands_on.press(Key::Kind::Up);
        hands_on.play(300);
        CHECK(game.at_table());
        hands_on.leave();
        CHECK(!game.at_table());
        CHECK_EQ(refills(game.profile()), 0);
        CHECK_EQ(all_chips(game.profile()), total);
        CHECK(game.profile().stats.play.hands > hands);
        home = game.bankroll();
    }
    {
        Game game(settings, kAvatars, 7, folder.path());
        CHECK_EQ(game.bankroll(), home);
        CHECK_EQ(all_chips(game.profile()), total);
    }
}

TEST(the_profile_screen_changes_nick_and_avatar) {
    const Folder folder("screen");
    app::Settings settings;
    Game game(settings, kAvatars, 3, folder.path());
    Hands hands_on{game};
    ui::Canvas canvas(110, 36);

    hands_on.press(Key::Kind::Down);
    hands_on.press(Key::Kind::Enter);
    game.draw(canvas);
    CHECK(contains(canvas, "Profile"));
    CHECK(contains(canvas, "not set"));
    CHECK(contains(canvas, "Friends see"));

    // A nick is at most twelve glyphs, and not the name of a bot.
    hands_on.press(Key::Kind::Enter);
    hands_on.type(U"Chad");
    hands_on.press(Key::Kind::Enter);
    CHECK(game.profile().nick.empty());
    game.draw(canvas);
    CHECK(contains(canvas, "name of a bot"));
    for (int i = 0; i < 4; ++i) hands_on.press(Key::Kind::Backspace);
    hands_on.type(U"  Абвгдежзиклмноп  ");
    hands_on.press(Key::Kind::Enter);
    CHECK(game.profile().nick == "Абвгдежзик");
    // Esc leaves the nick as it was.
    hands_on.press(Key::Kind::Enter);
    hands_on.type(U"x");
    hands_on.press(Key::Kind::Escape);
    CHECK(game.profile().nick == "Абвгдежзик");

    // The avatar: only the pictures the bots do not wear, round and round.
    hands_on.press(Key::Kind::Down);
    std::vector<std::string> seen;
    for (int i = 0; i < 30; ++i) {
        hands_on.press(Key::Kind::Right);
        const std::string avatar = game.profile().avatar;
        for (const host::Bot& bot : host::kBots) CHECK(bot.avatar != avatar);
        if (std::find(seen.begin(), seen.end(), avatar) == seen.end()) seen.push_back(avatar);
    }
    CHECK_EQ(seen.size(), 11);  // twenty pictures, nine of them taken
    hands_on.press(Key::Kind::Left);
    const std::string chosen = game.profile().avatar;
    game.draw(canvas);

    // Every size of the window can show the screen.
    for (int width = 80; width <= 200; width += 17) {
        for (int height = 24; height <= 60; height += 9) {
            ui::Canvas other(width, height);
            game.draw(other);
        }
    }

    // It is all there in a new run, and the picture is ours at the table.
    Game again(settings, kAvatars, 4, folder.path());
    CHECK(again.profile().nick == "Абвгдежзик");
    CHECK(again.profile().avatar == chosen);
    Hands more{again};
    more.play(3);
    CHECK(again.dealer()->view(0).seats[0].avatar == chosen);
    // S at the table opens the profile, Esc goes back to the table.
    more.press(Key::Kind::Letter, U'ы');
    again.draw(canvas);
    CHECK(contains(canvas, "Profile"));
    more.press(Key::Kind::Escape);
    again.draw(canvas);
    CHECK(!contains(canvas, "Friends see"));
    more.press(Key::Kind::Letter, U'h');
    again.draw(canvas);
    CHECK(contains(canvas, "Hand history"));
    more.press(Key::Kind::Escape);
    CHECK(again.at_table());
}

TEST(settings_are_kept) {
    const Folder folder("settings");
    {
        Game game(app::Settings{}, kAvatars, 3, folder.path());
        Hands hands_on{game};
        hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Enter);   // the settings
        hands_on.press(Key::Kind::Right);   // the language
        CHECK(game.settings().lang == ui::Lang::Ru);
    }
    app::Settings settings;
    app::Storage(folder.path()).load(settings);
    CHECK(settings.lang == ui::Lang::Ru);
}

TEST(broken_files_do_not_break_the_game) {
    const Folder folder("broken");
    std::filesystem::create_directories(folder.path());
    {
        std::ofstream out(folder.path() / "profile.txt", std::ios::binary);
        out << "nick \x01\x02 very long nick that goes on and on and on and on and on and on and on and on\n"
            << "avatar no such picture\n"
            << "bankroll -500\n"
            << "refills many\n"
            << "play 1 2\n"
            << "seen -5 0 0 0 0 0 0\n"
            << "bot Chad -9 0 0 0\n"
            << "bot Nobody 1 2 3 4\n"
            << "\n"
            << "what is this\n";
    }
    {
        std::ofstream out(folder.path() / "history.txt", std::ios::binary);
        out << "3 0 0 0 - stray line\n"
            << "hand x y z\n"
            << "99 0 0 0 - bad kind\n"
            << "hand 4 1800000000 77 -20 As Kd\n"
            << "2 1 50 0 Qh,Jh Chad\n"
            << "garbage\n";
    }
    {
        std::ofstream out(folder.path() / "settings.txt", std::ios::binary);
        out << "lang xx\nhint 99\nsound\n";
    }
    app::Settings settings;
    app::Storage(folder.path()).load(settings);
    CHECK(settings.lang == ui::Lang::En);
    CHECK(settings.hint == ui::Hint::Full);

    Game game(settings, kAvatars, 2, folder.path());
    CHECK_EQ(game.bankroll(), 0);
    CHECK_EQ(game.profile().refills, 0);
    CHECK_EQ(game.profile().stats.play.hands, 0);
    CHECK_EQ(game.profile().seen.hands, 0);
    CHECK_EQ(game.profile().bots[1].bankroll, host::start_bankroll(host::Strength::Weak));
    CHECK(game.profile().nick.size() <= app::kNickLength * 4);

    // Every screen still opens, and a game can be played.
    Hands hands_on{game};
    ui::Canvas canvas(120, 40);
    for (int row = 1; row <= 2; ++row) {
        for (int i = 0; i < row; ++i) hands_on.press(Key::Kind::Down);
        hands_on.press(Key::Kind::Enter);
        game.draw(canvas);
        hands_on.press(Key::Kind::Enter);
        game.draw(canvas);
        hands_on.press(Key::Kind::Escape);
        hands_on.press(Key::Kind::Escape);
        for (int i = 0; i < row; ++i) hands_on.press(Key::Kind::Up);
    }
    CHECK_EQ(game.history().size(), 2);
}

TEST(keys_at_random_on_every_screen) {
    const Folder folder("random");
    int checked = 0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
        Game game(app::Settings{}, kAvatars, seed, folder.path());
        game::Random random(seed * 131);
        const Chips total = all_chips(game.profile());
        const int refilled = refills(game.profile());
        Hands hands_on{game};
        const Key::Kind kinds[] = {Key::Kind::Enter, Key::Kind::Enter, Key::Kind::Left, Key::Kind::Right,
                                   Key::Kind::Up, Key::Kind::Down, Key::Kind::Tab, Key::Kind::Backspace,
                                   Key::Kind::Escape};
        const char32_t letters[] = {U'c', U'c', U'c', U'c', U'f', U'r', U'b', U's', U'h', U'1', U'я', U' '};
        for (int step = 0; step < 5000 && !game.quit(); ++step) {
            hands_on.now += 200;
            game.tick(hands_on.now);
            const std::uint32_t pick = random.below(10);
            if (pick < 3) {
                hands_on.press(kinds[random.below(std::size(kinds))]);
            } else if (pick < 8) {
                hands_on.press(Key::Kind::Letter, letters[random.below(std::size(letters))]);
            }
            if (step % 5 == 0) {
                ui::Canvas canvas(90 + static_cast<int>(random.below(120)), 26 + static_cast<int>(random.below(40)));
                game.draw(canvas);
            }
            CHECK(game.bankroll() >= 0);
            if (check::failures() > 10) return;
        }
        // Unless somebody refilled, not a chip appeared or vanished, whatever the game was doing at the end.
        const app::Profile now = kept(folder.path());
        if (refills(now) == refilled) {
            CHECK_EQ(all_chips(now), total);
            ++checked;
        }
    }
    CHECK(checked >= 3);
}
