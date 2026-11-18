// The game as the player sees it: the menus, the table and the keys. It
// draws onto a Canvas and gets keys and the time from outside, so it can be
// run without a terminal.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "app/profile.hpp"
#include "host/dealer.hpp"
#include "online/guest_table.hpp"
#include "online/host_room.hpp"
#include "online/online.hpp"
#include "host/roster.hpp"
#include "ui/avatar.hpp"
#include "ui/canvas.hpp"
#include "ui/lang.hpp"
#include "ui/table_draw.hpp"

namespace poher::app {

// The version of the program, shown to friends so that they know to update.
inline constexpr const char* kVersion = "0.6";

struct Key {
    enum class Kind { Letter, Enter, Escape, Tab, Backspace, Left, Right, Up, Down };
    Kind kind = Kind::Letter;
    char32_t letter = 0;  // for Kind::Letter: what was typed
};

class Game {
public:
    // With a data folder the profile is read from it and kept up to date
    // there; without one everything lives until the game is closed.
    // `network` starts the network; without it the game is for one player only.
    Game(Settings settings, const std::filesystem::path& avatar_folder, std::uint64_t seed,
         const std::filesystem::path& data_folder = {}, const online::Online::Factory& network = {});
    ~Game();

    // Moves time on. `now` is in milliseconds. True if the picture has
    // changed since it was last drawn.
    bool tick(std::int64_t now);
    void key(const Key& key);
    void draw(ui::Canvas& canvas);

    bool quit() const { return quit_; }
    // True once after each moment the terminal should ring.
    bool take_bell();
    // Text to put on the clipboard, once.
    std::string take_clipboard() { return std::exchange(clipboard_, {}); }

    // For tests.
    game::Chips bankroll() const { return profile_.bankroll; }
    const Profile& profile() const { return profile_; }
    const Settings& settings() const { return settings_; }
    const std::vector<PastHand>& history() const { return history_; }
    bool at_table() const { return dealer_ != nullptr || guest_ != nullptr; }
    bool paused() const { return paused_; }
    const host::Dealer* dealer() const { return dealer_.get(); }
    const online::GuestTable* guest() const { return guest_.get(); }
    online::Online* network() { return online_.get(); }
    // The table as this player sees it.
    host::TableView table_view() const;

private:
    enum class Screen { Menu, Setup, Settings, Table, Profile, History, Friends };

    const ui::Words& words() const { return ui::words(settings_.lang); }

    // Menus.
    void menu_key(const Key& key);
    void setup_key(const Key& key);
    void settings_key(const Key& key);
    void draw_menu(ui::Canvas& canvas);
    void draw_setup(ui::Canvas& canvas);
    void draw_settings(ui::Canvas& canvas);
    std::vector<std::string> menu_items() const;
    void profile_key(const Key& key);
    void history_key(const Key& key);
    void draw_profile(ui::Canvas& canvas);
    void draw_history(ui::Canvas& canvas);
    void open_profile(Screen from);
    void open_history(Screen from);
    std::vector<std::string> free_avatars() const;
    bool can_refill() const;
    void refill();

    // Friends and playing with them.
    void open_friends(Screen from);
    void friends_key(const Key& key);
    void draw_friends(ui::Canvas& canvas);
    int friend_rows() const;
    std::string mate_text(const online::Mate& mate) const;
    void invitation_key(const Key& key);
    void draw_invitation(ui::Canvas& canvas);
    bool invitation_shown() const;
    void join_table(const online::PeerId& host, const std::string& name, const online::TableInfo& table);
    void close_guest_table(game::Chips take_home, std::string why);
    void table_message(const online::PeerId& from, online::Kind kind, net::Reader& body);
    void friend_link(const online::PeerId& who, bool online);
    void open_room();
    int make_room();
    void watch_network();
    void update_status();
    std::string refusal_text(online::Refusal why) const;
    void draw_joining(ui::Canvas& canvas);

    // The table, wherever it is dealt.
    bool seated() const;
    game::Chips my_stack() const;
    game::Stakes stakes() const;
    std::uint64_t table_changes() const;
    bool with_people(const host::TableView& view) const;

