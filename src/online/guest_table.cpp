#include "online/guest_table.hpp"

#include <algorithm>
#include <utility>

namespace poher::online {

namespace {

constexpr std::size_t kLogLines = 60;  // as many lines as the host keeps
constexpr std::size_t kRounds = 4;     // hands of the fair deck kept
// A number sent this long before its hand showed up must have gone into it.
// The host waits kSeedWaitMs for numbers once it wants to deal; for an honest
// host to miss ours anyway, the way there and back would take longer than
// kSeedWaitMs + kSeedInTime while the connection was up.
constexpr std::int64_t kSeedInTime = 5000;

}  // namespace

GuestTable::GuestTable(Online& online, const PeerId& host, std::string host_name)
    : online_(online), host_(host), host_name_(std::move(host_name)) {}

void GuestTable::join(const Join& join) {
    net::Writer body;
    write(body, join);
    online_.send(host_, Kind::Join, body);
    state_ = State::Joining;
}

void GuestTable::message(Kind kind, net::Reader& body) {
    switch (kind) {
        case Kind::Snapshot: {
            Snapshot snapshot;
            if (read(body, snapshot)) take(std::move(snapshot));
            break;
        }
        case Kind::Refused: {
            const std::uint8_t why = body.u8();
            if (!body.done() || why > static_cast<std::uint8_t>(Refusal::Bankroll)) break;
            // After a seat was had, a refusal means the seat is gone.
            if (state_ == State::Seated) {
                state_ = State::Closed;
            } else if (state_ == State::Joining) {
                state_ = State::Refused;
                refusal_ = static_cast<Refusal>(why);
            }
            ++changes_;
            break;
        }
        case Kind::Closed:
            if (state_ == State::Seated || state_ == State::Joining) {
                state_ = State::Closed;
                ++changes_;
            }
            break;
        case Kind::Promise: {
            Promise promise;
            if (read(body, promise)) promised(promise);
            break;
        }
        case Kind::Reveal: {
            Reveal reveal;
            if (read(body, reveal)) revealed(reveal);
            break;
        }
        default: break;
    }
}

void GuestTable::take(Snapshot snapshot) {
    if (state_ != State::Joining && state_ != State::Seated) return;
    if (snapshot.log_reset) log_.clear();
    for (host::LogLine& line : snapshot.view.log) log_.push_back(std::move(line));
    while (log_.size() > kLogLines) log_.pop_front();
    snapshot.view.log.clear();
    view_ = std::move(snapshot.view);
    note_cards(view_);
    table_ = snapshot.table;
    received_at_ = now_;
    notes_ = snapshot.notes;
    if (snapshot.added > added_) added_ = snapshot.added;
    if (snapshot.left_with >= 0) {
        left_with_ = snapshot.left_with;
        state_ = State::Left;
    } else {
        state_ = State::Seated;
    }
    ++changes_;

    if (added_ > told_added_ && view_.viewer >= 0) {
        notices_.push_back(host::notice::ChipsAdded{view_.viewer, added_ - told_added_});
        told_added_ = added_;
    }
    // A hand whose end was never seen (its pictures did not get through) still ended.
    if (told_hand_ > 0 && view_.hand_number > told_hand_ + 1) {
        notices_.push_back(host::notice::HandOver{view_.hand_number - 1});
    }
    if (told_hand_ == 0 || view_.hand_number > told_hand_ + 1) told_hand_ = std::max(told_hand_, view_.hand_number - 1);
    if (view_.stage == host::Stage::Over && view_.hand_number > told_hand_) {
        told_hand_ = view_.hand_number;
        notices_.push_back(host::notice::HandOver{view_.hand_number});
    }
    if (left_with_ >= 0 && !told_left_) {
        told_left_ = true;
        notices_.push_back(host::notice::Left{view_.viewer, left_with_});
    }
}

void GuestTable::link(bool online) {
    if (online == host_online_) return;
    host_online_ = online;
    ++changes_;
    if (!online) {
        lost_since_ = now_;
        // Numbers on their way may be lost with the connection.
        for (auto& [hand, round] : rounds_) round.excused = true;
        return;
    }
    // Our number for the coming hand may have been lost on the way.
    if (online && !rounds_.empty() && !rounds_.rbegin()->second.dealt && rounds_.rbegin()->second.promised) {
        send_seed(rounds_.rbegin()->first);
    }
    // Back: ask to go on where we were.
    if (state_ == State::Seated) online_.send(host_, Kind::Resume, net::Writer{});
}

// ---------------------------------------------------------------- the fair deck
void GuestTable::promised(const Promise& promise) {
    if (state_ != State::Seated && state_ != State::Joining) return;
    Round& round = rounds_[promise.hand];
    // A promise once made stays; the same one again (after a lost connection) asks for the number again.
    if (round.promised && round.promise != promise.promise) {
        ++deck_alarms_;
        ++changes_;
        return;
    }
    round.promised = true;
    round.promise = promise.promise;
    if (round.sent_at < 0) round.seed = random_digest();
    send_seed(promise.hand);
    while (rounds_.size() > kRounds) rounds_.erase(rounds_.begin());
}

void GuestTable::send_seed(int hand) {
    Round& round = rounds_[hand];
    net::Writer body;
    write(body, Seed{hand, round.seed});
    if (online_.send(host_, Kind::Seed, body)) round.sent_at = now_;
}

// What we saw of a hand: our own cards, cards shown, the board.
void GuestTable::note_cards(const host::TableView& view) {
    if (view.hand_number <= 0 || view.stage == host::Stage::Idle) return;
    const auto found = rounds_.find(view.hand_number);
    // A hand we had no promise for (we sat down after it was made) is not checked.
    if (found == rounds_.end()) return;
    Round& round = found->second;
    if (round.seen_at < 0) round.seen_at = now_;
    for (std::size_t seat = 0; seat < view.seats.size(); ++seat) {
        const host::SeatView& place = view.seats[seat];
        if (place.taken && place.in_hand && place.cards) round.cards[static_cast<int>(seat)] = *place.cards;
    }
    if (view.board.size() > round.board.size()) round.board = view.board;
    // Cards that came after the secret: they too must be where the deck put them.
    if (round.dealt && !cards_fit(view.hand_number)) {
        ++deck_alarms_;
        round.dealt.reset();
    }
}

bool GuestTable::cards_fit(int hand) const {
    const Round& round = rounds_.at(hand);
    if (!round.dealt) return false;
    for (const auto& [seat, cards] : round.cards) {
        const auto dealt = round.dealt->holes.find(seat);
        if (dealt == round.dealt->holes.end() || dealt->second != cards) return false;
    }
    return std::equal(round.board.begin(), round.board.end(), round.dealt->board.begin(),
                      round.dealt->board.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(round.board.size(), 5)));
}

