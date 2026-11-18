#include "online/host_room.hpp"

#include <algorithm>

#include "host/roster.hpp"

namespace poher::online {

HostRoom::HostRoom(host::Dealer& dealer, Online& online, TableInfo table)
    : dealer_(dealer), online_(online), table_(table) {
    dealer_.set_deck_source([this](int hand) { return deck_for(hand); });
}

HostRoom::~HostRoom() {
    dealer_.set_deck_source({});
}

// ---------------------------------------------------------------- the fair deck
void HostRoom::promise(int hand) {
    next_ = Round{};
    next_.hand = hand;
    next_.secret = random_digest();
    next_.promise = commitment(next_.secret);
    for (Guest& guest : guests_) send_promise(guest);
}

void HostRoom::send_promise(Guest& guest) {
    if (next_.hand == 0 || guest.seat < 0 || guest.left_with >= 0 || !guest.online) return;
    net::Writer body;
    write(body, Promise{next_.hand, next_.promise});
    if (online_.send(guest.id, Kind::Promise, body)) guest.promised = next_.hand;
}

std::optional<std::vector<game::Card>> HostRoom::deck_for(int hand) {
    // Asked again after the table could not deal: the same deck.
    if (dealt_.hand == hand && !dealt_.deck.empty()) return dealt_.deck;
    if (next_.hand != hand) promise(hand);
    const std::int64_t now = dealer_.now();
    if (next_.asked_at < 0) next_.asked_at = now;
    // The numbers of friends who sit here and are in touch, but not for long.
    const bool all_in = std::all_of(guests_.begin(), guests_.end(), [&](const Guest& guest) {
        return guest.seat < 0 || guest.left_with >= 0 || !guest.online || guest.promised != hand ||
               next_.seeds.contains(guest.seat);
    });
    if (!all_in && now - next_.asked_at < kSeedWaitMs) return std::nullopt;
    reveal();  // the hand before, if nobody has seen its secret yet
    dealt_ = std::move(next_);
    dealt_.deck = fair_deck(dealt_.secret, hand, dealt_.seeds);
    // The next promise goes out at once, so the numbers for it are in long before it is needed.
    promise(hand + 1);
    return dealt_.deck;
}

// After a hand: its secret, so that every guest can check the deck.
void HostRoom::reveal() {
    if (dealt_.hand == 0 || dealt_.revealed) return;
    const host::Dealer::Deal& deal = dealer_.last_deal();
    if (deal.hand_number != dealt_.hand) return;
    dealt_.revealed = true;
    Reveal reveal;
    reveal.hand = dealt_.hand;
    reveal.secret = dealt_.secret;
    reveal.button = deal.button;
    reveal.seats = deal.seats;
    reveal.seeds = dealt_.seeds;
    net::Writer body;
    write(body, reveal);
    for (Guest& guest : guests_) {
        if (guest.seat >= 0 && guest.left_with < 0 && guest.online) online_.send(guest.id, Kind::Reveal, body);
    }
}

HostRoom::Guest* HostRoom::find(const PeerId& id) {
    for (Guest& guest : guests_) {
        if (guest.id == id && guest.left_with < 0) return &guest;
    }
    return nullptr;
}

HostRoom::Guest* HostRoom::at_seat(int seat) {
    for (Guest& guest : guests_) {
        if (guest.seat == seat && guest.left_with < 0) return &guest;
    }
    return nullptr;
}

bool HostRoom::guest_seat(int seat) const {
    return std::any_of(guests_.begin(), guests_.end(),
                       [&](const Guest& guest) { return guest.seat == seat && guest.left_with < 0; });
}

std::optional<PeerId> HostRoom::guest_in(int seat) const {
    for (const Guest& guest : guests_) {
        if (guest.seat == seat && guest.left_with < 0) return guest.id;
    }
    return std::nullopt;
}

int HostRoom::guests() const {
    return static_cast<int>(std::count_if(guests_.begin(), guests_.end(),
                                           [](const Guest& guest) { return guest.left_with < 0; }));
}

// Two players with one name would be one player to the bots and in the history.
std::string HostRoom::unique_name(std::string name) const {
    const auto taken = [&](const std::string& candidate) {
        const host::TableView view = dealer_.view(-1);
        for (const host::SeatView& seat : view.seats) {
            if (seat.taken && seat.name == candidate) return true;
        }
        for (const Guest& guest : guests_) {
            if (guest.left_with < 0 && guest.name == candidate) return true;
        }
        for (const host::Bot& bot : host::kBots) {
            if (bot.name == candidate) return true;
        }
        return false;
    };
    if (name.empty()) name = "?";
    if (!taken(name)) return name;
    for (int number = 2;; ++number) {
        const std::string candidate = name + " " + std::to_string(number);
        if (!taken(candidate)) return candidate;
    }
}

void HostRoom::refuse(const PeerId& to, Refusal why) {
    net::Writer body;
    body.u8(static_cast<std::uint8_t>(why));
    online_.send(to, Kind::Refused, body);
}

void HostRoom::join(const PeerId& from, const Join& request) {
    if (request.protocol != kProtocol) return refuse(from, Refusal::Version);
    if (!table_.open && !invited_.contains(from)) return refuse(from, Refusal::Closed);
    const game::Stakes& stakes = dealer_.table().stakes();
    if (request.buy_in < stakes.min_buy_in || request.buy_in > stakes.max_buy_in) {
        return refuse(from, Refusal::Bankroll);
    }
    // Back after a restart of their program: the old seat goes, they counted its chips as theirs.
    if (Guest* old = find(from)) {
        if (old->seat >= 0) dealer_.leave(old->seat);
        old->left_with = 0;
        old->online = false;
    }
    int seat = -1;
    for (int i = 0; i < dealer_.table().seat_count() && seat < 0; ++i) {
        const bool promised = std::any_of(guests_.begin(), guests_.end(), [&](const Guest& guest) {
            return guest.left_with < 0 && guest.seat < 0 && guest.wanted_seat == i;
        });
        if (!dealer_.table().seat(i).taken && !promised) seat = i;
    }
    if (seat < 0 && make_room) seat = make_room();
    if (seat < 0) return refuse(from, Refusal::Full);

    Guest guest;
    guest.id = from;
    std::string name = request.name;
    if (name.empty()) {
        if (const std::optional<Mate> mate = online_.mate(from)) name = mate->name;
    }
    guest.name = unique_name(name.substr(0, 48));
    guest.avatar = request.avatar.substr(0, 32);
    guest.buy_in = request.buy_in;
    guest.wanted_seat = seat;
    invited_.erase(from);
    guests_.push_back(std::move(guest));
    tick();
}

void HostRoom::message(const PeerId& from, Kind kind, net::Reader& body) {
    switch (kind) {
        case Kind::Join: {
            Join request;
            if (read(body, request)) join(from, request);
            return;
        }
        case Kind::Resume: {
            Guest* guest = find(from);
            if (guest == nullptr) return refuse(from, Refusal::NoTable);
            guest->online = true;
            guest->fresh = true;
            guest->log_sent = -1;
            if (guest->seat >= 0) dealer_.set_offline(guest->seat, false);
            send_promise(*guest);
            return;
        }
        default: break;
    }
    Guest* guest = find(from);
    if (guest == nullptr) return;
    if (guest->seat < 0) {
        // Gave up waiting for a seat: nothing to settle. The seat being freed stays free.
        if (kind == Kind::Leave && body.done()) std::erase_if(guests_, [&](const Guest& other) { return &other == guest; });
        return;
    }
    switch (kind) {
        case Kind::Act: {
            game::Action action;
            int hand = 0;
            if (read(body, action, hand) && hand == dealer_.view(guest->seat).hand_number) {
                dealer_.act(guest->seat, action);
            }
            break;
        }
        case Kind::WantChips: {
            const Chips target = body.number(0, dealer_.table().stakes().max_buy_in);
            const Chips most = body.number(0, dealer_.table().stakes().max_buy_in);
            if (body.done()) dealer_.want_chips(guest->seat, target, most);
            break;
        }
        case Kind::Leave:
            if (body.done()) dealer_.leave(guest->seat);
            break;
        case Kind::Back:
            if (body.done()) dealer_.come_back(guest->seat);
            break;
        case Kind::Seed: {
            // Only for the hand promised, and the first number counts.
            Seed seed;
            if (read(body, seed) && seed.hand == next_.hand && !next_.seeds.contains(guest->seat)) {
                next_.seeds[guest->seat] = seed.seed;
            }
            break;
        }
        default: break;
    }
}

void HostRoom::link(const PeerId& who, bool online) {
    Guest* guest = find(who);
    if (guest == nullptr) return;
    // Coming back is announced by the guest with Resume: only they know whether they still sit here.
    if (online) return;
    guest->online = false;
    if (guest->seat >= 0) {
        dealer_.set_offline(guest->seat, true);
    } else {
        guest->left_with = 0;  // was waiting for a seat; gone
    }
}

bool HostRoom::notice(const host::Notice& notice) {
    if (const auto* added = std::get_if<host::notice::ChipsAdded>(&notice)) {
        Guest* guest = at_seat(added->seat);
        if (guest == nullptr) return false;
        guest->added += added->amount;
        guest->fresh = true;
        return true;
    }
    if (const auto* left = std::get_if<host::notice::Left>(&notice)) {
        Guest* guest = at_seat(left->seat);
        if (guest == nullptr) return false;
        guest->left_with = left->chips;
        guest->fresh = true;
        send_picture(*guest);
        guest->seat = -1;
        return true;
    }
    return false;
}

bool HostRoom::send_picture(Guest& guest) {
    if (!guest.online) return false;
    Snapshot snapshot;
    snapshot.view = dealer_.view(guest.left_with >= 0 ? -1 : guest.seat);
    snapshot.table = table_;
    snapshot.added = guest.added;
    snapshot.left_with = guest.left_with;
    if (const bots::Notes* notes = dealer_.notebook().find(guest.name)) snapshot.notes = *notes;
    // Only the lines the guest has not had yet.
    std::vector<host::LogLine>& log = snapshot.view.log;
    const std::int64_t first = snapshot.view.log_total - static_cast<std::int64_t>(log.size());
    if (guest.log_sent < first) {
        snapshot.log_reset = true;
    } else {
        log.erase(log.begin(), log.begin() + static_cast<std::ptrdiff_t>(guest.log_sent - first));
    }
    net::Writer body;
    write(body, snapshot);
    if (!online_.send(guest.id, Kind::Snapshot, body)) return false;
    guest.sent = dealer_.changes();
    guest.fresh = false;
    guest.log_sent = snapshot.view.log_total;
    return true;
}

void HostRoom::tick() {
    for (Guest& guest : guests_) {
        if (guest.left_with >= 0) {
            if (guest.fresh) send_picture(guest);  // the news that they got up has not gone yet
            continue;
        }
        // Waiting for a seat: sit down as soon as it is empty.
        if (guest.seat < 0 && guest.wanted_seat >= 0 && !dealer_.table().seat(guest.wanted_seat).taken) {
            if (dealer_.sit(guest.wanted_seat, host::Player{guest.name, guest.avatar, false}, guest.buy_in)) {
                guest.seat = guest.wanted_seat;
                guest.added = guest.buy_in;
                guest.fresh = true;
                if (on_seated) on_seated(guest.id, guest.name);
            }
        }
        if (guest.seat >= 0 && (guest.fresh || guest.sent != dealer_.changes())) send_picture(guest);
        if (guest.seat >= 0 && guest.promised != next_.hand) send_promise(guest);
    }
    std::erase_if(guests_, [](const Guest& guest) { return guest.left_with >= 0 && !guest.fresh; });
    std::erase_if(guests_, [](const Guest& guest) { return guest.left_with >= 0 && !guest.online; });
    const host::TableView view = dealer_.view(-1);
    // The hand is over and everybody has its last picture: its secret can go.
    if (view.hand_number == dealt_.hand && view.stage != host::Stage::Playing) reveal();
    // How the table looks to friends.
    table_.people = 0;
    table_.bots = 0;
    for (const host::SeatView& seat : view.seats) {
        if (!seat.taken) continue;
        if (seat.bot) {
            ++table_.bots;
        } else {
            ++table_.people;
        }
    }
}

void HostRoom::close() {
    for (Guest& guest : guests_) {
        if (guest.left_with < 0 && guest.online) online_.send(guest.id, Kind::Closed, net::Writer{});
    }
    guests_.clear();
}

}  // namespace poher::online
