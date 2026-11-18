// Two whole games, as two friends would run them, joined through a net::Hub:
// they become friends on the friends screen and play at one table.
#include <filesystem>
#include <string>

#include "app/game.hpp"
#include "check.hpp"
#include "net/loop_network.hpp"
#include "ui/canvas.hpp"

using namespace poher;
using app::Game;
using app::Key;
using game::Chips;

namespace {

const std::filesystem::path kAvatars = std::filesystem::path(POHER_SOURCE_DIR) / "avatars";

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

online::Online::Factory on(net::Hub& hub, std::string name) {
    return [&hub, name](net::Events events, std::string&) { return hub.join(name, std::move(events)); };
}

// What both players and the bots of both own, as the files have it.
Chips everything(const std::filesystem::path& folder) {
    app::Profile profile;
    app::Storage(folder).load(profile);
    Chips total = profile.bankroll;
    for (const app::BotAccount& account : profile.bots) total += account.bankroll;
    return total;
}

int refills(const std::filesystem::path& folder) {
    app::Profile profile;
    app::Storage(folder).load(profile);
    int count = profile.refills;
    for (const app::BotAccount& account : profile.bots) count += account.refills;
    return count;
}

struct Pair {
    net::Hub hub;
    Folder ann_folder{"ann"};
    Folder bob_folder{"bob"};
    Game ann{app::Settings{}, kAvatars, 1, ann_folder.path(), on(hub, "Ann")};
    Game bob{app::Settings{}, kAvatars, 2, bob_folder.path(), on(hub, "Bob")};
    std::int64_t now = 0;

    void tick(int times = 1) {
        for (int i = 0; i < times; ++i) {
            now += 100;
            ann.tick(now);
            bob.tick(now);
        }
    }
    void press(Game& game, Key::Kind kind, char32_t letter = 0) {
        game.key(Key{kind, letter});
        tick();
    }
    void type(Game& game, const std::string& text) {
        for (const char c : text) press(game, Key::Kind::Letter, static_cast<char32_t>(c));
    }
    std::string screen(Game& game) {
        ui::Canvas canvas(140, 44);
        game.draw(canvas);
        std::string all;
        for (const std::string& line : canvas.to_text()) all += line + "\n";
        return all;
    }
    // Both play: call or check whatever comes, now and then a fold.
    void play(int steps) {
        for (int i = 0; i < steps; ++i) {
            tick();
            for (Game* game : {&ann, &bob}) {
                if (!game->at_table()) continue;
                const host::TableView view = game->table_view();
                if (view.choices) game->key(Key{Key::Kind::Letter, i % 9 == 0 ? U'f' : U'c'});
                if (view.viewer >= 0 && static_cast<std::size_t>(view.viewer) < view.seats.size()) {
                    const host::SeatView& me = view.seats[static_cast<std::size_t>(view.viewer)];
                    if (me.taken && me.stack == 0 && !me.in_hand && view.chips_coming == 0) {
                        game->key(Key{Key::Kind::Letter, U'b'});
                    }
                }
            }
        }
    }
};

}  // namespace