void GuestTable::revealed(const Reveal& reveal) {
    const auto found = rounds_.find(reveal.hand);
    if (found == rounds_.end() || !found->second.promised || found->second.dealt) return;
    Round& round = found->second;
    ++changes_;
    bool fair = commitment(reveal.secret) == round.promise;
    // Our number: if it is there it must be ours; if it is missing, it must have been late.
    const int me = view_.viewer;
    const auto mine = reveal.seeds.find(me);
    if (mine != reveal.seeds.end()) {
        if (mine->second != round.seed) fair = false;
    } else if (!round.excused && round.sent_at >= 0 && round.seen_at >= 0 &&
               round.seen_at - round.sent_at >= kSeedInTime) {
        fair = false;
    }
    if (fair) {
        round.dealt = deal_out(fair_deck(reveal.secret, reveal.hand, reveal.seeds), reveal.seats, reveal.button);
        fair = round.dealt && cards_fit(reveal.hand);
    }
    if (fair) {
        ++decks_checked_;
    } else {
        ++deck_alarms_;
        round.dealt.reset();
    }
}

void GuestTable::tick(std::int64_t now) {
    // The clock on the screen moves every second.
    if (view_.turn_ms >= 0 && (now - received_at_) / 1000 != (now_ - received_at_) / 1000) ++changes_;
    now_ = now;
}

host::TableView GuestTable::view() const {
    host::TableView view = view_;
    view.log.assign(log_.begin(), log_.end());
    if (view.turn_ms >= 0) {
        std::int64_t passed = now_ - received_at_;
        const std::int64_t turn = std::min<std::int64_t>(view.turn_ms, passed);
        view.turn_ms -= static_cast<int>(turn);
        passed -= turn;
        view.bank_ms = static_cast<int>(std::max<std::int64_t>(0, view.bank_ms - passed));
    }
    return view;
}

Chips GuestTable::stack() const {
    if (view_.viewer < 0 || static_cast<std::size_t>(view_.viewer) >= view_.seats.size()) return 0;
    return view_.seats[static_cast<std::size_t>(view_.viewer)].stack;
}

bool GuestTable::act(game::Action action) {
    if (state_ != State::Seated || !view_.choices || !host_online_) return false;
    net::Writer body;
    write(body, action, view_.hand_number);
    if (!online_.send(host_, Kind::Act, body)) return false;
    // The move is on its way; the buttons go until the host answers.
    view_.choices.reset();
    ++changes_;
    return true;
}

void GuestTable::want_chips(Chips target, Chips most) {
    if (state_ != State::Seated) return;
    net::Writer body;
    body.number(target);
    body.number(most);
    online_.send(host_, Kind::WantChips, body);
}

void GuestTable::leave() {
    if (state_ == State::Seated || state_ == State::Joining) online_.send(host_, Kind::Leave, net::Writer{});
}

void GuestTable::come_back() {
    if (state_ == State::Seated) online_.send(host_, Kind::Back, net::Writer{});
}

std::vector<host::Notice> GuestTable::take_notices() {
    return std::exchange(notices_, {});
}

}  // namespace poher::online
