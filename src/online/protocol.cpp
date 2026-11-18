#include "online/protocol.hpp"

#include "host/roster.hpp"

namespace poher::online {

namespace {

constexpr Chips kMostChips = 1'000'000'000'000;  // far beyond any bankroll; guards against nonsense
constexpr int kMostSeats = 6;
constexpr std::size_t kMostLines = 400;

void write_card(net::Writer& out, game::Card card) {
    out.u8(static_cast<std::uint8_t>((card.rank - 2) * 4 + static_cast<int>(card.suit)));
}

game::Card read_card(net::Reader& in) {
    const std::uint8_t code = in.u8();
    if (code >= 52) {
        in.fail();
        return {};
    }
    return game::Card{static_cast<std::uint8_t>(code / 4 + 2), static_cast<game::Suit>(code % 4)};
}

void write_cards(net::Writer& out, const std::vector<game::Card>& cards) {
    out.u8(static_cast<std::uint8_t>(cards.size()));
    for (const game::Card card : cards) write_card(out, card);
}

std::vector<game::Card> read_cards(net::Reader& in, std::size_t most) {
    const std::size_t count = in.u8();
    if (count > most) {
        in.fail();
        return {};
    }
    std::vector<game::Card> cards;
    for (std::size_t i = 0; i < count && in.ok(); ++i) cards.push_back(read_card(in));
    return cards;
}

Chips read_chips(net::Reader& in) {
    return in.number(0, kMostChips);
}

// A seat number, or -1 for none.
int read_seat(net::Reader& in) {
    return static_cast<int>(in.number(-1, kMostSeats - 1));
}

void write_notes(net::Writer& out, const bots::Notes& notes) {
    for (const int count : {notes.hands, notes.entered, notes.raised, notes.bets, notes.calls, notes.faced, notes.gave_up}) {
        out.number(count);
    }
}

bots::Notes read_notes(net::Reader& in) {
    bots::Notes notes;
    for (int* count : {&notes.hands, &notes.entered, &notes.raised, &notes.bets, &notes.calls, &notes.faced,
                       &notes.gave_up}) {
        *count = static_cast<int>(in.number(0, 1'000'000'000));
    }
    return notes;
}

void write_line(net::Writer& out, const host::LogLine& line) {
    out.u8(static_cast<std::uint8_t>(line.kind));
    out.text(line.name);
    out.flag(line.bot);
    out.number(line.amount);
    out.number(line.number);
    write_cards(out, line.cards);
}

host::LogLine read_line(net::Reader& in) {
    host::LogLine line;
    const std::uint8_t kind = in.u8();
    if (kind > static_cast<std::uint8_t>(host::LogLine::Kind::Says)) in.fail();
    line.kind = static_cast<host::LogLine::Kind>(kind);
    line.name = in.text();
    line.bot = in.flag();
    line.amount = read_chips(in);
    line.number = static_cast<int>(in.number(0, 1'000'000'000));
    line.cards = read_cards(in, 7);
    return line;
}

void write_view(net::Writer& out, const host::TableView& view) {
    out.number(view.stakes.small_blind);
    out.number(view.stakes.big_blind);
    out.number(view.stakes.min_buy_in);
    out.number(view.stakes.max_buy_in);
    out.number(view.hand_number);
    out.u8(static_cast<std::uint8_t>(view.stage));
    out.number(view.viewer);
    out.number(view.button);
    out.number(view.acting);
    out.number(view.aggressor);
    out.number(view.last_actor);
    out.number(view.turn_ms);
    out.number(view.bank_ms);
    write_cards(out, view.board);
    out.number(view.pot);
    out.u8(static_cast<std::uint8_t>(view.seats.size()));
    for (const host::SeatView& seat : view.seats) {
        out.flag(seat.taken);
        if (!seat.taken) continue;
        out.text(seat.name);
        out.text(seat.avatar);
        out.u8(static_cast<std::uint8_t>(seat.bot | seat.in_hand << 1 | seat.folded << 2 | seat.all_in << 3 |
                                         seat.away << 4 | seat.offline << 5 | seat.shown << 6 |
                                         seat.cards.has_value() << 7));
        out.number(seat.stack);
        out.number(seat.bet);
        out.u8(static_cast<std::uint8_t>(seat.note));
        out.number(seat.note_amount);
        out.number(seat.won);
        if (seat.cards) {
            write_card(out, (*seat.cards)[0]);
            write_card(out, (*seat.cards)[1]);
        }
    }
    out.flag(view.choices.has_value());
    if (view.choices) {
        out.flag(view.choices->can_check);
        out.number(view.choices->to_call);
        out.flag(view.choices->can_raise);
        out.number(view.choices->min_raise_to);
        out.number(view.choices->max_raise_to);
    }
    out.number(view.chips_coming);
    out.number(view.log_total);
    out.u16(static_cast<std::uint16_t>(view.log.size()));
    for (const host::LogLine& line : view.log) write_line(out, line);
}

bool read_view(net::Reader& in, host::TableView& view) {
    view = host::TableView{};
    view.stakes.small_blind = read_chips(in);
    view.stakes.big_blind = read_chips(in);
    view.stakes.min_buy_in = read_chips(in);
    view.stakes.max_buy_in = read_chips(in);
    view.hand_number = static_cast<int>(in.number(0, 1'000'000'000));
    const std::uint8_t stage = in.u8();
    if (stage > static_cast<std::uint8_t>(host::Stage::Over)) in.fail();
    view.stage = static_cast<host::Stage>(stage);
    view.viewer = read_seat(in);
    view.button = read_seat(in);
    view.acting = read_seat(in);
    view.aggressor = read_seat(in);
    view.last_actor = read_seat(in);
    view.turn_ms = static_cast<int>(in.number(-1, 24 * 3600 * 1000));
    view.bank_ms = static_cast<int>(in.number(-1, 24 * 3600 * 1000));
    view.board = read_cards(in, 5);
    view.pot = read_chips(in);
    const std::size_t seats = in.u8();
    if (seats > kMostSeats) in.fail();
    view.seats.resize(in.ok() ? seats : 0);
    for (host::SeatView& seat : view.seats) {
        seat.taken = in.flag();
        if (!seat.taken) continue;
        seat.name = in.text();
        seat.avatar = in.text();
        const std::uint8_t flags = in.u8();
        seat.bot = flags & 1;
        seat.in_hand = flags & 2;
        seat.folded = flags & 4;
        seat.all_in = flags & 8;
        seat.away = flags & 16;
        seat.offline = flags & 32;
        seat.shown = flags & 64;
        seat.stack = read_chips(in);
        seat.bet = read_chips(in);
        const std::uint8_t note = in.u8();
        if (note > static_cast<std::uint8_t>(host::Note::Win)) in.fail();
        seat.note = static_cast<host::Note>(note);
        seat.note_amount = read_chips(in);
        seat.won = read_chips(in);
        if (flags & 128) {
            const game::Card first = read_card(in);
            const game::Card second = read_card(in);
            seat.cards = std::array<game::Card, 2>{first, second};
        }
    }
    const int count = static_cast<int>(view.seats.size());
    for (const int seat : {view.viewer, view.button, view.acting, view.aggressor, view.last_actor}) {
        if (seat >= count) in.fail();
    }
    if (in.flag()) {
        game::Choices choices;
        choices.can_check = in.flag();
        choices.to_call = read_chips(in);
        choices.can_raise = in.flag();
        choices.min_raise_to = read_chips(in);
        choices.max_raise_to = read_chips(in);
        view.choices = choices;
    }
    view.chips_coming = read_chips(in);
    view.log_total = in.number(0, kMostChips);
    const std::size_t lines = in.u16();
    if (lines > kMostLines) in.fail();
    for (std::size_t i = 0; i < lines && in.ok(); ++i) view.log.push_back(read_line(in));
    return in.ok();
}

void write_stats(net::Writer& out, const PublicStats& stats) {
    out.number(stats.hands);
    out.number(stats.hands_won);
    out.number(stats.biggest_pot);
    out.number(stats.bankroll);
    out.number(stats.refills);
    out.number(stats.from_bots);
    out.number(stats.from_people);
}

void read_stats(net::Reader& in, PublicStats& stats) {
    stats.hands = in.number(0, kMostChips);
    stats.hands_won = in.number(0, kMostChips);
    stats.biggest_pot = read_chips(in);
    stats.bankroll = read_chips(in);
    stats.refills = in.number(0, kMostChips);
    stats.from_bots = in.number(-kMostChips, kMostChips);
    stats.from_people = in.number(-kMostChips, kMostChips);
}

}  // namespace

void write(net::Writer& out, const TableInfo& table) {
    out.u8(table.level);
    out.u8(table.seats);
    out.u8(table.people);
    out.u8(table.bots);
    out.flag(table.open);
    out.u8(table.turn_seconds);
}

bool read(net::Reader& in, TableInfo& table) {
    table.level = in.u8();
    table.seats = in.u8();
    table.people = in.u8();
    table.bots = in.u8();
    table.open = in.flag();
    table.turn_seconds = in.u8();
    if (table.level >= host::kLevels.size() || table.seats > kMostSeats ||
        table.people + table.bots > table.seats) {
        in.fail();
    }
    return in.ok();
}

void write(net::Writer& out, const Status& status) {
    // The protocol number comes first and stays there forever: whatever else
    // changes, an older program can still tell that it has to be updated.
    out.number(status.protocol);
    out.text(status.version);
    out.u8(static_cast<std::uint8_t>(status.activity));
    write(out, status.table);
    out.text(status.avatar);
    write_stats(out, status.stats);
}

bool read(net::Reader& in, Status& status) {
    status = Status{};
    status.protocol = static_cast<int>(in.number(0, 1'000'000));
    status.version = in.text();
    if (!in.ok() || status.protocol != kProtocol) return in.ok();  // the rest may be in another shape
    const std::uint8_t activity = in.u8();
    if (activity > static_cast<std::uint8_t>(Activity::Guest)) in.fail();
    status.activity = static_cast<Activity>(activity);
    read(in, status.table);
    status.avatar = in.text();
    read_stats(in, status.stats);
    return in.done();
}

void write(net::Writer& out, const Join& join) {
    out.number(join.protocol);
    out.text(join.name);
    out.text(join.avatar);
    out.number(join.buy_in);
}

bool read(net::Reader& in, Join& join) {
    join = Join{};
    join.protocol = static_cast<int>(in.number(0, 1'000'000));
    if (!in.ok() || join.protocol != kProtocol) return in.ok();
    join.name = in.text();
    join.avatar = in.text();
    join.buy_in = read_chips(in);
    return in.done();
}

void write(net::Writer& out, const Snapshot& snapshot) {
    write_view(out, snapshot.view);
    out.flag(snapshot.log_reset);
    write(out, snapshot.table);
    out.number(snapshot.added);
    out.number(snapshot.left_with);
    write_notes(out, snapshot.notes);
}

bool read(net::Reader& in, Snapshot& snapshot) {
    snapshot = Snapshot{};
    read_view(in, snapshot.view);
    snapshot.log_reset = in.flag();
    read(in, snapshot.table);
    snapshot.added = read_chips(in);
    snapshot.left_with = in.number(-1, kMostChips);
    snapshot.notes = read_notes(in);
    return in.done();
}

void write(net::Writer& out, const game::Action& action, int hand_number) {
    out.number(hand_number);
    out.u8(static_cast<std::uint8_t>(action.move));
    out.number(action.to);
}

bool read(net::Reader& in, game::Action& action, int& hand_number) {
    hand_number = static_cast<int>(in.number(0, 1'000'000'000));
    const std::uint8_t move = in.u8();
    if (move > static_cast<std::uint8_t>(game::Move::Raise)) in.fail();
    action.move = static_cast<game::Move>(move);
    action.to = read_chips(in);
    return in.done();
}

std::vector<std::uint8_t> message(Kind kind, const net::Writer& body) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(body.size() + 1);
    bytes.push_back(static_cast<std::uint8_t>(kind));
    bytes.insert(bytes.end(), body.bytes().begin(), body.bytes().end());
    return bytes;
}

// ---------------------------------------------------------------- the fair deck
namespace {

constexpr int kMostHands = 1 << 30;

void write_digest(net::Writer& out, const Digest& digest) {
    out.raw(digest);
}

void read_digest(net::Reader& in, Digest& digest) {
    in.raw(digest);
}

}  // namespace

void write(net::Writer& out, const Promise& promise) {
    out.number(promise.hand);
    write_digest(out, promise.promise);
}

bool read(net::Reader& in, Promise& promise) {
    promise.hand = static_cast<int>(in.number(1, kMostHands));
    read_digest(in, promise.promise);
    return in.done();
}

void write(net::Writer& out, const Seed& seed) {
    out.number(seed.hand);
    write_digest(out, seed.seed);
}

bool read(net::Reader& in, Seed& seed) {
    seed.hand = static_cast<int>(in.number(1, kMostHands));
    read_digest(in, seed.seed);
    return in.done();
}

void write(net::Writer& out, const Reveal& reveal) {
    out.number(reveal.hand);
    write_digest(out, reveal.secret);
    out.number(reveal.button);
    out.u8(static_cast<std::uint8_t>(reveal.seats.size()));
    for (const int seat : reveal.seats) out.u8(static_cast<std::uint8_t>(seat));
    out.u8(static_cast<std::uint8_t>(reveal.seeds.size()));
    for (const auto& [seat, seed] : reveal.seeds) {
        out.u8(static_cast<std::uint8_t>(seat));
        write_digest(out, seed);
    }
}

bool read(net::Reader& in, Reveal& reveal) {
    reveal.hand = static_cast<int>(in.number(1, kMostHands));
    read_digest(in, reveal.secret);
    reveal.button = static_cast<int>(in.number(0, kMostSeats - 1));
    const std::size_t seats = in.u8();
    if (seats < 2 || seats > kMostSeats) in.fail();
    reveal.seats.clear();
    for (std::size_t i = 0; i < seats && in.ok(); ++i) {
        const int seat = in.u8();
        if (seat >= kMostSeats) in.fail();
        reveal.seats.push_back(seat);
    }
    const std::size_t seeds = in.u8();
    if (seeds > kMostSeats) in.fail();
    reveal.seeds.clear();
    for (std::size_t i = 0; i < seeds && in.ok(); ++i) {
        const int seat = in.u8();
        if (seat >= kMostSeats) in.fail();
        read_digest(in, reveal.seeds[seat]);
    }
    return in.done();
}

}  // namespace poher::online