TEST(two_friends_meet_and_play) {
    Pair pair;
    pair.tick(5);
    CHECK(pair.ann.network() != nullptr);
    CHECK(pair.bob.network() != nullptr);
    if (pair.ann.network() == nullptr || pair.bob.network() == nullptr) return;
    const Chips start = everything(pair.ann_folder.path()) + everything(pair.bob_folder.path());

    // The menu has "Friends" second.
    pair.press(pair.bob, Key::Kind::Down);
    pair.press(pair.bob, Key::Kind::Enter);
    std::string bob_screen = pair.screen(pair.bob);
    CHECK(bob_screen.find("Friends") != std::string::npos);
    CHECK(bob_screen.find(pair.bob.network()->code().substr(0, 20)) != std::string::npos);

    // Bob types Ann's code, with spaces and junk that a paste might bring.
    pair.press(pair.bob, Key::Kind::Enter);
    pair.type(pair.bob, " " + pair.ann.network()->code() + "\n");
    pair.press(pair.bob, Key::Kind::Enter);
    CHECK(pair.screen(pair.bob).find("Request sent") != std::string::npos);
    pair.tick(3);
    // Ann sees the request in the menu and accepts it on the friends screen.
    CHECK(pair.screen(pair.ann).find("Friends  (1)") != std::string::npos);
    pair.press(pair.ann, Key::Kind::Down);
    pair.press(pair.ann, Key::Kind::Enter);
    pair.press(pair.ann, Key::Kind::Down);
    pair.press(pair.ann, Key::Kind::Enter);
    pair.tick(3);
    CHECK_EQ(pair.ann.network()->mates().size(), 1);
    CHECK_EQ(pair.bob.network()->mates().size(), 1);
    CHECK(pair.screen(pair.bob).find("in the menus") != std::string::npos);

    // Ann sets up a table: Esc back to the menu, Play, start (six seats, five bots).
    pair.press(pair.ann, Key::Kind::Escape);
    pair.press(pair.ann, Key::Kind::Up);
    pair.press(pair.ann, Key::Kind::Enter);
    pair.press(pair.ann, Key::Kind::Enter);
    CHECK(pair.ann.at_table());
    pair.tick(3);
    CHECK(pair.screen(pair.bob).find("at own table") != std::string::npos);

    // Bob goes to the second row (Ann) and sits down; the table is full, so a bot gives way.
    pair.press(pair.bob, Key::Kind::Down);
    pair.press(pair.bob, Key::Kind::Enter);
    CHECK(pair.bob.at_table());
    CHECK(pair.bob.guest() != nullptr);
    pair.play(3000);
    CHECK(pair.bob.guest() != nullptr);
    if (pair.bob.guest() == nullptr) return;
    CHECK(pair.bob.guest()->state() == online::GuestTable::State::Seated);
    CHECK(pair.bob.guest()->view().hand_number > 5);
    CHECK(pair.screen(pair.bob).find("'s table") != std::string::npos);
    // Five bots were there; one made room.
    int bots = 0;
    for (const host::SeatView& seat : pair.ann.table_view().seats) bots += seat.taken && seat.bot;
    CHECK_EQ(bots, 4);
    CHECK(pair.bob.profile().stats.play.hands > 5);
    CHECK(!pair.bob.history().empty());
    CHECK(pair.bob.profile().stats.from_people != 0 || pair.bob.profile().stats.hands_won == 0);

    // In the middle of it all, the files hold every chip.
    if (refills(pair.ann_folder.path()) + refills(pair.bob_folder.path()) == 0) {
        const Chips middle = everything(pair.ann_folder.path()) + everything(pair.bob_folder.path());
        // Bob's chips in the pot of a hand in progress are counted as given up.
        CHECK(middle <= start);
        CHECK(middle >= start - 400);
    }

    // Bob gets up; then Ann calls him back with an invitation.
    pair.press(pair.bob, Key::Kind::Escape);
    pair.press(pair.bob, Key::Kind::Up);
    pair.press(pair.bob, Key::Kind::Up);
    pair.press(pair.bob, Key::Kind::Enter);
    CHECK(!pair.bob.at_table());
    pair.play(200);
    // Ann: Esc, "Invite friends", Bob, Enter.
    pair.press(pair.ann, Key::Kind::Escape);
    pair.press(pair.ann, Key::Kind::Down);
    pair.press(pair.ann, Key::Kind::Down);
    pair.press(pair.ann, Key::Kind::Enter);
    pair.press(pair.ann, Key::Kind::Down);
    pair.press(pair.ann, Key::Kind::Enter);
    pair.press(pair.ann, Key::Kind::Escape);
    pair.tick(3);
    CHECK(pair.screen(pair.bob).find("invites you") != std::string::npos);
    CHECK(pair.bob.take_bell());
    pair.press(pair.bob, Key::Kind::Enter);
    pair.play(1500);
    CHECK(pair.bob.guest() != nullptr && pair.bob.guest()->state() == online::GuestTable::State::Seated);

    // Ann closes her table: Bob is back in the menu with his chips.
    pair.ann.key(Key{Key::Kind::Escape});
    if (!pair.ann.paused()) pair.ann.key(Key{Key::Kind::Escape});
    pair.press(pair.ann, Key::Kind::Up);
    pair.press(pair.ann, Key::Kind::Up);
    pair.press(pair.ann, Key::Kind::Enter);
    CHECK(!pair.ann.at_table());
    pair.tick(5);
    CHECK(!pair.bob.at_table());
    CHECK(pair.screen(pair.bob).find("closed the table") != std::string::npos);

    // Nobody refilled: everything both own is what they started with, give or take
    // what Bob left in the pot when he got up in the middle of a hand.
    if (refills(pair.ann_folder.path()) + refills(pair.bob_folder.path()) == 0) {
        const Chips end = everything(pair.ann_folder.path()) + everything(pair.bob_folder.path());
        CHECK(end <= start);
        CHECK(end >= start - 400);
    }
}

TEST(a_guest_whose_host_vanishes_goes_home) {
    Pair pair;
    pair.tick(3);
    pair.press(pair.bob, Key::Kind::Down);
    pair.press(pair.bob, Key::Kind::Enter);
    pair.press(pair.bob, Key::Kind::Enter);
    pair.type(pair.bob, pair.ann.network()->code());
    pair.press(pair.bob, Key::Kind::Enter);
    pair.tick(3);
    pair.ann.network()->accept(pair.bob.network()->id());
    pair.tick(3);
    pair.press(pair.ann, Key::Kind::Enter);
    pair.press(pair.ann, Key::Kind::Enter);
    pair.tick(3);
    pair.press(pair.bob, Key::Kind::Down);
    pair.press(pair.bob, Key::Kind::Enter);
    pair.play(1500);
    CHECK(pair.bob.guest() != nullptr && pair.bob.guest()->state() == online::GuestTable::State::Seated);
    const Chips bankroll = pair.bob.bankroll();

    // Ann's cable is pulled for good.
    pair.hub.set_online(pair.ann.network()->id(), false);
    pair.tick(5);
    CHECK(pair.bob.at_table());
    CHECK(pair.screen(pair.bob).find("No connection with the host") != std::string::npos);
    pair.tick(3 * 60 * 10 + 20);
    CHECK(!pair.bob.at_table());
    CHECK(pair.bob.bankroll() > bankroll);
    CHECK(pair.screen(pair.bob).find("The host is gone") != std::string::npos);
}
