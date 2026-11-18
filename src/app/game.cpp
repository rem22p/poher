#include "app/game.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <functional>
#include <span>
#include <thread>
#include <utility>

#include "game/equity.hpp"
#include "game/outs.hpp"

#include "app/menu_parts.hpp"
#include "ui/card_art.hpp"

namespace poher::app {

using game::Chips;
using ui::Canvas;
using ui::kKeep;
namespace color = ui::color;
using parts::draw_bars;
using parts::draw_choice;
using parts::draw_row;
using parts::kColumn;
using parts::kFlashTime;

namespace {

constexpr int kSettings = 4;  // rows on the settings screen
constexpr std::size_t kHistoryShown = 300;  // hands kept at hand for the history screen
constexpr int kMemoryHands = 200;  // at a new table what the bots remember of a player weighs as this many hands at most

// Old notes fade: scaled down to `most` hands, so that the hands to come weigh more than the ones long ago.
bots::Notes faded(bots::Notes notes, int most) {
    if (notes.hands <= most) return notes;
    const double scale = static_cast<double>(most) / notes.hands;
    for (int* count : {&notes.entered, &notes.raised, &notes.bets, &notes.calls, &notes.faced, &notes.gave_up}) {
        *count = static_cast<int>(std::lround(*count * scale));
    }
    notes.hands = most;
    return notes;
}

}  // namespace

char32_t key_letter(char32_t typed) {
    if (typed >= U'A' && typed <= U'Z') return typed - U'A' + U'a';
    if (typed >= U'А' && typed <= U'Я') typed += 0x20;
    static constexpr std::pair<char32_t, char32_t> same_key[] = {
        {U'й', U'q'}, {U'ц', U'w'}, {U'у', U'e'}, {U'к', U'r'}, {U'е', U't'}, {U'н', U'y'}, {U'г', U'u'},
        {U'ш', U'i'}, {U'щ', U'o'}, {U'з', U'p'}, {U'ф', U'a'}, {U'ы', U's'}, {U'в', U'd'}, {U'а', U'f'},
        {U'п', U'g'}, {U'р', U'h'}, {U'о', U'j'}, {U'л', U'k'}, {U'д', U'l'}, {U'я', U'z'}, {U'ч', U'x'},
        {U'с', U'c'}, {U'м', U'v'}, {U'и', U'b'}, {U'т', U'n'}, {U'ь', U'm'},
    };
    for (const auto& [russian, latin] : same_key) {
        if (typed == russian) return latin;
    }
    return typed;
}

Game::Game(Settings settings, const std::filesystem::path& avatar_folder, std::uint64_t seed,
           const std::filesystem::path& data_folder, const online::Online::Factory& network)
    : settings_(settings), random_(seed), storage_(data_folder) {
    avatars_.open_folder(avatar_folder);
    storage_.load(profile_);
    history_ = storage_.history(kHistoryShown);
    fix_setup(true);
    if (network) {
        online_ = std::make_unique<online::Online>(network);
        if (!online_->started()) {
            online_.reset();
        } else {
            online_->on_table = [this](const online::PeerId& from, online::Kind kind, net::Reader& body) {
                table_message(from, kind, body);
            };
            online_->on_link = [this](const online::PeerId& who, bool online) { friend_link(who, online); };
            online_->set_name(profile_.nick.empty() ? std::string("POHER") : profile_.nick);
        }
    }
}

Game::~Game() {
    // Friends at our table hear that it is gone, the host that we got up.
    const bool friends_wait = (room_ && room_->guests() > 0) || guest_;
    if (room_) room_->close();
    if (guest_) guest_->leave();
    if (online_) {
        // A moment for the news to go out; otherwise they learn it in three minutes.
        for (int i = 0; i < (friends_wait ? 25 : 3); ++i) {
            online_->poll();
            if (friends_wait) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

// Our chips at the table as they were when the last hand ended. Chips are
// counted between hands only: a hand in progress has not happened yet.
Chips Game::chips_at_table() const {
    if (guest_) return guest_->state() == online::GuestTable::State::Seated ? guest_->stack() : 0;
    return dealer_ && dealer_->table().seat(my_seat_).taken ? my_mark_ : 0;
}

// ---------------------------------------------------------------- the table, wherever it is dealt
host::TableView Game::table_view() const {
    if (guest_) return guest_->view();
    if (dealer_) return dealer_->view(my_seat_);
    return {};
}

bool Game::seated() const {
    if (guest_) return guest_->state() == online::GuestTable::State::Seated && guest_->seat() >= 0;
    return dealer_ && dealer_->table().seat(my_seat_).taken;
}

// Our chips right now, the bet in front not counted while a hand is on.
Chips Game::my_stack() const {
    if (guest_) return guest_->stack();
    return dealer_ ? dealer_->stack(my_seat_) : 0;
}

game::Stakes Game::stakes() const {
    if (guest_) return guest_->view().stakes;
    return dealer_ ? dealer_->table().stakes() : game::Stakes{};
}

std::uint64_t Game::table_changes() const {
    if (guest_) return guest_->changes();
    return dealer_ ? dealer_->changes() : 0;
}

// Somebody else than bots was dealt into the hand.
bool Game::with_people(const host::TableView& view) const {
    for (std::size_t seat = 0; seat < view.seats.size(); ++seat) {
        const host::SeatView& other = view.seats[seat];
        if (static_cast<int>(seat) != my_seat_ && other.taken && !other.bot && other.in_hand) return true;
    }
    return false;
}

// Writes the profile down. Chips at a table are counted as already back in
// the bankroll: if the game is closed in the middle, the table is not lost.
// A hand cut short by that is as if it was never dealt, for the bots as well.
void Game::save_profile() {
    if (!storage_.enabled()) return;
    Profile whole = profile_;
    whole.bankroll += chips_at_table();
    for (std::size_t seat = 0; seat < seat_bot_.size(); ++seat) {
        if (seat_bot_[seat] >= 0) whole.bots[static_cast<std::size_t>(seat_bot_[seat])].bankroll += seat_mark_[seat];
    }
    storage_.save(whole);
}

bool Game::take_bell() {
    return std::exchange(bell_, false);
}

void Game::flash(std::string text) {
    flash_ = std::move(text);
    flash_until_ = now_ + kFlashTime;
    dirty_ = true;
}

bool Game::tick(std::int64_t now) {
    const bool new_second = now / 1000 != now_ / 1000;
    now_ = now;
    watch_network();
    if (dealer_) {
        dealer_->tick(now);
        take_notices();
        if (room_) {
            room_->tick();
            take_notices();
        }
        // The clock of a move counts down on the screen.
        if (new_second && dealer_->clock().turn_ms > 0 && dealer_->people() >= 2) dirty_ = true;
    }
    if (guest_) {
        guest_->tick(now);
        my_seat_ = std::max(guest_->seat(), 0);
        take_notices();
    }
    if (guest_ && guest_->state() == online::GuestTable::State::Seated) {
        // The file keeps what we have behind: closing the window mid-hand gives up the chips in the pot.
        if (my_stack() != saved_stack_) {
            save_profile();
            saved_stack_ = my_stack();
        }
    }
    if (at_table() && table_changes() != seen_changes_) {
        seen_changes_ = table_changes();
        dirty_ = true;
        const host::TableView view = table_view();
        const bool mine = view.choices.has_value();
        if (mine && !my_turn_) {
            // A new decision: start from the safe button.
            bell_ = settings_.sound;
            look_.focus = 1;
        }
        if (mine != my_turn_) {
            look_.raising = false;
            look_.typed.clear();
        }
        my_turn_ = mine;
    }
    if (!flash_.empty() && now >= flash_until_) {
        flash_.clear();
        dirty_ = true;
    }
    return dirty_;
}

void Game::key(const Key& key) {
    dirty_ = true;
    if (invitation_shown()) {
        invitation_key(key);
        return;
    }
    switch (screen_) {
        case Screen::Menu: menu_key(key); break;
        case Screen::Setup: setup_key(key); break;
        case Screen::Settings: settings_key(key); break;
        case Screen::Table: table_key(key); break;
        case Screen::Profile: profile_key(key); break;
        case Screen::History: history_key(key); break;
        case Screen::Friends: friends_key(key); break;
    }
}

void Game::draw(Canvas& canvas) {
    dirty_ = false;
    if (!ui::table_fits(canvas.width(), canvas.height())) {
        ui::draw_too_small(canvas);
        return;
    }
    switch (screen_) {
        case Screen::Menu: draw_menu(canvas); break;
        case Screen::Setup: draw_setup(canvas); break;
        case Screen::Settings: draw_settings(canvas); break;
        case Screen::Table: draw_table(canvas); break;
        case Screen::Profile: draw_profile(canvas); break;
        case Screen::History: draw_history(canvas); break;
        case Screen::Friends: draw_friends(canvas); break;
    }
    if (invitation_shown()) draw_invitation(canvas);
}

// ---------------------------------------------------------------- the bankroll
bool Game::can_refill() const {
    return profile_.bankroll + chips_at_table() < host::kRefillBelow;
}

void Game::refill() {
    // Back to the starting amount, chips at the table counted in.
    profile_.bankroll = host::kStartBankroll - chips_at_table();
    ++profile_.refills;
    save_profile();
    flash(ui::fill(words().refilled, {ui::chips_text(host::kStartBankroll)}));
}

// ---------------------------------------------------------------- the main menu
std::vector<Game::MenuItem> Game::menu_list() const {
    std::vector<MenuItem> list{MenuItem::Play};
    if (online_) list.push_back(MenuItem::Friends);
    list.push_back(MenuItem::Profile);
    list.push_back(MenuItem::History);
    if (can_refill()) list.push_back(MenuItem::Refill);
    list.push_back(MenuItem::Settings);
    list.push_back(MenuItem::Quit);
    return list;
}

std::vector<std::string> Game::menu_items() const {
    const ui::Words& w = words();
    std::vector<std::string> items;
    for (const MenuItem item : menu_list()) {
        switch (item) {
            case MenuItem::Play: items.emplace_back(w.menu_play); break;
            case MenuItem::Friends: {
                std::string text(w.menu_friends);
                if (!online_->requests().empty()) text += "  (" + std::to_string(online_->requests().size()) + ")";
                items.push_back(std::move(text));
                break;
            }
            case MenuItem::Profile: items.emplace_back(w.menu_profile); break;
            case MenuItem::History: items.emplace_back(w.menu_history); break;
            case MenuItem::Refill: items.push_back(ui::fill(w.menu_refill, {ui::chips_text(host::kStartBankroll)})); break;
            case MenuItem::Settings: items.emplace_back(w.menu_settings); break;
            case MenuItem::Quit: items.emplace_back(w.menu_quit); break;
        }
    }
    return items;
}

void Game::menu_key(const Key& key) {
    const std::vector<MenuItem> list = menu_list();
    const int count = static_cast<int>(list.size());
    menu_row_ = std::clamp(menu_row_, 0, count - 1);
    switch (key.kind) {
        case Key::Kind::Up: menu_row_ = (menu_row_ + count - 1) % count; break;
        case Key::Kind::Down: menu_row_ = (menu_row_ + 1) % count; break;
        case Key::Kind::Enter:
            switch (list[static_cast<std::size_t>(menu_row_)]) {
                case MenuItem::Play:
                    fix_setup(false);
                    setup_row_ = 0;
                    screen_ = Screen::Setup;
                    break;
                case MenuItem::Friends: open_friends(Screen::Menu); break;
                case MenuItem::Profile: open_profile(Screen::Menu); break;
                case MenuItem::History: open_history(Screen::Menu); break;
                case MenuItem::Refill:
                    refill();
                    menu_row_ = 0;
                    break;
                case MenuItem::Settings:
                    settings_from_ = Screen::Menu;
                    settings_row_ = 0;
                    screen_ = Screen::Settings;
                    break;
                case MenuItem::Quit: quit_ = true; break;
            }
            break;
        default: break;
    }
}

void Game::draw_menu(Canvas& canvas) {
    const ui::Words& w = words();
    draw_bars(canvas, w, profile_.bankroll, w.menu_keys);
    const std::vector<std::string> items = menu_items();
    menu_row_ = std::clamp(menu_row_, 0, static_cast<int>(items.size()) - 1);

    const int centre = canvas.width() / 2;
    const int height = 7 + 3 + 2 * static_cast<int>(items.size());
    int y = std::max(2, (canvas.height() - height) / 2);
    ui::draw_logo(canvas, centre - ui::logo_width(2) / 2, y, 2);
    y += 10;
    const int width = 40;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const bool picked = static_cast<int>(i) == menu_row_;
        draw_row(canvas, centre - width / 2, y, width, picked);
        canvas.put(centre - width / 2 + 3, y, items[i], picked ? color::kYellow : color::kText,
                   picked ? color::kSeatActive : color::kBackground, picked);
        y += 2;
    }
    if (!flash_.empty()) canvas.put_centred(centre, y + 1, flash_, color::kYellow);
}

// ---------------------------------------------------------------- settings
void Game::settings_key(const Key& key) {
    const bool change = key.kind == Key::Kind::Left || key.kind == Key::Kind::Right || key.kind == Key::Kind::Enter ||
                        (key.kind == Key::Kind::Letter && key.letter == U' ');
    switch (key.kind) {
        case Key::Kind::Up: settings_row_ = (settings_row_ + kSettings - 1) % kSettings; return;
        case Key::Kind::Down: settings_row_ = (settings_row_ + 1) % kSettings; return;
        case Key::Kind::Escape: screen_ = settings_from_; return;
        default: break;
    }
    if (!change) return;
    // Most settings have two values, so any change flips them.
    if (settings_row_ == 0) settings_.lang = settings_.lang == ui::Lang::En ? ui::Lang::Ru : ui::Lang::En;
    if (settings_row_ == 1) {
        const int step = key.kind == Key::Kind::Left ? 2 : 1;
        settings_.hint = static_cast<ui::Hint>((static_cast<int>(settings_.hint) + step) % 3);
    }
    if (settings_row_ == 2) settings_.sound = !settings_.sound;
    if (settings_row_ == 3) {
        settings_.talk = !settings_.talk;
        if (dealer_ != nullptr) dealer_->set_talk(settings_.talk);
    }
    storage_.save(settings_);
}

void Game::draw_settings(Canvas& canvas) {
    const ui::Words& w = words();
    draw_bars(canvas, w, profile_.bankroll, w.settings_keys);
    const int x = (canvas.width() - kColumn) / 2;
    int y = std::max(2, canvas.height() / 2 - 5);
    canvas.put(x + 2, y, w.settings_title, color::kWhite, kKeep, true);
    y += 2;

    const std::string_view hints[] = {w.hint_off, w.hint_name, w.hint_full};
    const std::string_view labels[] = {w.settings_language, w.settings_hint, w.settings_sound, w.settings_talk};
    const std::string_view values[] = {w.language, hints[static_cast<int>(settings_.hint)],
                                       settings_.sound ? w.on : w.off, settings_.talk ? w.on_many : w.off_many};
    for (int row = 0; row < kSettings; ++row) {
        const bool picked = row == settings_row_;
        const ui::Rgb back = picked ? color::kSeatActive : color::kBackground;
        draw_row(canvas, x, y, kColumn, picked);
        canvas.put(x + 2, y, labels[row], picked ? color::kYellow : color::kText, back, picked);
        draw_choice(canvas, x + 26, y, values[row], picked, true, true);
        y += 2;
    }
}

// ---------------------------------------------------------------- the bots' own money
// A bot sits where its bankroll covers the biggest buy-in. Its starting
// bankroll always counts: a bot that went broke refills to it.
bool Game::bot_sits(std::size_t bot, std::size_t level) const {
    const Chips has = std::max(profile_.bots[bot].bankroll, host::start_bankroll(host::kBots[bot].strength));
    return has >= host::kLevels[level].bankroll;
}

// Takes chips for a table out of the bot's bankroll, refilling it first if there is not enough.
void Game::bot_pays(std::size_t bot, Chips amount) {
    BotAccount& account = profile_.bots[bot];
    if (account.bankroll < amount) {
        account.bankroll = std::max(host::start_bankroll(host::kBots[bot].strength), amount);
        ++account.refills;
    }
    account.bankroll -= amount;
}

// ---------------------------------------------------------------- setting up a table
bool Game::level_open(std::size_t level) const {
    const host::Level& entry = host::kLevels[level];
    return profile_.bankroll >= entry.bankroll && profile_.bankroll >= entry.stakes.min_buy_in;
}

int Game::picked_bots() const {
    return static_cast<int>(std::count(picked_.begin(), picked_.end(), true));
}

// Keeps the choices consistent after one of them changed. With `fill_seats`
// the free seats are filled with bots picked at random.
void Game::fix_setup(bool fill_seats) {
    const game::Stakes& stakes = host::kLevels[level_].stakes;
    const Chips most = std::min(stakes.max_buy_in, std::max(profile_.bankroll, stakes.min_buy_in));
    buy_in_ = buy_in_ == 0 ? most : std::clamp(buy_in_, stakes.min_buy_in, most);

    for (std::size_t i = 0; i < host::kBots.size(); ++i) {
        if (!bot_sits(i, level_)) picked_[i] = false;
    }
    // Too many for the table: the last ones get up.
    for (std::size_t i = host::kBots.size(); i-- > 0 && picked_bots() > seats_ - 1;) picked_[i] = false;
    if (!fill_seats && picked_bots() > 0) return;

    std::vector<std::size_t> spare;
    for (std::size_t i = 0; i < host::kBots.size(); ++i) {
        if (!picked_[i] && bot_sits(i, level_)) spare.push_back(i);
    }
    while (picked_bots() < seats_ - 1 && !spare.empty()) {
        const std::size_t at = random_.below(static_cast<std::uint32_t>(spare.size()));
        picked_[spare[at]] = true;
        spare.erase(spare.begin() + static_cast<std::ptrdiff_t>(at));
    }
}

// The rows of the setup screen: stakes, seats, buy-in, then with the network
// whether friends may come and the clock, then the bots and "start".
int Game::first_bot_row() const {
    return online_ ? 5 : 3;
}

void Game::setup_key(const Key& key) {
    const int bots = static_cast<int>(host::kBots.size());
    const int first_bot = first_bot_row();
    const int rows = first_bot + bots + 1;
    const int direction = key.kind == Key::Kind::Left ? -1 : key.kind == Key::Kind::Right ? 1 : 0;
    const game::Stakes& stakes = host::kLevels[level_].stakes;

    switch (key.kind) {
        case Key::Kind::Up: setup_row_ = (setup_row_ + rows - 1) % rows; return;
        case Key::Kind::Down: setup_row_ = (setup_row_ + 1) % rows; return;
        case Key::Kind::Escape: screen_ = Screen::Menu; return;
        case Key::Kind::Enter:
            if (level_open(level_) && (picked_bots() >= 1 || (online_ && table_open_))) start_table();
            return;
        default: break;
    }

    if (direction != 0 && setup_row_ == 0) {
        const int level = static_cast<int>(level_) + direction;
        if (level < 0 || level >= static_cast<int>(host::kLevels.size())) return;
        level_ = static_cast<std::size_t>(level);
        buy_in_ = 0;
        fix_setup(true);
    } else if (direction != 0 && setup_row_ == 1) {
        seats_ = std::clamp(seats_ + direction, 2, 6);
        fix_setup(true);
    } else if (direction != 0 && setup_row_ == 2) {
        // In steps of ten big blinds.
        buy_in_ += direction * 10 * stakes.big_blind;
        fix_setup(false);
    } else if (direction != 0 && online_ && setup_row_ == 3) {
        table_open_ = !table_open_;
    } else if (direction != 0 && online_ && setup_row_ == 4) {
        static constexpr int kClocks[] = {0, 15, 30, 60};
        int at = 0;
        while (at < 3 && kClocks[at] != turn_seconds_) ++at;
        turn_seconds_ = kClocks[std::clamp(at + direction, 0, 3)];
    } else if (setup_row_ >= first_bot && setup_row_ < first_bot + bots &&
               (direction != 0 || (key.kind == Key::Kind::Letter && key.letter == U' '))) {
        const auto bot = static_cast<std::size_t>(setup_row_ - first_bot);
        if (!bot_sits(bot, level_)) return;
        if (picked_[bot]) {
            picked_[bot] = false;
        } else if (picked_bots() < seats_ - 1) {
            picked_[bot] = true;
        }
    }
}

void Game::draw_setup(Canvas& canvas) {
    const ui::Words& w = words();
    draw_bars(canvas, w, profile_.bankroll, w.setup_keys);
    const int bots = static_cast<int>(host::kBots.size());
    const int x = (canvas.width() - kColumn) / 2;
    const int first_bot = first_bot_row();
    int y = std::max(2, (canvas.height() - (bots + 14 + (first_bot - 3))) / 2);
    const int value_x = x + 16;
    const int note_x = x + 40;
    const host::Level& level = host::kLevels[level_];
    const game::Stakes& stakes = level.stakes;

    canvas.put(x + 2, y, online_ ? w.setup_title_online : w.setup_title, color::kWhite, kKeep, true);
    y += 2;

    const auto label = [&](int row, std::string_view text) {
        const bool picked = row == setup_row_;
        draw_row(canvas, x, y, kColumn, picked);
        canvas.put(x + 2, y, text, picked ? color::kYellow : color::kText,
                   picked ? color::kSeatActive : color::kBackground, picked);
        return picked;
    };

    bool picked = label(0, w.setup_stakes);
    const std::string stake_text = std::string(w.level_names[level_]) + "  " + ui::chips_text(stakes.small_blind) +
                                   "/" + ui::chips_text(stakes.big_blind);
    draw_choice(canvas, value_x, y, stake_text, picked, level_ > 0, level_ + 1 < host::kLevels.size());
    if (!level_open(level_)) {
        const Chips need = std::max(level.bankroll, stakes.min_buy_in);
        canvas.put(note_x, y, ui::fill(w.setup_need_bankroll, {ui::chips_text(need)}), color::kRed, kKeep);
    }
    ++y;

    picked = label(1, w.setup_seats);
    draw_choice(canvas, value_x, y, std::to_string(seats_), picked, seats_ > 2, seats_ < 6);
    ++y;

    picked = label(2, w.setup_buy_in);
    const Chips most = std::min(stakes.max_buy_in, std::max(profile_.bankroll, stakes.min_buy_in));
    draw_choice(canvas, value_x, y, ui::chips_text(buy_in_), picked, buy_in_ > stakes.min_buy_in, buy_in_ < most);
    canvas.put(note_x, y, ui::fill(w.setup_range, {ui::chips_text(stakes.min_buy_in), ui::chips_text(most)}),
               color::kDim, kKeep);
    ++y;

    // With the network: who else may come, and how long a move may take when they do.
    if (online_) {
        picked = label(3, w.setup_friends);
        draw_choice(canvas, value_x, y, table_open_ ? w.setup_open : w.setup_invited, picked, true, true);
        ++y;
        picked = label(4, w.setup_clock);
        const std::string clock =
            turn_seconds_ == 0 ? std::string(w.setup_no_clock) : ui::fill(w.clock_left, {std::to_string(turn_seconds_)});
        draw_choice(canvas, value_x, y, clock, picked, turn_seconds_ > 0, turn_seconds_ < 60);
        canvas.put(note_x, y, w.setup_clock_note, color::kDim, kKeep);
        ++y;
    }
    ++y;

    canvas.put(x + 2, y, w.setup_bots, color::kText);
    canvas.put(value_x, y, ui::fill(w.setup_pick_bots, {std::to_string(seats_ - 1)}), color::kDim);
    ++y;
    const int list_y = y;
    for (int i = 0; i < bots; ++i) {
        const host::Bot& bot = host::kBots[static_cast<std::size_t>(i)];
        const bool here = bot_sits(static_cast<std::size_t>(i), level_);
        const bool seated = picked_[static_cast<std::size_t>(i)];
        const bool row_picked = first_bot + i == setup_row_;
        const ui::Rgb back = row_picked ? color::kSeatActive : color::kBackground;
        draw_row(canvas, x, y, kColumn, row_picked);
        canvas.put(x + 3, y, seated ? "■" : "□", seated ? color::kGreen : color::kDim, back);
        canvas.put(x + 6, y, bot.name, !here ? color::kDim : seated ? color::kWhite : color::kText, back, seated);
        canvas.put(x + 13, y, " BOT ", here ? color::kWhite : color::kDim,
                   here ? color::kBotTag : ui::mix(color::kBotTag, back, 0.6), true);
        const std::string_view strength = w.strength_names[static_cast<std::size_t>(bot.strength)];
        canvas.put(x + 20, y, here ? strength : w.setup_not_here, color::kDim, back);
        ++y;
    }

    // The picture of the bot the cursor is on, and how it has been doing.
    if (setup_row_ >= first_bot && setup_row_ < first_bot + bots) {
        const auto index = static_cast<std::size_t>(setup_row_ - first_bot);
        const host::Bot& bot = host::kBots[index];
        const ui::AvatarArt& art = avatars_.art(std::string(bot.avatar), 16, true, color::kBotTag);
        ui::draw_avatar(canvas, x + kColumn - 18, list_y, art);
        const BotAccount& account = profile_.bots[index];
        const std::string result = (account.won > 0 ? "+" : "") + ui::chips_text(account.won);
        canvas.put(x + 2, y,
                   ui::fit_text(ui::fill(w.setup_bot_account, {std::string(bot.name), ui::chips_text(account.bankroll),
                                                               ui::chips_text(account.hands), result}),
                                kColumn - 4),
                   color::kDim);
    }
    y += 2;

    const bool ready = level_open(level_) && (picked_bots() >= 1 || (online_ && table_open_));
    const bool on_start = setup_row_ == first_bot + bots;
    draw_row(canvas, x, y, kColumn, on_start);
    const ui::Rgb face = ready ? (on_start ? color::kCall : ui::mix(color::kCall, color::kBackground, 0.55))
                               : color::kChip;
    canvas.put(x + 2, y, "  " + std::string(w.setup_start) + "  ", ready ? color::kWhite : color::kDim, face, on_start);
    canvas.put(x + 2 + ui::text_width(w.setup_start) + 6, y, "Enter", color::kDim, kKeep);
}

// ---------------------------------------------------------------- the table
void Game::start_table() {
    const host::Level& level = host::kLevels[level_];
    dealer_ = std::make_unique<host::Dealer>(level.stakes, seats_, random_.next());
    dealer_->set_talk(settings_.talk);
    host::TurnClock clock;
    clock.turn_ms = turn_seconds_ * 1000;
    dealer_->set_clock(clock);
    seat_order_.clear();
    my_seat_ = 0;
    my_name_ = profile_.nick.empty() ? std::string(words().you) : profile_.nick;
    dealer_->sit(my_seat_, host::Player{my_name_, profile_.avatar, false}, buy_in_);
    profile_.bankroll -= buy_in_;
    my_mark_ = buy_in_;
    // The bots have met this player before.
    profile_.seen = faded(profile_.seen, kMemoryHands);
    my_notes_ = profile_.seen;
    dealer_->remember(my_name_, my_notes_);
    seat_bot_.assign(static_cast<std::size_t>(seats_), -1);
    seat_mark_.assign(static_cast<std::size_t>(seats_), 0);

    // The bots take the other seats in a random order.
    std::vector<int> free_seats;
    for (int seat = 1; seat < seats_; ++seat) free_seats.push_back(seat);
    const Chips most = level.stakes.max_buy_in / level.stakes.big_blind;
    for (std::size_t i = 0; i < host::kBots.size(); ++i) {
        if (!picked_[i] || free_seats.empty()) continue;
        const std::size_t at = random_.below(static_cast<std::uint32_t>(free_seats.size()));
        const int seat = free_seats[at];
        free_seats.erase(free_seats.begin() + static_cast<std::ptrdiff_t>(at));
        // Bots sit down with 60 to 100 big blinds.
        const Chips blinds = most * 6 / 10 + random_.below(static_cast<std::uint32_t>(most - most * 6 / 10 + 1));
        const host::Bot& bot = host::kBots[i];
        const Chips chips = blinds * level.stakes.big_blind;
        if (!dealer_->sit(seat, host::Player{std::string(bot.name), std::string(bot.avatar), true}, chips)) continue;
        bot_pays(i, chips);
        profile_.bots[i].seen = faded(profile_.bots[i].seen, kMemoryHands);
        dealer_->remember(bot.name, profile_.bots[i].seen);
        seat_bot_[static_cast<std::size_t>(seat)] = static_cast<int>(i);
        seat_mark_[static_cast<std::size_t>(seat)] = chips;
        seat_order_.push_back(seat);
    }
    open_room();

    seen_changes_ = 0;
    my_turn_ = false;
    paused_ = false;
    look_ = ui::TableLook{};
    screen_ = Screen::Table;
}

void Game::leave_table() {
    if (guest_) {
        // Getting up at a friend's table: what is in the pot stays there.
        const Chips stack = chips_at_table();
        guest_->leave();
        close_guest_table(stack, {});
        return;
    }
    if (dealer_) {
        const bool friends_here = room_ && room_->guests() > 0;
        if (room_) {
            room_->close();
            room_.reset();
        }
        if (friends_here) {
            // With friends at the table the hand in progress is called off for
            // everybody, the same as when the host is lost to them.
            if (dealer_->table().seat(my_seat_).taken) profile_.bankroll += my_mark_;
            for (std::size_t seat = 0; seat < seat_bot_.size(); ++seat) {
                if (seat_bot_[seat] >= 0) profile_.bots[static_cast<std::size_t>(seat_bot_[seat])].bankroll += seat_mark_[seat];
            }
        } else {
            // The hand in progress is played out at once; our cards are folded if we still had a move.
            dealer_->leave(my_seat_);
            dealer_->finish_now();
            take_notices();
            // The bots get up as well: their chips go back to their bankrolls.
            for (std::size_t seat = 0; seat < seat_bot_.size(); ++seat) {
                if (seat_bot_[seat] < 0) continue;
                BotAccount& account = profile_.bots[static_cast<std::size_t>(seat_bot_[seat])];
                const Chips stack = dealer_->stack(static_cast<int>(seat));
                account.won += stack - seat_mark_[seat];
                account.bankroll += stack;
            }
        }
        note_play();
        seat_bot_.clear();
        seat_mark_.clear();
        seat_order_.clear();
        dealer_.reset();
        save_profile();
    }
    paused_ = false;
    my_turn_ = false;
    menu_row_ = 0;
    screen_ = Screen::Menu;
}

void Game::take_notices() {
    std::vector<host::Notice> notices;
    if (guest_) {
        notices = guest_->take_notices();
    } else if (dealer_) {
        notices = dealer_->take_notices();
    }
    for (const host::Notice& notice : notices) {
        if (room_ && room_->notice(notice)) continue;  // about a friend's seat: theirs to count
        if (const auto* added = std::get_if<host::notice::ChipsAdded>(&notice)) {
            const auto seat = static_cast<std::size_t>(added->seat);
            if (added->seat == my_seat_) {
                profile_.bankroll -= added->amount;
                my_mark_ += added->amount;
            } else if (seat < seat_bot_.size() && seat_bot_[seat] >= 0) {
                bot_pays(static_cast<std::size_t>(seat_bot_[seat]), added->amount);
                seat_mark_[seat] += added->amount;
            }
        } else if (const auto* left = std::get_if<host::notice::Left>(&notice)) {
            const auto seat = static_cast<std::size_t>(left->seat);
            if (guest_) {
                // The host got us up.
                close_guest_table(left->chips, std::string(words().table_got_up));
                return;
            }
            if (left->seat == my_seat_) {
                // Whatever the last hand changed is counted here: the seat is empty by now.
                profile_.stats.from_bots += left->chips - my_mark_;
                my_mark_ = left->chips;
                profile_.bankroll += left->chips;
            } else if (seat < seat_bot_.size() && seat_bot_[seat] >= 0) {
                // A bot made room for a friend.
                BotAccount& account = profile_.bots[static_cast<std::size_t>(seat_bot_[seat])];
                account.won += left->chips - seat_mark_[seat];
                account.bankroll += left->chips;
                seat_bot_[seat] = -1;
                seat_mark_[seat] = 0;
                std::erase(seat_order_, left->seat);
                save_profile();
            }
        } else if (std::holds_alternative<host::notice::HandOver>(notice)) {
            after_hand();
        }
    }
}

// A hand has ended: the statistics, the bots' accounts and the history take note of it.
void Game::after_hand() {
    const host::TableView view = table_view();
    Stats& stats = profile_.stats;

    if (seated() && static_cast<std::size_t>(my_seat_) < view.seats.size()) {
        const Chips stack = my_stack();
        const Chips result = stack - my_mark_;
        my_mark_ = stack;
        (with_people(view) ? stats.from_people : stats.from_bots) += result;
        const host::SeatView& me = view.seats[static_cast<std::size_t>(my_seat_)];
        // A hand whose end we did not see (a friend's table, a bad connection) counts, but has no record.
        if (me.in_hand && view.stage == host::Stage::Over) {
            if (me.won > 0) {
                ++stats.hands_won;
                stats.biggest_pot = std::max(stats.biggest_pot, me.won);
            }
            if (me.shown || me.note == host::Note::Muck) {
                ++stats.showdowns;
                if (me.won > 0) ++stats.showdowns_won;
            }
            PastHand hand;
            hand.number = view.hand_number;
            hand.when = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
            hand.level = level_;
            hand.cards = me.cards;
            hand.result = result;
            std::size_t start = view.log.size();
            while (start > 0 && view.log[start - 1].kind != host::LogLine::Kind::NewHand) --start;
            hand.lines.assign(view.log.begin() + static_cast<std::ptrdiff_t>(start), view.log.end());
            storage_.add(hand);
            history_.insert(history_.begin(), std::move(hand));
            if (history_.size() > kHistoryShown) history_.pop_back();
            // The list on the history screen keeps pointing at the same hand.
            if (screen_ == Screen::History && (history_open_ || history_row_ > 0)) ++history_row_;
        }
    }
    if (dealer_) {
        for (std::size_t seat = 0; seat < seat_bot_.size(); ++seat) {
            if (seat_bot_[seat] < 0) continue;
            BotAccount& account = profile_.bots[static_cast<std::size_t>(seat_bot_[seat])];
            const Chips stack = dealer_->stack(static_cast<int>(seat));
            account.won += stack - seat_mark_[seat];
            seat_mark_[seat] = stack;
        }
    }
    note_play();
    save_profile();
}

// What the table noted about everybody's play goes to the profile: to our
// statistics, and to what the bots will remember at the next table.
// What the table noted about everybody's play goes to the profile: to our
// statistics, to what the bots will remember at the next table, and to what
// they remember of friends.
void Game::note_play() {
    const bots::Notes* notes = nullptr;
    if (guest_) {
        if (guest_seated_) notes = &guest_->notes();
    } else if (dealer_) {
        notes = dealer_->notebook().find(my_name_);
    }
    if (notes != nullptr) {
        bots::Notes& play = profile_.stats.play;
        // Counts never go back; if they seem to, the host started counting anew.
        if (notes->hands >= my_notes_.hands) {
            play.hands += notes->hands - my_notes_.hands;
            play.entered += std::max(0, notes->entered - my_notes_.entered);
            play.raised += std::max(0, notes->raised - my_notes_.raised);
            play.bets += std::max(0, notes->bets - my_notes_.bets);
            play.calls += std::max(0, notes->calls - my_notes_.calls);
            play.faced += std::max(0, notes->faced - my_notes_.faced);
            play.gave_up += std::max(0, notes->gave_up - my_notes_.gave_up);
        }
        my_notes_ = *notes;
        if (!guest_) profile_.seen = *notes;
    }
    if (!dealer_) return;
    const bots::Notebook& notebook = dealer_->notebook();
    for (std::size_t seat = 0; seat < seat_bot_.size(); ++seat) {
        if (seat_bot_[seat] < 0) continue;
        const auto bot = static_cast<std::size_t>(seat_bot_[seat]);
        if (const bots::Notes* seen = notebook.find(host::kBots[bot].name)) {
            BotAccount& account = profile_.bots[bot];
            account.hands += seen->hands - account.seen.hands;
            account.seen = *seen;
        }
    }
    if (room_) {
        const host::TableView view = dealer_->view(-1);
        for (int seat = 0; seat < static_cast<int>(view.seats.size()); ++seat) {
            const std::optional<online::PeerId> friend_id = room_->guest_in(seat);
            if (!friend_id) continue;
            if (const bots::Notes* seen = notebook.find(view.seats[static_cast<std::size_t>(seat)].name)) {
                profile_.people[net::to_hex(net::Bytes(*friend_id))] = *seen;
            }
        }
    }
}

Chips Game::chips_to_buy(bool& needs_refill) const {
    needs_refill = false;
    if (!seated()) return 0;
    const Chips stack = my_stack();
    const Chips room = stakes().max_buy_in - stack;
    if (room <= 0) return 0;
    if (profile_.bankroll > 0) return std::min(room, profile_.bankroll);
    if (!can_refill()) return 0;
    needs_refill = true;
    return std::min(room, host::kStartBankroll);
}

void Game::buy_chips() {
    bool needs_refill = false;
    const Chips amount = chips_to_buy(needs_refill);
    if (amount <= 0) return;
    if (needs_refill) refill();
    if (guest_) {
        guest_->want_chips(stakes().max_buy_in, amount);
    } else {
        dealer_->want_chips(my_seat_, stakes().max_buy_in, amount);
    }
}

void Game::make_move(game::Action action) {
    if (guest_ ? !guest_->act(action) : !dealer_->act(my_seat_, action)) return;
    look_.raising = false;
    look_.typed.clear();
}

void Game::start_raise() {
    const host::TableView view = table_view();
    if (!view.choices || !view.choices->can_raise) return;
    look_.raising = true;
    look_.typed.clear();
    look_.focus = 2;
    look_.raise_to = ui::sized_raise(view, 0);
}

void Game::raise_key(const Key& key) {
    const host::TableView view = table_view();
    if (!view.choices || !view.choices->can_raise) {
        look_.raising = false;
        return;
    }
    const Chips least = view.choices->min_raise_to;
    const Chips most = view.choices->max_raise_to;
    const Chips step = view.stakes.big_blind;

    switch (key.kind) {
        case Key::Kind::Escape:
            look_.raising = false;
            look_.typed.clear();
            break;
        case Key::Kind::Left:
        case Key::Kind::Right:
            look_.typed.clear();
            look_.raise_to = std::clamp(look_.raise_to + (key.kind == Key::Kind::Left ? -step : step), least, most);
            break;
        case Key::Kind::Up:
        case Key::Kind::Down: {
            // The next ready-made size above or below the current amount.
            look_.typed.clear();
            const bool up = key.kind == Key::Kind::Up;
            Chips next = up ? most : least;
            for (int size = 0; size < 4; ++size) {
                const Chips amount = ui::sized_raise(view, size);
                if (up && amount > look_.raise_to) next = std::min(next, amount);
                if (!up && amount < look_.raise_to) next = std::max(next, amount);
            }
            look_.raise_to = next;
            break;
        }
        case Key::Kind::Backspace:
            if (!look_.typed.empty()) look_.typed.pop_back();
            break;
        case Key::Kind::Enter:
            if (!look_.typed.empty()) {
                const Chips typed = std::stoll(look_.typed);
                look_.typed.clear();
                look_.raise_to = std::clamp(typed, least, most);
                if (typed != look_.raise_to) {
                    // Not what was typed: show the corrected amount and wait for another Enter.
                    flash(ui::fill(words().raise_limits, {ui::chips_text(least), ui::chips_text(most)}));
                    break;
                }
            }
            make_move(game::Action{game::Move::Raise, look_.raise_to});
            break;
        case Key::Kind::Tab:
            look_.panel = !look_.panel;
            break;
        case Key::Kind::Letter:
            if (key.letter >= U'0' && key.letter <= U'9' && look_.typed.size() < 9) {
                if (look_.typed.empty() && key.letter == U'0') break;
                look_.typed.push_back(static_cast<char>(key.letter));
            }
            break;
    }
}

void Game::table_key(const Key& key) {
    if (paused_) {
        pause_key(key);
        return;
    }
    if (guest_ && guest_->state() == online::GuestTable::State::Joining) {
        // Still waiting for a seat: Esc gives up.
        if (key.kind == Key::Kind::Escape) {
            guest_->leave();
            close_guest_table(0, {});
        }
        return;
    }
    if (look_.raising) {
        raise_key(key);
        return;
    }
    const host::TableView view = table_view();
    const bool mine = view.choices.has_value();
    // Sitting out after missed moves: any key but Esc plays on.
    if (key.kind != Key::Kind::Escape && static_cast<std::size_t>(my_seat_) < view.seats.size()) {
        const host::SeatView& me = view.seats[static_cast<std::size_t>(my_seat_)];
        if (me.taken && me.away && !me.offline) {
            if (guest_) {
                guest_->come_back();
            } else {
                dealer_->come_back(my_seat_);
            }
            return;
        }
    }
    const bool can_fold = mine && !view.choices->can_check;
    const bool can_raise = mine && view.choices->can_raise;
    const auto check_or_call = [&] {
        make_move(game::Action{view.choices->can_check ? game::Move::Check : game::Move::Call});
    };

    switch (key.kind) {
        case Key::Kind::Escape:
            paused_ = true;
            pause_row_ = 0;
            break;
        case Key::Kind::Tab:
            look_.panel = !look_.panel;
            break;
        case Key::Kind::Left:
            if (mine && look_.focus == 2) look_.focus = 1;
            else if (mine && look_.focus == 1 && can_fold) look_.focus = 0;
            break;
        case Key::Kind::Right:
            if (mine && look_.focus == 0) look_.focus = 1;
            else if (mine && look_.focus == 1 && can_raise) look_.focus = 2;
            break;
        case Key::Kind::Enter:
            if (!mine) break;
            if (look_.focus == 0 && can_fold) make_move(game::Action{game::Move::Fold});
            else if (look_.focus == 2 && can_raise) start_raise();
            else check_or_call();
            break;
        case Key::Kind::Letter:
            switch (key_letter(key.letter)) {
                case U'f':
                    if (can_fold) make_move(game::Action{game::Move::Fold});
                    break;
                case U'c':
                    if (mine) check_or_call();
                    break;
                case U'r':
                    if (can_raise) start_raise();
                    break;
                case U'b':
                    buy_chips();
                    break;
                case U's':
                    open_profile(Screen::Table);
                    break;
                case U'h':
                    open_history(Screen::Table);
                    break;
                default: break;
            }
            break;
        default: break;
    }
}

std::vector<Game::PauseItem> Game::pause_list() const {
    std::vector<PauseItem> list{PauseItem::Resume, PauseItem::Settings};
    if (room_) list.push_back(PauseItem::Friends);
    if (can_refill()) list.push_back(PauseItem::Refill);
    list.push_back(PauseItem::Leave);
    list.push_back(PauseItem::Quit);
    return list;
}

std::vector<std::string> Game::pause_items() const {
    const ui::Words& w = words();
    std::vector<std::string> items;
    for (const PauseItem item : pause_list()) {
        switch (item) {
            case PauseItem::Resume: items.emplace_back(w.pause_resume); break;
            case PauseItem::Settings: items.emplace_back(w.menu_settings); break;
            case PauseItem::Friends: items.emplace_back(w.pause_friends); break;
            case PauseItem::Refill:
                items.push_back(ui::fill(w.pause_refill, {ui::chips_text(host::kStartBankroll)}));
                break;
            case PauseItem::Leave: items.emplace_back(w.pause_leave); break;
            case PauseItem::Quit: items.emplace_back(w.pause_quit); break;
        }
    }
    return items;
}

void Game::pause_key(const Key& key) {
    const std::vector<PauseItem> list = pause_list();
    const int count = static_cast<int>(list.size());
    pause_row_ = std::clamp(pause_row_, 0, count - 1);
    switch (key.kind) {
        case Key::Kind::Up: pause_row_ = (pause_row_ + count - 1) % count; break;
        case Key::Kind::Down: pause_row_ = (pause_row_ + 1) % count; break;
        case Key::Kind::Escape: paused_ = false; break;
        case Key::Kind::Enter:
            switch (list[static_cast<std::size_t>(pause_row_)]) {
                case PauseItem::Resume: paused_ = false; break;
                case PauseItem::Settings:
                    settings_from_ = Screen::Table;
                    settings_row_ = 0;
                    screen_ = Screen::Settings;
                    break;
                case PauseItem::Friends:
                    paused_ = false;
                    open_friends(Screen::Table);
                    break;
                case PauseItem::Refill:
                    refill();
                    pause_row_ = 0;
                    break;
                case PauseItem::Leave: leave_table(); break;
                case PauseItem::Quit:
                    leave_table();
                    quit_ = true;
                    break;
            }
            break;
        default: break;
    }
}

void Game::draw_pause(Canvas& canvas) {
    const std::vector<std::string> items = pause_items();
    pause_row_ = std::clamp(pause_row_, 0, static_cast<int>(items.size()) - 1);
    const int width = 44;
    const int height = static_cast<int>(items.size()) * 2 + 4;
    const int x = (canvas.width() - width) / 2;
    const int y = (canvas.height() - height) / 2;
    canvas.fill(x, y, width, height, color::kPanel);
    canvas.outline(x, y, width, height, color::kRule);
    canvas.put(x + 3, y + 1, words().pause_title, color::kWhite, color::kPanel, true);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const bool picked = static_cast<int>(i) == pause_row_;
        const int row = y + 3 + static_cast<int>(i) * 2;
        const ui::Rgb back = picked ? color::kSeatActive : color::kPanel;
        canvas.fill(x + 1, row, width - 2, 1, back);
        if (picked) canvas.put(x + 1, row, "▌", color::kYellow, back);
        canvas.put(x + 4, row, items[i], picked ? color::kYellow : color::kText, back, picked);
    }
}

// The second line of the full hint: how many cards improve the hand and how
// often it wins if all the cards are simply dealt out. Counting takes a
// moment, so it is done once per change of the cards, not for every picture.
void Game::work_out_hint(const host::TableView& view) {
    const host::SeatView& me = view.seats[static_cast<std::size_t>(my_seat_)];
    int opponents = 0;
    for (std::size_t seat = 0; seat < view.seats.size(); ++seat) {
        const host::SeatView& other = view.seats[seat];
        if (static_cast<int>(seat) != my_seat_ && other.taken && other.in_hand && !other.folded) ++opponents;
    }
    // Once the hand is over there is nothing left to guess.
    if (view.stage != host::Stage::Playing || !me.in_hand || me.folded || !me.cards || opponents == 0) {
        hint_more_.clear();
        hint_for_.clear();
        return;
    }
    std::string key = ui::cards_text({(*me.cards)[0], (*me.cards)[1]}) + "|" + ui::cards_text(view.board) + "|" +
                      std::to_string(opponents) + (settings_.lang == ui::Lang::Ru ? "r" : "e");
    if (key == hint_for_) return;
    hint_for_ = std::move(key);

    const ui::Words& w = words();
    const auto percent = [](double part) { return std::to_string(static_cast<int>(part * 100 + 0.5)); };
    hint_more_.clear();
    const game::Outs outs = game::count_outs(*me.cards, view.board);
    if (outs.count > 0) {
        hint_more_ = ui::fill(w.hint_outs, {std::to_string(outs.count),
                                            percent(outs.by_the_river(5 - static_cast<int>(view.board.size())))});
        hint_more_ += " · ";
    }
    // Against hands nobody knows anything about: the same seed gives the same number for the same cards.
    game::Random random(std::hash<std::string>{}(hint_for_));
    hint_more_ += ui::fill(w.hint_win, {percent(game::estimate_equity(*me.cards, view.board, opponents, 3000, random))});
}

void Game::draw_table(Canvas& canvas) {
    if (guest_ && guest_->state() != online::GuestTable::State::Seated) {
        draw_joining(canvas);
        return;
    }
    const host::TableView view = table_view();
    look_.lang = settings_.lang;
    look_.hint = settings_.hint;
    look_.hint_more.clear();
    if (settings_.hint == ui::Hint::Full) {
        work_out_hint(view);
        look_.hint_more = hint_more_;
    }
    look_.title = std::string(words().level_names[level_]);
    if (guest_) {
        look_.title = ui::fill(words().table_of, {guest_->host_name()}) + "  ·  " + look_.title;
        if (guest_->deck_alarms() > 0) look_.title = std::string(words().deck_doubt) + "  ·  " + look_.title;
    }
    look_.bankroll = profile_.bankroll;
    look_.flash = flash_;
    // Out of touch with the host: say so, and how long we wait.
    if (guest_ && !guest_->host_online()) {
        const std::int64_t left = std::max<std::int64_t>(0, host::TurnClock{}.offline_seat_ms - (now_ - guest_->lost_since()));
        look_.flash = ui::fill(words().host_lost, {std::to_string(left / 60000) + ":" +
                                                   (left / 1000 % 60 < 10 ? "0" : "") + std::to_string(left / 1000 % 60)});
    }
    look_.buy_amount = chips_to_buy(look_.refill);

    // The big picture in the side panel: whoever is thinking; on our own
    // turn, whoever made the bet we are answering.
    const auto other = [&](int seat) {
        return seat >= 0 && seat != my_seat_ && view.seats[static_cast<std::size_t>(seat)].taken;
    };
    if (other(view.acting)) {
        look_.portrait_seat = view.acting;
    } else if (other(view.aggressor)) {
        look_.portrait_seat = view.aggressor;
    } else if (other(view.last_actor)) {
        look_.portrait_seat = view.last_actor;
    } else if (!other(look_.portrait_seat)) {
        look_.portrait_seat = -1;
        for (int seat = 0; seat < static_cast<int>(view.seats.size()); ++seat) {
            if (other(seat)) {
                look_.portrait_seat = seat;
                break;
            }
        }
    }

    ui::draw_table(canvas, view, look_, avatars_);
    if (paused_) draw_pause(canvas);
}

// ---------------------------------------------------------------- the profile
namespace {

std::string signed_text(Chips chips) {
    return (chips > 0 ? "+" : chips < 0 ? "-" : "") + ui::chips_text(chips < 0 ? -chips : chips);
}

ui::Rgb sign_colour(Chips chips) {
    return chips > 0 ? color::kGreen : chips < 0 ? color::kRed : color::kText;
}

std::string percent(long long part, long long whole) {
    if (whole <= 0) return "—";
    return std::to_string((part * 100 + whole / 2) / whole) + "%";
}

// "16.11 08:30" in the time of the player's computer.
std::string when_text(std::int64_t seconds) {
    const std::time_t time = static_cast<std::time_t>(seconds);
    const std::tm* local = std::localtime(&time);
    if (local == nullptr) return {};
    char text[32];
    std::snprintf(text, sizeof text, "%02d.%02d %02d:%02d", local->tm_mday, local->tm_mon + 1, local->tm_hour,
                  local->tm_min);
    return text;
}

// Cards in the colours of their suits. Returns the width used.
int put_cards(Canvas& canvas, int x, int y, std::span<const game::Card> cards, ui::Rgb back) {
    int at = x;
    for (const game::Card card : cards) {
        at += canvas.put(at, y, ui::cards_text({card}), ui::suit_colour_on_dark(card.suit), back, true) + 1;
    }
    return at - x;
}

}  // namespace

void Game::open_profile(Screen from) {
    back_to_ = from;
    profile_row_ = 0;
    typing_nick_ = false;
    screen_ = Screen::Profile;
}

// The pictures of the set that no bot wears.
std::vector<std::string> Game::free_avatars() const {
    std::vector<std::string> free;
    for (const std::string& name : avatars_.names()) {
        bool taken = false;
        for (const host::Bot& bot : host::kBots) taken = taken || bot.avatar == name;
        if (!taken) free.push_back(name);
    }
    return free;
}

// Nick, avatar and, when it is offered, the refill.
int Game::profile_rows() const {
    return can_refill() ? 3 : 2;
}

void Game::profile_key(const Key& key) {
    if (typing_nick_) {
        switch (key.kind) {
            case Key::Kind::Enter: {
                std::string nick = nick_draft_;
                while (!nick.empty() && nick.back() == ' ') nick.pop_back();
                while (!nick.empty() && nick.front() == ' ') nick.erase(0, 1);
                // Two players with one name at a table would be one player to the bots.
                for (const host::Bot& bot : host::kBots) {
                    if (bot.name == nick) {
                        flash(std::string(words().nick_taken));
                        return;
                    }
                }
                profile_.nick = nick;
                typing_nick_ = false;
                save_profile();
                // Friends see the new name in their lists.
                if (online_) online_->set_name(nick.empty() ? std::string("POHER") : nick);
                break;
            }
            case Key::Kind::Escape: typing_nick_ = false; break;
            case Key::Kind::Backspace: {
                std::u32string glyphs = ui::decode_utf8(nick_draft_);
                if (!glyphs.empty()) glyphs.pop_back();
                nick_draft_.clear();
                for (const char32_t glyph : glyphs) ui::append_utf8(nick_draft_, glyph);
                break;
            }
            case Key::Kind::Letter:
                if (key.letter >= 0x20 && key.letter != 0x7f && ui::glyph_width(key.letter) == 1 &&
                    ui::decode_utf8(nick_draft_).size() < kNickLength) {
                    ui::append_utf8(nick_draft_, key.letter);
                }
                break;
            default: break;
        }
        return;
    }
    const int rows = profile_rows();
    profile_row_ = std::clamp(profile_row_, 0, rows - 1);
    switch (key.kind) {
        case Key::Kind::Escape: screen_ = back_to_; break;
        case Key::Kind::Up: profile_row_ = (profile_row_ + rows - 1) % rows; break;
        case Key::Kind::Down: profile_row_ = (profile_row_ + 1) % rows; break;
        case Key::Kind::Enter:
            if (profile_row_ == 0) {
                typing_nick_ = true;
                nick_draft_ = profile_.nick;
            } else if (profile_row_ == 2) {
                refill();
                profile_row_ = 0;
            }
            break;
        case Key::Kind::Left:
        case Key::Kind::Right: {
            if (profile_row_ != 1) break;
            const std::vector<std::string> free = free_avatars();
            if (free.empty()) break;
            const auto found = std::find(free.begin(), free.end(), profile_.avatar);
            const std::size_t at = found == free.end() ? 0 : static_cast<std::size_t>(found - free.begin());
            const std::size_t step = key.kind == Key::Kind::Right ? 1 : free.size() - 1;
            profile_.avatar = free[found == free.end() ? 0 : (at + step) % free.size()];
            save_profile();
            break;
        }
        default: break;
    }
}

void Game::draw_profile(Canvas& canvas) {
    const ui::Words& w = words();
    draw_bars(canvas, w, profile_.bankroll, typing_nick_ ? w.profile_nick_keys : w.profile_keys);
    profile_row_ = std::clamp(profile_row_, 0, profile_rows() - 1);
    const int x = (canvas.width() - kColumn) / 2;
    int y = std::max(2, (canvas.height() - 25) / 2);
    canvas.put(x + 2, y, w.profile_title, color::kWhite, kKeep, true);
    y += 2;

    // The picture on the left, what can be changed next to it.
    ui::draw_avatar(canvas, x + 2, y, avatars_.art(profile_.avatar, 16, false, color::kHero));
    const int left = x + 22;
    const int width = kColumn - 22;
    const int value_x = left + 14;
    const auto label = [&](int row, int at, std::string_view text) {
        const bool picked = row == profile_row_;
        if (row >= 0) draw_row(canvas, left, at, width, picked);
        canvas.put(left + 2, at, text, picked ? color::kYellow : row >= 0 ? color::kText : color::kDim,
                   picked ? color::kSeatActive : color::kBackground, picked);
        return picked;
    };
    bool picked = label(0, y, w.profile_nick);
    const ui::Rgb back = picked ? color::kSeatActive : color::kBackground;
    if (typing_nick_) {
        canvas.put(value_x, y, nick_draft_ + "▏", color::kWhite, back, true);
    } else if (profile_.nick.empty()) {
        canvas.put(value_x, y, w.profile_no_nick, color::kDim, back);
    } else {
        canvas.put(value_x, y, profile_.nick, color::kWhite, back, true);
    }
    picked = label(1, y + 2, w.profile_avatar);
    draw_choice(canvas, value_x, y + 2, profile_.avatar, picked, true, true);
    label(-1, y + 4, w.profile_bankroll);
    canvas.put(value_x, y + 4, ui::chips_text(profile_.bankroll + chips_at_table()), color::kText);
    label(-1, y + 5, w.profile_refills);
    canvas.put(value_x, y + 5, std::to_string(profile_.refills), color::kText);
    if (profile_rows() == 3) {
        label(2, y + 7, ui::fill(w.menu_refill, {ui::chips_text(host::kStartBankroll)}));
    }
    y += 9;

    const Stats& stats = profile_.stats;
    const bots::Notes& play = stats.play;
    const auto line = [&](std::string_view name, const std::string& value, ui::Rgb colour = color::kText) {
        canvas.put(x + 4, y, name, color::kDim);
        canvas.put(x + 40, y, value, colour);
        ++y;
    };
    canvas.put(x + 2, y, w.profile_public, color::kWhite, kKeep, true);
    ++y;
    line(w.stat_hands, ui::chips_text(play.hands));
    line(w.stat_hands_won, play.hands > 0 ? ui::chips_text(stats.hands_won) + "  (" + percent(stats.hands_won, play.hands) + ")"
                                          : std::string("—"));
    line(w.stat_biggest_pot, ui::chips_text(stats.biggest_pot));
    line(w.stat_vs_bots, signed_text(stats.from_bots), sign_colour(stats.from_bots));
    line(w.stat_vs_people, signed_text(stats.from_people), sign_colour(stats.from_people));
    ++y;
    canvas.put(x + 2, y, w.profile_private, color::kWhite, kKeep, true);
    ++y;
    line(w.stat_enters, percent(play.entered, play.hands));
    line(w.stat_raises, percent(play.raised, play.hands));
    if (play.calls > 0) {
        char text[16];
        std::snprintf(text, sizeof text, "%.1f", static_cast<double>(play.bets) / play.calls);
        std::string value = text;
        // Russian writes fractions with a comma.
        if (settings_.lang == ui::Lang::Ru) std::replace(value.begin(), value.end(), '.', ',');
        line(w.stat_pushes, value);
    } else {
        line(w.stat_pushes, "—");
    }
    line(w.stat_showdowns_won, percent(stats.showdowns_won, stats.showdowns));
    if (!flash_.empty()) canvas.put(x + 2, y + 1, flash_, color::kYellow);
}

// ---------------------------------------------------------------- the history of hands
void Game::open_history(Screen from) {
    back_to_ = from;
    history_row_ = 0;
    history_open_ = false;
    history_scroll_ = 0;
    screen_ = Screen::History;
}

void Game::history_key(const Key& key) {
    const int count = static_cast<int>(history_.size());
    history_row_ = std::clamp(history_row_, 0, std::max(count - 1, 0));
    if (history_open_) {
        switch (key.kind) {
            case Key::Kind::Escape:
            case Key::Kind::Backspace: history_open_ = false; break;
            case Key::Kind::Up: history_scroll_ = std::max(0, history_scroll_ - 1); break;
            case Key::Kind::Down: ++history_scroll_; break;  // held back when drawn
            case Key::Kind::Left:  // the hand before this one
                if (history_row_ + 1 < count) ++history_row_;
                history_scroll_ = 0;
                break;
            case Key::Kind::Right:
                if (history_row_ > 0) --history_row_;
                history_scroll_ = 0;
                break;
            default: break;
        }
        return;
    }
    switch (key.kind) {
        case Key::Kind::Escape: screen_ = back_to_; break;
        case Key::Kind::Up: history_row_ = std::max(history_row_ - 1, 0); break;
        case Key::Kind::Down: history_row_ = std::min(history_row_ + 1, std::max(count - 1, 0)); break;
        case Key::Kind::Enter:
            if (count > 0) {
                history_open_ = true;
                history_scroll_ = 0;
            }
            break;
        default: break;
    }
}

void Game::draw_history(Canvas& canvas) {
    const ui::Words& w = words();
    const int count = static_cast<int>(history_.size());
    history_row_ = std::clamp(history_row_, 0, std::max(count - 1, 0));
    const bool open = history_open_ && count > 0;
    draw_bars(canvas, w, profile_.bankroll, open ? w.history_hand_keys : w.history_keys);
    const int x = (canvas.width() - kColumn) / 2;
    int y = 2;
    canvas.put(x + 2, y, w.history_title, color::kWhite, kKeep, true);
    y += 2;
    if (count == 0) {
        canvas.put(x + 2, y, w.history_empty, color::kDim);
        return;
    }
    const int room = canvas.height() - 2 - y;

    const auto summary = [&](const PastHand& hand, int at, ui::Rgb back) {
        const std::string number = ui::fill(w.history_hand, {std::to_string(hand.number)});
        canvas.put(x + 2, at, number, color::kText, back);
        canvas.put(x + 17, at, when_text(hand.when), color::kDim, back);
        canvas.put(x + 30, at, w.level_names[hand.level], color::kDim, back);
        if (hand.cards) put_cards(canvas, x + 46, at, *hand.cards, back);
        canvas.put_right(x + kColumn - 2, at, signed_text(hand.result), sign_colour(hand.result), back);
    };

    if (!open) {
        // The list, the newest hand on top; the picked one stays in view.
        const int first = std::clamp(history_row_ - room / 2, 0, std::max(count - room, 0));
        for (int i = 0; i < room && first + i < count; ++i) {
            const bool picked = first + i == history_row_;
            draw_row(canvas, x, y + i, kColumn, picked);
            summary(history_[static_cast<std::size_t>(first + i)], y + i, picked ? color::kSeatActive : color::kBackground);
        }
        return;
    }

    const PastHand& hand = history_[static_cast<std::size_t>(history_row_)];
    canvas.fill(x, y, kColumn, 1, color::kSeatActive);
    summary(hand, y, color::kSeatActive);
    y += 2;
    const int lines = static_cast<int>(hand.lines.size());
    const int space = canvas.height() - 2 - y;
    history_scroll_ = std::clamp(history_scroll_, 0, std::max(lines - space, 0));
    for (int i = 0; i < space && history_scroll_ + i < lines; ++i) {
        const host::LogLine& line = hand.lines[static_cast<std::size_t>(history_scroll_ + i)];
        if (line.kind == host::LogLine::Kind::Board) {
            put_cards(canvas, x + 4, y + i, line.cards, color::kBackground);
            continue;
        }
        const int used = canvas.put(x + 4, y + i, ui::fit_text(line.name, 16),
                                    line.bot ? color::kBotTag : color::kHero, kKeep, true);
        canvas.put(x + 5 + used, y + i, ui::fit_text(ui::log_text(line, settings_.lang), kColumn - 7 - used),
                   line.kind == host::LogLine::Kind::Says ? color::kYellow : color::kText);
    }
}

}  // namespace poher::app