    // Setting up a table.
    int first_bot_row() const;
    bool level_open(std::size_t level) const;
    int picked_bots() const;
    bool bot_sits(std::size_t bot, std::size_t level) const;
    void bot_pays(std::size_t bot, game::Chips amount);
    void fix_setup(bool fill_seats);

    // The table.
    void start_table();
    void leave_table();
    void table_key(const Key& key);
    void pause_key(const Key& key);
    void raise_key(const Key& key);
    void draw_table(ui::Canvas& canvas);
    void draw_pause(ui::Canvas& canvas);
    std::vector<std::string> pause_items() const;
    enum class PauseItem { Resume, Settings, Friends, Refill, Leave, Quit };
    std::vector<PauseItem> pause_list() const;
    enum class MenuItem { Play, Friends, Profile, History, Refill, Settings, Quit };
    std::vector<MenuItem> menu_list() const;
    void take_notices();
    void after_hand();
    void note_play();
    int profile_rows() const;
    void save_profile();
    game::Chips chips_at_table() const;
    void buy_chips();
    game::Chips chips_to_buy(bool& needs_refill) const;
    void start_raise();
    void make_move(game::Action action);
    void flash(std::string text);
    void work_out_hint(const host::TableView& view);

    Settings settings_;
    ui::Avatars avatars_;
    game::Random random_;
    Screen screen_ = Screen::Menu;
    Screen settings_from_ = Screen::Menu;
    bool quit_ = false;
    bool bell_ = false;
    bool dirty_ = true;
    std::int64_t now_ = 0;

    Storage storage_;
    Profile profile_;
    std::vector<PastHand> history_;  // the latest hands, the newest first

    int menu_row_ = 0;
    int settings_row_ = 0;

    // The profile and history screens; Esc goes back to where they were opened from.
    Screen back_to_ = Screen::Menu;
    int profile_row_ = 0;
    bool typing_nick_ = false;
    std::string nick_draft_;
    int history_row_ = 0;
    bool history_open_ = false;
    int history_scroll_ = 0;

    // The table being set up.
    int setup_row_ = 0;
    std::size_t level_ = 0;
    int seats_ = 6;
    game::Chips buy_in_ = 0;
    std::array<bool, host::kBots.size()> picked_{};
    bool table_open_ = true;  // friends may sit down without an invitation
    int turn_seconds_ = 30;   // the clock on moves when friends sit at the table; 0 = none

    // The network.
    std::unique_ptr<online::Online> online_;
    std::unique_ptr<online::HostRoom> room_;     // our table, as friends see it
    std::unique_ptr<online::GuestTable> guest_;  // a friend's table we sit at
    std::string clipboard_;
    int friends_row_ = 0;
    bool typing_code_ = false;
    std::string code_draft_;
    std::string invitation_seen_;  // the invitation the bell has rung for
    bool guest_seated_ = false;
    game::Chips saved_stack_ = -1;  // at a friend's table: our chips as the profile file has them
    int deck_alarms_ = 0;           // at a friend's table: decks that did not check out, as told
    std::vector<int> seat_order_;   // seats of bots in the order they sat down

    // The table being played at.
    std::unique_ptr<host::Dealer> dealer_;
    int my_seat_ = 0;
    std::string my_name_;
    game::Chips my_mark_ = 0;             // our chips at the table when they were last counted
    bots::Notes my_notes_;                // and what the table had noted about our play by then
    std::vector<int> seat_bot_;           // which of host::kBots sits in a seat; -1 for none
    std::vector<game::Chips> seat_mark_;  // the same count for the bots
    std::uint64_t seen_changes_ = 0;
    bool my_turn_ = false;
    bool paused_ = false;
    int pause_row_ = 0;
    ui::TableLook look_;
    // The second line of the full hint and what it was worked out for.
    std::string hint_more_;
    std::string hint_for_;
    std::string flash_;
    std::int64_t flash_until_ = 0;
};

// The Latin letter on the same key as a Russian letter, lower case; other
// glyphs come back in lower case where they have one. Hot keys work the same
// in both keyboard layouts this way.
char32_t key_letter(char32_t typed);

}  // namespace poher::app
