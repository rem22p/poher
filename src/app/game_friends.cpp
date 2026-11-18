// The parts of the game that are about friends: the friends screen,
// invitations, our table as friends see it, and sitting at a friend's table.
#include <algorithm>

#include "app/game.hpp"
#include "app/menu_parts.hpp"

namespace poher::app {

using game::Chips;
using ui::Canvas;
using ui::kKeep;
namespace color = ui::color;
using parts::draw_bars;
using parts::draw_row;
using parts::kColumn;

namespace {

constexpr int kFriendsWidth = 96;  // the friends screen is wider than a menu: the code is long

std::string short_id(const online::PeerId& id) {
    return net::short_id(id);
}

bool same(const online::PeerId& a, const online::PeerId& b) {
    return a == b;
}

}  // namespace

// ---------------------------------------------------------------- the network every moment
void Game::watch_network() {
    if (!online_) return;
    online_->poll();
    if (online_->take_changed()) storage_.save_bytes("network.dat", online_->save());
    update_status();

    // A new invitation rings once.
    if (const std::optional<online::Invitation> invitation = online_->invitation()) {
        const std::string key = net::to_hex(net::Bytes(invitation->from));
        if (key != invitation_seen_) {
            invitation_seen_ = key;
            bell_ = settings_.sound;
            dirty_ = true;
        }
    } else {
        invitation_seen_.clear();
    }

    if (!guest_) return;
    using State = online::GuestTable::State;
    if (guest_->state() == State::Seated && !guest_seated_) {
        // What the host's table knew about our play when we sat down is where our counting starts.
        guest_seated_ = true;
        my_notes_ = guest_->notes();
        saved_stack_ = -1;
    }
    // A deck that did not check out is said at once.
    if (guest_->deck_alarms() > deck_alarms_) {
        deck_alarms_ = guest_->deck_alarms();
        flash(std::string(words().deck_alarm));
        bell_ = settings_.sound;
    }
    switch (guest_->state()) {
        case State::Refused: close_guest_table(0, refusal_text(guest_->refusal())); return;
        // The table is gone with the hand in it: the hand is called off, we take what we had before it.
        case State::Closed: close_guest_table(guest_seated_ ? my_mark_ : 0, std::string(words().table_closed)); return;
        default: break;
    }
    if (!guest_->host_online() && now_ - guest_->lost_since() >= host::TurnClock{}.offline_seat_ms) {
        close_guest_table(guest_seated_ ? my_mark_ : 0, std::string(words().host_gone));
    }
}

void Game::update_status() {
    online::Status status;
    status.version = kVersion;
    status.avatar = profile_.avatar;
    if (guest_) {
        status.activity = online::Activity::Guest;
    } else if (room_) {
        status.activity = room_->table().open ? online::Activity::Hosting : online::Activity::WithBots;
        status.table = room_->table();
    } else if (dealer_) {
        status.activity = online::Activity::WithBots;
    }
    const Stats& stats = profile_.stats;
    status.stats.hands = stats.play.hands;
    status.stats.hands_won = stats.hands_won;
    status.stats.biggest_pot = stats.biggest_pot;
    status.stats.bankroll = profile_.bankroll + chips_at_table();
    status.stats.refills = profile_.refills;
    status.stats.from_bots = stats.from_bots;
    status.stats.from_people = stats.from_people;
    online_->set_status(status);
}

// Messages about tables: to our room, or from the host of the table we sit at.
void Game::table_message(const online::PeerId& from, online::Kind kind, net::Reader& body) {
    if (guest_ && same(from, guest_->host())) {
        guest_->message(kind, body);
        return;
    }
    if (room_) {
        room_->message(from, kind, body);
        return;
    }
    // Asked for a seat with no table here.
    if (kind == online::Kind::Join && online_) {
        net::Writer answer;
        answer.u8(static_cast<std::uint8_t>(online::Refusal::NoTable));
        online_->send(from, online::Kind::Refused, answer);
    }
}

void Game::friend_link(const online::PeerId& who, bool online) {
    if (room_) room_->link(who, online);
    if (guest_ && same(who, guest_->host())) guest_->link(online);
    dirty_ = true;
}

// ---------------------------------------------------------------- our table, as friends see it
void Game::open_room() {
    if (!online_ || !dealer_) return;
    online::TableInfo table;
    table.level = static_cast<std::uint8_t>(level_);
    table.seats = static_cast<std::uint8_t>(seats_);
    table.open = table_open_;
    table.turn_seconds = static_cast<std::uint8_t>(turn_seconds_);
    room_ = std::make_unique<online::HostRoom>(*dealer_, *online_, table);
    room_->make_room = [this] { return make_room(); };
    room_->on_seated = [this](const online::PeerId& who, const std::string& name) {
        // Our bots have met this friend before.
        const auto known = profile_.people.find(net::to_hex(net::Bytes(who)));
        if (known != profile_.people.end()) dealer_->remember(name, known->second);
        flash(ui::fill(words().friend_sat_down, {name}));
    };
    room_->tick();
}

// A friend wants a seat at a full table: the bot that sat down last gives way
// once the hand is over.
int Game::make_room() {
    for (auto it = seat_order_.rbegin(); it != seat_order_.rend(); ++it) {
        const int seat = *it;
        if (seat_bot_[static_cast<std::size_t>(seat)] < 0 || !dealer_->table().seat(seat).taken) continue;
        if (dealer_->table().seat(seat).leaving) continue;
        dealer_->leave(seat);
        take_notices();
        return seat;
    }
    return -1;
}

// ---------------------------------------------------------------- a friend's table
void Game::join_table(const online::PeerId& host, const std::string& name, const online::TableInfo& table) {
    if (!online_) return;
    const std::size_t level = std::min<std::size_t>(table.level, host::kLevels.size() - 1);
    const game::Stakes& stakes = host::kLevels[level].stakes;
    if (!level_open(level)) {
        flash(ui::fill(words().need_bankroll,
                       {ui::chips_text(std::max(host::kLevels[level].bankroll, stakes.min_buy_in))}));
        return;
    }
    if (at_table()) leave_table();
    level_ = level;
    const Chips buy_in = std::min(stakes.max_buy_in, profile_.bankroll);
    guest_ = std::make_unique<online::GuestTable>(*online_, host, name);
    my_name_ = profile_.nick.empty() ? std::string(words().you) : profile_.nick;
    guest_->join(online::Join{online::kProtocol, my_name_, profile_.avatar, buy_in});
    my_seat_ = 0;
    my_mark_ = 0;
    my_notes_ = bots::Notes{};
    guest_seated_ = false;
    saved_stack_ = -1;
    deck_alarms_ = 0;
    seen_changes_ = 0;
    my_turn_ = false;
    paused_ = false;
    look_ = ui::TableLook{};
    screen_ = Screen::Table;
}

// Back to the menu from a friend's table with `take_home` chips.
void Game::close_guest_table(Chips take_home, std::string why) {
    if (!guest_) return;
    if (guest_seated_) {
        // The hand that was cut short: whatever it took or gave.
        profile_.stats.from_people += take_home - my_mark_;
        profile_.bankroll += take_home;
    }
    guest_.reset();
    guest_seated_ = false;
    my_mark_ = 0;
    paused_ = false;
    my_turn_ = false;
    menu_row_ = 0;
    screen_ = Screen::Menu;
    save_profile();
    if (!why.empty()) flash(std::move(why));
}

std::string Game::refusal_text(online::Refusal why) const {
    const ui::Words& w = words();
    switch (why) {
        case online::Refusal::Full: return std::string(w.refused_full);
        case online::Refusal::Closed: return std::string(w.refused_closed);
        case online::Refusal::Version: return std::string(w.refused_version);
        case online::Refusal::NoTable: return std::string(w.refused_no_table);
        case online::Refusal::Bankroll: return std::string(w.refused_bankroll);
    }
    return {};
}

void Game::draw_joining(Canvas& canvas) {
    const ui::Words& w = words();
    draw_bars(canvas, w, profile_.bankroll, w.joining_keys);
    const std::string text = ui::fill(w.joining, {guest_ ? guest_->host_name() : std::string()});
    canvas.put_centred(canvas.width() / 2, canvas.height() / 2, text, color::kText);
}

// ---------------------------------------------------------------- the friends screen
void Game::open_friends(Screen from) {
    back_to_ = from;
    friends_row_ = 0;
    typing_code_ = false;
    screen_ = Screen::Friends;
}

// "Add a friend", the requests, then the friends.
int Game::friend_rows() const {
    if (!online_) return 1;
    return 1 + static_cast<int>(online_->requests().size()) + static_cast<int>(online_->mates().size());
}

std::string Game::mate_text(const online::Mate& mate) const {
    const ui::Words& w = words();
    if (mate.link == net::Link::None) return std::string(w.friend_offline);
    if (!mate.status) return std::string(w.friend_online);
    const online::Status& status = *mate.status;
    if (status.protocol != online::kProtocol) return ui::fill(w.friend_version, {status.version});
    switch (status.activity) {
        case online::Activity::Menu: return std::string(w.friend_menu);
        case online::Activity::WithBots: return std::string(w.friend_bots);
        case online::Activity::Guest: return std::string(w.friend_guest);
        case online::Activity::Hosting: {
            const online::TableInfo& table = status.table;
            const std::size_t level = std::min<std::size_t>(table.level, host::kLevels.size() - 1);
            std::string text = ui::fill(w.friend_hosting, {std::string(w.level_names[level]),
                                                            std::to_string(table.people + table.bots),
                                                            std::to_string(table.seats)});
            if (table.open) text += " · " + std::string(w.friend_open);
            return text;
        }
    }
    return {};
}

void Game::friends_key(const Key& key) {
    const ui::Words& w = words();
    if (!online_) {
        if (key.kind == Key::Kind::Escape) screen_ = back_to_;
        return;
    }
    if (typing_code_) {
        switch (key.kind) {
            case Key::Kind::Escape: typing_code_ = false; break;
            case Key::Kind::Backspace:
                if (!code_draft_.empty()) code_draft_.pop_back();
                break;
            case Key::Kind::Enter: {
                const net::AddResult result = online_->add(code_draft_);
                switch (result) {
                    case net::AddResult::Ok: flash(std::string(w.add_sent)); break;
                    case net::AddResult::BadCode: flash(std::string(w.add_bad)); return;
                    case net::AddResult::OwnCode: flash(std::string(w.add_own)); return;
                    case net::AddResult::AlreadyAdded: flash(std::string(w.add_already)); break;
                    case net::AddResult::Failed: flash(std::string(w.add_failed)); return;
                }
                typing_code_ = false;
                code_draft_.clear();
                break;
            }
            case Key::Kind::Letter:
                // A code is hex digits; whatever else comes with a paste is left out.
                if (((key.letter >= U'0' && key.letter <= U'9') || (key.letter >= U'a' && key.letter <= U'f') ||
                     (key.letter >= U'A' && key.letter <= U'F')) &&
                    code_draft_.size() < 76) {
                    code_draft_.push_back(static_cast<char>(key.letter));
                }
                break;
            default: break;
        }
        return;
    }

    const std::vector<online::Request> requests = online_->requests();
    const std::vector<online::Mate> mates = online_->mates();
    const int rows = friend_rows();
    friends_row_ = std::clamp(friends_row_, 0, rows - 1);
    const int request = friends_row_ - 1;
    const int mate = friends_row_ - 1 - static_cast<int>(requests.size());

    switch (key.kind) {
        case Key::Kind::Escape: screen_ = back_to_; break;
        case Key::Kind::Up: friends_row_ = (friends_row_ + rows - 1) % rows; break;
        case Key::Kind::Down: friends_row_ = (friends_row_ + 1) % rows; break;
        case Key::Kind::Backspace:
            if (request >= 0 && request < static_cast<int>(requests.size())) {
                online_->decline(requests[static_cast<std::size_t>(request)].id);
            }
            break;
        case Key::Kind::Letter:
            if (key_letter(key.letter) == U'c') {
                clipboard_ = online_->code();
                flash(std::string(w.code_copied));
            }
            break;
        case Key::Kind::Enter:
            if (friends_row_ == 0) {
                typing_code_ = true;
                code_draft_.clear();
            } else if (request >= 0 && request < static_cast<int>(requests.size())) {
                online_->accept(requests[static_cast<std::size_t>(request)].id);
            } else if (mate >= 0 && mate < static_cast<int>(mates.size())) {
                const online::Mate& chosen = mates[static_cast<std::size_t>(mate)];
                if (room_) {
                    // At our own table: call them to it.
                    room_->invited(chosen.id);
                    online_->invite(chosen.id, room_->table());
                    flash(ui::fill(w.invite_sent, {chosen.name}));
                } else if (chosen.link != net::Link::None && chosen.status &&
                           chosen.status->protocol == online::kProtocol &&
                           chosen.status->activity == online::Activity::Hosting && chosen.status->table.open) {
                    join_table(chosen.id, chosen.name, chosen.status->table);
                } else {
                    flash(std::string(w.cannot_join));
                }
            }
            break;
        default: break;
    }
}

void Game::draw_friends(Canvas& canvas) {
    const ui::Words& w = words();
    draw_bars(canvas, w, profile_.bankroll, typing_code_ ? w.code_keys : w.friends_keys);
    const int width = std::min(kFriendsWidth, canvas.width() - 4);
    const int x = (canvas.width() - width) / 2;
    int y = 2;
    canvas.put(x + 2, y, w.friends_title, color::kWhite, kKeep, true);
    if (!online_) return;
    const net::Link link = online_->link();
    std::string state = link == net::Link::None ? std::string(w.net_connecting) : std::string(w.net_online);
    if (link != net::Link::None) state += " · " + std::string(link == net::Link::Direct ? w.link_direct : w.link_relay);
    canvas.put_right(x + width - 2, y, state, link == net::Link::None ? color::kDim : color::kGreen);
    y += 2;

    // Our code: long, so it stands on a line of its own.
    canvas.put(x + 2, y, w.my_code, color::kText);
    canvas.put(x + 2, y + 1, ui::fit_text(online_->code(), width - 4), color::kYellow, kKeep, true);
    canvas.put(x + 2, y + 2, ui::fit_text(std::string(w.my_code_note), width - 4), color::kDim);
    y += 4;

    const std::vector<online::Request> requests = online_->requests();
    const std::vector<online::Mate> mates = online_->mates();
    const int rows = friend_rows();
    friends_row_ = std::clamp(friends_row_, 0, rows - 1);

    // Row 0: adding a friend, with the code being typed.
    bool picked = friends_row_ == 0;
    draw_row(canvas, x, y, width, picked);
    const ui::Rgb back = picked ? color::kSeatActive : color::kBackground;
    canvas.put(x + 2, y, w.add_friend, picked ? color::kYellow : color::kText, back, picked);
    if (typing_code_) {
        const std::string shown = code_draft_.empty() ? std::string(w.code_prompt) : code_draft_ + "▏";
        const int at = x + 4 + ui::text_width(w.add_friend);
        canvas.put(at, y, ui::fit_text(shown, x + width - at - 1), code_draft_.empty() ? color::kDim : color::kWhite,
                   back, true);
    }
    y += 2;

    int row = 1;
    for (const online::Request& request : requests) {
        picked = friends_row_ == row;
        draw_row(canvas, x, y, width, picked);
        const ui::Rgb line_back = picked ? color::kSeatActive : color::kBackground;
        canvas.put(x + 2, y, ui::fill(w.request_from, {short_id(request.id)}), color::kYellow, line_back, picked);
        canvas.put_right(x + width - 2, y, w.request_keys, color::kDim, line_back);
        ++y;
        ++row;
    }
    if (mates.empty()) canvas.put(x + 2, y + 1, w.friends_none, color::kDim);
    // As many as fit; the picked one stays in view.
    const int room = canvas.height() - 2 - y;
    const int first_mate = std::clamp(friends_row_ - row - room + 1, 0, std::max(0, static_cast<int>(mates.size()) - room));
    for (int i = first_mate; i < static_cast<int>(mates.size()) && y < canvas.height() - 2; ++i) {
        const online::Mate& mate = mates[static_cast<std::size_t>(i)];
        picked = friends_row_ == row + i;
        draw_row(canvas, x, y, width, picked);
        const ui::Rgb line_back = picked ? color::kSeatActive : color::kBackground;
        const bool on = mate.link != net::Link::None;
        canvas.put(x + 2, y, on ? "●" : "○", on ? color::kGreen : color::kDim, line_back);
        const std::string name = mate.name.empty() ? short_id(mate.id) : mate.name;
        canvas.put(x + 4, y, ui::fit_text(name, 18), on ? color::kWhite : color::kDim, line_back, picked);
        canvas.put(x + 24, y, ui::fit_text(mate_text(mate), width - 26), on ? color::kText : color::kDim, line_back);
        ++y;
    }
    if (!flash_.empty()) canvas.put(x + 2, canvas.height() - 2, ui::fit_text(flash_, width - 4), color::kYellow);
}

// ---------------------------------------------------------------- invitations
bool Game::invitation_shown() const {
    if (!online_) return false;
    const std::optional<online::Invitation> invitation = online_->invitation();
    if (!invitation) return false;
    // Already at that very table.
    return !(guest_ && same(guest_->host(), invitation->from));
}

void Game::invitation_key(const Key& key) {
    const std::optional<online::Invitation> invitation = online_->invitation();
    if (!invitation) return;
    if (key.kind == Key::Kind::Escape) {
        online_->drop_invitation();
    } else if (key.kind == Key::Kind::Enter) {
        online_->drop_invitation();
        join_table(invitation->from, invitation->name, invitation->table);
    }
}

void Game::draw_invitation(Canvas& canvas) {
    const std::optional<online::Invitation> invitation = online_->invitation();
    if (!invitation) return;
    const ui::Words& w = words();
    const online::TableInfo& table = invitation->table;
    const std::size_t level = std::min<std::size_t>(table.level, host::kLevels.size() - 1);
    const game::Stakes& stakes = host::kLevels[level].stakes;
    const std::string where = std::string(w.level_names[level]) + " " + ui::chips_text(stakes.small_blind) + "/" +
                              ui::chips_text(stakes.big_blind);
    const std::string seats = ui::fill(w.invitation_table, {where, std::to_string(table.people + table.bots),
                                                             std::to_string(table.seats)});
    const Chips buy_in = std::min(stakes.max_buy_in, profile_.bankroll);
    const std::string keys = ui::fill(w.invitation_keys, {ui::chips_text(buy_in)});

    const int width = std::max({ui::text_width(seats), ui::text_width(keys), 40}) + 8;
    const int height = at_table() ? 9 : 8;
    const int x = (canvas.width() - width) / 2;
    const int y = (canvas.height() - height) / 2;
    canvas.fill(x, y, width, height, color::kPanel);
    canvas.outline(x, y, width, height, color::kYellow);
    canvas.put(x + 4, y + 2, ui::fill(w.invitation_title, {invitation->name}), color::kYellow, color::kPanel, true);
    canvas.put(x + 4, y + 3, seats, color::kText, color::kPanel);
    int line = y + 5;
    if (at_table()) canvas.put(x + 4, line++, w.invitation_leave, color::kDim, color::kPanel);
    canvas.put(x + 4, line, keys, color::kWhite, color::kPanel);
}

}  // namespace poher::app
