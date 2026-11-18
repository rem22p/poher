// Playing with friends, without a real network: every program is in this
// test, joined through a net::Hub.
#include <algorithm>
#include <cstdio>

#include "bots/simple_bot.hpp"
#include "check.hpp"
#include "host/dealer.hpp"
#include "net/loop_network.hpp"
#include "online/guest_table.hpp"
#include "online/host_room.hpp"
#include "online/sha256.hpp"
#include "online/online.hpp"

using namespace poher;
using game::Chips;
using online::GuestTable;
using online::HostRoom;
using online::Online;

namespace {

Online::Factory on(net::Hub& hub, std::string name) {
    return [&hub, name](net::Events events, std::string&) { return hub.join(name, std::move(events)); };
}

void poll(std::initializer_list<Online*> all) {
    for (int round = 0; round < 3; ++round) {
        for (Online* one : all) one->poll();
    }
}

void befriend(Online& one, Online& other) {
    CHECK(one.add(other.code()) == net::AddResult::Ok);
    poll({&one, &other});
    other.accept(one.id());
    poll({&one, &other});
}

// A plain way to play from what the screen shows.
game::Action plain_move(const host::TableView& view, game::Random& random) {
    const game::Choices& choices = *view.choices;
    if (choices.can_raise && random.chance(0.15)) return game::Action{game::Move::Raise, choices.min_raise_to};
    if (choices.can_check) return game::Action{game::Move::Check};
    if (choices.to_call <= view.stakes.big_blind * 4 || random.chance(0.3)) return game::Action{game::Move::Call};
    return game::Action{game::Move::Fold};
}

// No pauses but the one after a hand, so that everybody sees how it ended.
host::Pace pace() {
    host::Pace pace = host::Pace::none();
    pace.after_hand = pace.after_showdown = 200;
    return pace;
}

// A host with a table of four seats and two friends who come to it.
struct Party {
    net::Hub hub;
    Online host_net{on(hub, "Host")};
    Online ann_net{on(hub, "Ann")};
    Online bob_net{on(hub, "Bob")};
    host::Pace given;
    host::Dealer dealer{game::Stakes{1, 2, 40, 200}, 4, 21, given};
    HostRoom room{dealer, host_net, online::TableInfo{0, 4, 1, 1, true, 30}};
    GuestTable ann{ann_net, host_net.id(), "Host"};
    GuestTable bob{bob_net, host_net.id(), "Host"};
    game::Random random{8};
    std::int64_t now = 0;
    Chips host_in = 0, host_out = 0;  // what the host and the bot brought and took
    Chips ann_paid = 0, ann_took = 0;
    Chips bob_paid = 0, bob_took = 0;
    int ann_hands = 0;
    bool leaked = false;  // somebody saw cards they should not have

    explicit Party(host::Pace pace_of_play = pace()) : given(pace_of_play) {
        befriend(host_net, ann_net);
        befriend(host_net, bob_net);
        host::TurnClock clock;
        clock.turn_ms = 30000;
        dealer.set_clock(clock);
        dealer.sit(0, host::Player{"Host", "01", false}, 200);
        dealer.sit(1, host::Player{"Bot1", "02", true}, 200);
        host_in = 400;
        host_net.on_table = [this](const net::PeerId& from, online::Kind kind, net::Reader& body) {
            room.message(from, kind, body);
        };
        host_net.on_link = [this](const net::PeerId& who, bool online) { room.link(who, online); };
        const net::PeerId host = host_net.id();
        ann_net.on_table = [this, host](const net::PeerId& from, online::Kind kind, net::Reader& body) {
            if (from == host) ann.message(kind, body);
        };
        ann_net.on_link = [this, host](const net::PeerId& who, bool online) {
            if (who == host) ann.link(online);
        };
        bob_net.on_table = [this, host](const net::PeerId& from, online::Kind kind, net::Reader& body) {
            if (from == host) bob.message(kind, body);
        };
        bob_net.on_link = [this, host](const net::PeerId& who, bool online) {
            if (who == host) bob.link(online);
        };
    }

    void count(GuestTable& guest, Chips& paid, Chips& took, int* hands = nullptr) {
        for (const host::Notice& notice : guest.take_notices()) {
            if (const auto* added = std::get_if<host::notice::ChipsAdded>(&notice)) paid += added->amount;
            if (const auto* left = std::get_if<host::notice::Left>(&notice)) took += left->chips;
            if (hands != nullptr && std::holds_alternative<host::notice::HandOver>(notice)) ++*hands;
        }
    }

    void step(bool play = true) {
        now += 50;
        dealer.tick(now);
        for (const host::Notice& notice : dealer.take_notices()) {
            if (room.notice(notice)) continue;
            if (const auto* added = std::get_if<host::notice::ChipsAdded>(&notice)) host_in += added->amount;
            if (const auto* left = std::get_if<host::notice::Left>(&notice)) host_out += left->chips;
        }
        room.tick();
        poll({&host_net, &ann_net, &bob_net});
        ann.tick(now);
        bob.tick(now);
        count(ann, ann_paid, ann_took, &ann_hands);
        count(bob, bob_paid, bob_took);
        for (GuestTable* guest : {&ann, &bob}) {
            const host::TableView view = guest->view();
            for (std::size_t seat = 0; seat < view.seats.size(); ++seat) {
                if (static_cast<int>(seat) != view.viewer && view.seats[seat].cards && !view.seats[seat].shown) {
                    leaked = true;
                }
            }
        }
        if (!play) return;
        if (dealer.view(0).choices) dealer.act(0, plain_move(dealer.view(0), random));
        for (GuestTable* guest : {&ann, &bob}) {
            const host::TableView view = guest->view();
            if (view.choices) guest->act(plain_move(view, random));
            // Broke: buy in again.
            if (guest->state() == GuestTable::State::Seated && view.viewer >= 0) {
                const host::SeatView& me = view.seats[static_cast<std::size_t>(view.viewer)];
                if (me.stack == 0 && view.chips_coming == 0 && !me.in_hand) guest->want_chips(200, 200);
            }
        }
        if (dealer.table().seat(0).taken && dealer.stack(0) == 0 && !dealer.table().seat(0).in_hand) {
            dealer.want_chips(0, 200, 200);
        }
    }

    Chips on_table() const {
        Chips sum = 0;
        for (int seat = 0; seat < dealer.table().seat_count(); ++seat) sum += dealer.table().seat(seat).stack;
        return sum;
    }
};

}  // namespace

TEST(messages_survive_the_trip) {
    online::Snapshot snapshot;
    host::TableView& view = snapshot.view;
    view.stakes = game::Stakes{5, 10, 200, 1000};
    view.hand_number = 1234;
    view.stage = host::Stage::Playing;
    view.viewer = 2;
    view.button = 0;
    view.acting = 2;
    view.aggressor = 1;
    view.last_actor = 1;
    view.turn_ms = 12345;
    view.bank_ms = 60000;
    view.board = game::parse_cards("Ah Kd 7c 2s");
    view.pot = 98765;
    view.seats.resize(4);
    view.seats[0].taken = true;
    view.seats[0].name = "Рем";
    view.seats[0].avatar = "03";
    view.seats[0].stack = 900;
    view.seats[0].in_hand = true;
    view.seats[0].folded = true;
    view.seats[1].taken = true;
    view.seats[1].name = "Chad";
    view.seats[1].avatar = "02";
    view.seats[1].bot = true;
    view.seats[1].bet = 1500;
    view.seats[1].in_hand = true;
    view.seats[1].all_in = true;
    view.seats[1].note = host::Note::AllIn;
    view.seats[1].note_amount = 1500;
    view.seats[2].taken = true;
    view.seats[2].name = "Bob";
    view.seats[2].stack = 777;
    view.seats[2].cards = std::array<game::Card, 2>{game::Card{game::kAce, game::Suit::Hearts},
                                                     game::Card{2, game::Suit::Clubs}};
    view.seats[2].offline = true;
    view.choices = game::Choices{false, 1500, true, 3000, 777};
    view.chips_coming = 100;
    view.log_total = 99;
    host::LogLine line;
    line.kind = host::LogLine::Kind::Shows;
    line.name = "Chad";
    line.bot = true;
    line.cards = game::parse_cards("Qs Qd");
    view.log.push_back(line);
    snapshot.log_reset = true;
    snapshot.table = online::TableInfo{2, 6, 2, 3, true, 30};
    snapshot.added = 1200;
    snapshot.notes = bots::Notes{50, 20, 10, 8, 4, 12, 5};

    net::Writer out;
    write(out, snapshot);
    online::Snapshot back;
    net::Reader in(out.bytes());
    CHECK(read(in, back));
    CHECK_EQ(back.view.hand_number, 1234);
    CHECK_EQ(back.view.turn_ms, 12345);
    CHECK_EQ(back.view.board.size(), 4);
    CHECK(back.view.board[1] == (game::Card{game::kKing, game::Suit::Diamonds}));
    CHECK_EQ(back.view.seats.size(), 4);
    CHECK(back.view.seats[0].name == "Рем");
    CHECK(back.view.seats[1].bot);
    CHECK(back.view.seats[1].all_in);
    CHECK(back.view.seats[1].note == host::Note::AllIn);
    CHECK(!back.view.seats[3].taken);
    CHECK(back.view.seats[2].cards.has_value());
    CHECK(back.view.seats[2].offline);
    CHECK(!back.view.seats[0].cards.has_value());
    CHECK(back.view.choices.has_value());
    CHECK_EQ(back.view.choices->min_raise_to, 3000);
    CHECK_EQ(back.view.log.size(), 1);
    CHECK_EQ(back.view.log[0].cards.size(), 2);
    CHECK(back.log_reset);
    CHECK_EQ(back.table.turn_seconds, 30);
    CHECK_EQ(back.added, 1200);
    CHECK_EQ(back.left_with, -1);
    CHECK_EQ(back.notes.gave_up, 5);

    // Anything cut short or spoilt is refused, never half taken.
    for (std::size_t size = 0; size < out.size(); ++size) {
        std::vector<std::uint8_t> cut(out.bytes().begin(), out.bytes().begin() + static_cast<std::ptrdiff_t>(size));
        net::Reader short_in(cut);
        online::Snapshot broken;
        CHECK(!read(short_in, broken));
    }
    game::Random random(3);
    int accepted = 0;
    for (int round = 0; round < 3000; ++round) {
        std::vector<std::uint8_t> spoilt = out.bytes();
        spoilt[random.below(static_cast<std::uint32_t>(spoilt.size()))] ^=
            static_cast<std::uint8_t>(1 + random.below(255));
        net::Reader spoilt_in(spoilt);
        online::Snapshot maybe;
        if (read(spoilt_in, maybe)) {
            ++accepted;
            // Whatever got through is within bounds.
            CHECK(maybe.view.seats.size() <= 6);
            CHECK(maybe.view.board.size() <= 5);
            CHECK(maybe.view.viewer < static_cast<int>(maybe.view.seats.size()));
        }
    }
    CHECK(accepted < 3000);
}

TEST(long_messages_go_in_pieces) {
    net::Hub hub;
    Online ann(on(hub, "Ann"));
    Online bob(on(hub, "Bob"));
    std::vector<std::vector<std::uint8_t>> got;
    bob.on_table = [&](const net::PeerId&, online::Kind kind, net::Reader& body) {
        CHECK(kind == online::Kind::Snapshot);
        std::vector<std::uint8_t> bytes(body.left());
        body.raw(bytes);
        got.push_back(std::move(bytes));
    };
    CHECK(ann.add(bob.code()) == net::AddResult::Ok);
    bob.poll();
    CHECK_EQ(bob.requests().size(), 1);
    bob.accept(ann.id());
    ann.poll();
    bob.poll();
    CHECK_EQ(ann.mates().size(), 1);
    CHECK(ann.mates()[0].name == "Bob");
    CHECK(ann.mates()[0].link != net::Link::None);

    for (const std::size_t size : {0, 1, 1197, 1198, 1199, 5000, 70000}) {
        net::Writer body;
        for (std::size_t i = 0; i < size; ++i) body.u8(static_cast<std::uint8_t>(i * 7));
        CHECK(ann.send(bob.id(), online::Kind::Snapshot, body));
        bob.poll();
        CHECK_EQ(got.size(), 1);
        if (got.size() == 1) CHECK(got[0] == body.bytes());
        got.clear();
    }
}

TEST(friends_hear_what_each_other_is_doing) {
    net::Hub hub;
    Online ann(on(hub, "Ann"));
    Online bob(on(hub, "Bob"));
    online::Status status;
    status.version = "0.6";
    status.activity = online::Activity::Hosting;
    status.table = online::TableInfo{1, 6, 1, 4, true, 30};
    status.stats.hands = 500;
    ann.set_status(status);
    befriend(ann, bob);
    // Heard as soon as they became friends.
    std::vector<online::Mate> mates = bob.mates();
    CHECK_EQ(mates.size(), 1);
    if (mates.size() != 1) return;
    CHECK(mates[0].name == "Ann");
    CHECK(mates[0].status.has_value());
    CHECK(mates[0].status && mates[0].status->activity == online::Activity::Hosting);
    CHECK(mates[0].status && mates[0].status->table.bots == 4);
    CHECK(mates[0].status && mates[0].status->stats.hands == 500);
    // A change is heard at once.
    status.activity = online::Activity::Menu;
    ann.set_status(status);
    poll({&ann, &bob});
    CHECK(bob.mates()[0].status->activity == online::Activity::Menu);
    // Offline: nothing is known any more, and the news comes again when they are back.
    hub.set_online(ann.id(), false);
    poll({&ann, &bob});
    CHECK(bob.mates()[0].link == net::Link::None);
    CHECK(!bob.mates()[0].status.has_value());
    hub.set_online(ann.id(), true);
    poll({&ann, &bob});
    CHECK(bob.mates()[0].status.has_value());

    // An invitation.
    ann.invite(bob.id(), online::TableInfo{2, 6, 2, 1, false, 15});
    poll({&ann, &bob});
    CHECK(bob.invitation().has_value());
    CHECK(bob.invitation() && bob.invitation()->name == "Ann");
    CHECK(bob.invitation() && bob.invitation()->table.level == 2);
    bob.drop_invitation();
    CHECK(!bob.invitation().has_value());

    // Codes: wrong, own, twice.
    CHECK(ann.add("not a code") == net::AddResult::BadCode);
    CHECK(ann.add(ann.code()) == net::AddResult::OwnCode);
    CHECK(ann.add(bob.code()) == net::AddResult::AlreadyAdded);
}

TEST(friends_play_at_the_hosts_table) {
    Party party;
    party.ann.join(online::Join{online::kProtocol, "Ann", "03", 200});
    party.bob.join(online::Join{online::kProtocol, "Bob", "11", 150});
    for (int i = 0; i < 4; ++i) party.step(false);
    CHECK(party.ann.state() == GuestTable::State::Seated);
    CHECK(party.bob.state() == GuestTable::State::Seated);
    CHECK_EQ(party.room.guests(), 2);
    CHECK_EQ(party.ann_paid, 200);
    CHECK_EQ(party.bob_paid, 150);
    CHECK_EQ(party.room.table().people, 3);

    int checked = 0;
    for (int i = 0; i < 30000 && party.ann_hands < 150; ++i) {
        party.step();
        // A guest sees what the host's table shows from their seat, record of the hand included,
        // once the latest picture has arrived.
        const int seat = party.ann.seat();
        if (i % 37 == 0 && seat >= 0) {
            party.step(false);  // moves on their way reach the host...
            party.step(false);  // ...and the picture after them comes back
            const host::TableView theirs = party.ann.view();
            const host::TableView real = party.dealer.view(seat);
            CHECK_EQ(theirs.hand_number, real.hand_number);
            CHECK_EQ(theirs.pot, real.pot);
            CHECK_EQ(theirs.board.size(), real.board.size());
            CHECK_EQ(theirs.log.size(), real.log.size());
            if (!theirs.log.empty() && theirs.log.size() == real.log.size()) {
                CHECK(theirs.log.back().kind == real.log.back().kind);
                CHECK_EQ(theirs.log.back().amount, real.log.back().amount);
            }
            for (std::size_t s = 0; s < real.seats.size(); ++s) {
                CHECK_EQ(theirs.seats[s].stack, real.seats[s].stack);
                CHECK(theirs.seats[s].name == real.seats[s].name);
            }
            ++checked;
        }
        if (check::failures() > 10) return;
    }
    CHECK(checked > 30);
    if (party.ann_hands < 150) {
        std::printf("  ann hands %d, dealer hand %d, ann seat %d, state %d, stack %lld\n", party.ann_hands,
                    party.dealer.view(0).hand_number, party.ann.seat(), static_cast<int>(party.ann.state()),
                    static_cast<long long>(party.ann.stack()));
    }
    CHECK(party.ann_hands >= 150);
    CHECK(!party.leaked);
    CHECK(party.ann.notes().hands > 30);
    // Every hand they were dealt into came from a deck they could check.
    CHECK(party.ann.decks_checked() > 100);
    CHECK(party.bob.decks_checked() > 100);
    CHECK_EQ(party.ann.deck_alarms(), 0);
    CHECK_EQ(party.bob.deck_alarms(), 0);

    // Everybody gets up; not a chip appeared or vanished.
    party.ann.leave();
    party.bob.leave();
    party.dealer.leave(0);
    party.dealer.leave(1);
    for (int i = 0; i < 2000 && (party.ann.state() != GuestTable::State::Left ||
                                 party.bob.state() != GuestTable::State::Left || party.on_table() > 0);
         ++i) {
        party.step();
    }
    CHECK(party.ann.state() == GuestTable::State::Left);
    CHECK(party.bob.state() == GuestTable::State::Left);
    CHECK_EQ(party.on_table(), 0);
    CHECK_EQ(party.room.guests(), 0);
    CHECK_EQ(party.host_in + party.ann_paid + party.bob_paid, party.host_out + party.ann_took + party.bob_took);
}

TEST(a_guest_who_loses_the_connection_comes_back) {
    Party party;
    party.ann.join(online::Join{online::kProtocol, "Ann", "03", 200});
    for (int i = 0; i < 400; ++i) party.step();
    CHECK(party.ann.state() == GuestTable::State::Seated);
    const int seat = party.ann.seat();
    CHECK(seat >= 0);
    if (seat < 0) return;

    party.hub.set_online(party.ann_net.id(), false);
    for (int i = 0; i < 400; ++i) party.step();
    CHECK(!party.ann.host_online());
    CHECK(party.dealer.view(0).seats[static_cast<std::size_t>(seat)].offline);
    CHECK(!party.dealer.table().seat(seat).in_hand || party.dealer.view(0).stage != host::Stage::Playing);
    const int hand = party.dealer.view(0).hand_number;

    party.hub.set_online(party.ann_net.id(), true);
    for (int i = 0; i < 20; ++i) party.step(false);
    CHECK(party.ann.host_online());
    CHECK(!party.dealer.view(0).seats[static_cast<std::size_t>(seat)].offline);
    // The picture is whole again, the record of the table included.
    CHECK(party.ann.view().hand_number >= hand);
    CHECK_EQ(party.ann.view().log.size(), party.dealer.view(seat).log.size());
    for (int i = 0; i < 3000; ++i) party.step();
    CHECK(party.dealer.table().seat(seat).taken);
    CHECK(party.ann.view().hand_number > hand + 3);
}

TEST(a_full_table_makes_room_or_says_no) {
    Party party;
    // Seats 2 and 3 go to bots.
    party.dealer.sit(2, host::Player{"Bot2", "02", true}, 200);
    party.dealer.sit(3, host::Player{"Bot3", "02", true}, 200);
    party.host_in += 400;
    // No room to make: no.
    party.ann.join(online::Join{online::kProtocol, "Ann", "03", 200});
    for (int i = 0; i < 4; ++i) party.step();
    CHECK(party.ann.state() == GuestTable::State::Refused);
    CHECK(party.ann.refusal() == online::Refusal::Full);

    // The owner gets a bot up for the newcomer.
    party.room.make_room = [&]() {
        party.dealer.leave(3);
        return 3;
    };
    std::string seated;
    party.room.on_seated = [&](const net::PeerId&, const std::string& name) { seated = name; };
    GuestTable again(party.ann_net, party.host_net.id(), "Host");
    const net::PeerId host = party.host_net.id();
    party.ann_net.on_table = [&](const net::PeerId& from, online::Kind kind, net::Reader& body) {
        if (from == host) again.message(kind, body);
    };
    // The name of a bot or of somebody at the table is not taken twice.
    again.join(online::Join{online::kProtocol, "Bot1", "03", 200});
    for (int i = 0; i < 3000 && again.state() != GuestTable::State::Seated; ++i) party.step();
    CHECK(again.state() == GuestTable::State::Seated);
    CHECK_EQ(again.seat(), 3);
    CHECK(seated == "Bot1 2");

    // The wrong version, and a closed table.
    GuestTable old(party.bob_net, party.host_net.id(), "Host");
    party.bob_net.on_table = [&](const net::PeerId& from, online::Kind kind, net::Reader& body) {
        if (from == host) old.message(kind, body);
    };
    old.join(online::Join{online::kProtocol + 1, "Bob", "03", 200});
    for (int i = 0; i < 4; ++i) party.step();
    CHECK(old.state() == GuestTable::State::Refused);
    CHECK(old.refusal() == online::Refusal::Version);
    party.room.table().open = false;
    GuestTable shut(party.bob_net, party.host_net.id(), "Host");
    party.bob_net.on_table = [&](const net::PeerId& from, online::Kind kind, net::Reader& body) {
        if (from == host) shut.message(kind, body);
    };
    shut.join(online::Join{online::kProtocol, "Bob", "03", 200});
    for (int i = 0; i < 4; ++i) party.step();
    CHECK(shut.state() == GuestTable::State::Refused);
    CHECK(shut.refusal() == online::Refusal::Closed);
}

TEST(the_host_closes_the_table) {
    Party party;
    party.ann.join(online::Join{online::kProtocol, "Ann", "03", 200});
    for (int i = 0; i < 100; ++i) party.step();
    CHECK(party.ann.state() == GuestTable::State::Seated);
    party.room.close();
    for (int i = 0; i < 3; ++i) party.step(false);
    CHECK(party.ann.state() == GuestTable::State::Closed);
}

TEST(sha256_gives_the_known_digests) {
    const auto hex = [](const online::Digest& digest) {
        std::string text;
        char two[3];
        for (const std::uint8_t byte : digest) {
            std::snprintf(two, sizeof two, "%02x", byte);
            text += two;
        }
        return text;
    };
    CHECK(hex(online::Sha256().finish()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex(online::Sha256().add("abc").finish()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hex(online::Sha256().add("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").finish()) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // In pieces, the same.
    online::Sha256 pieces;
    for (const char c : std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) {
        pieces.add(std::string_view(&c, 1));
    }
    CHECK(hex(pieces.finish()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(a_fair_deck_is_a_whole_deck_and_depends_on_every_number) {
    const online::Digest secret = online::random_digest();
    online::Seeds seeds{{2, online::random_digest()}, {3, online::random_digest()}};
    const std::vector<game::Card> deck = online::fair_deck(secret, 7, seeds);
    CHECK_EQ(deck.size(), 52);
    std::vector<int> codes;
    for (const game::Card& card : deck) codes.push_back(static_cast<int>(card.rank) * 4 + static_cast<int>(card.suit));
    std::sort(codes.begin(), codes.end());
    CHECK(std::adjacent_find(codes.begin(), codes.end()) == codes.end());
    CHECK(online::fair_deck(secret, 7, seeds) == deck);
    CHECK(online::fair_deck(secret, 8, seeds) != deck);
    online::Seeds other = seeds;
    other[3][0] ^= 1;
    CHECK(online::fair_deck(secret, 7, other) != deck);
    CHECK(online::commitment(secret) != online::commitment(other[2]));
}

TEST(a_host_who_stacks_the_deck_is_caught) {
    Party party;
    // The host's program is changed: every deck is moved by one card after it was made.
    const host::Dealer::DeckSource fair = party.dealer.deck_source();
    party.dealer.set_deck_source([fair](int hand) {
        std::optional<std::vector<game::Card>> deck = fair(hand);
        if (deck) std::rotate(deck->begin(), deck->begin() + 1, deck->end());
        return deck;
    });
    party.ann.join(online::Join{online::kProtocol, "Ann", "03", 200});
    party.bob.join(online::Join{online::kProtocol, "Bob", "11", 150});
    for (int i = 0; i < 4000 && party.ann_hands < 20; ++i) party.step();
    CHECK(party.ann_hands >= 20);
    CHECK(party.ann.deck_alarms() > 10);
    CHECK(party.bob.deck_alarms() > 10);
    CHECK_EQ(party.ann.decks_checked(), 0);
}

TEST(a_host_who_leaves_out_a_guests_number_is_caught) {
    // Bots take their time, as at a real table.
    Party party(host::Pace{});
    // The host's program is changed: it throws away Ann's numbers and deals without them.
    const net::PeerId ann = party.ann_net.id();
    party.host_net.on_table = [&party, ann](const net::PeerId& from, online::Kind kind, net::Reader& body) {
        if (from == ann && kind == online::Kind::Seed) return;
        party.room.message(from, kind, body);
    };
    party.ann.join(online::Join{online::kProtocol, "Ann", "03", 200});
    party.bob.join(online::Join{online::kProtocol, "Bob", "11", 150});
    for (int i = 0; i < 20000 && party.ann.deck_alarms() < 3; ++i) party.step();
    CHECK(party.ann.deck_alarms() >= 3);
    // Bob's numbers went in: his decks are fine.
    CHECK(party.bob.decks_checked() >= 3);
    CHECK_EQ(party.bob.deck_alarms(), 0);
}

TEST(a_guest_who_gives_up_waiting_is_not_seated) {
    // Bots take their time, so the hand in progress lasts.
    Party party(host::Pace{});
    party.dealer.sit(2, host::Player{"Bot2", "02", true}, 200);
    party.dealer.sit(3, host::Player{"Bot3", "02", true}, 200);
    party.host_in += 400;
    party.room.make_room = [&]() {
        party.dealer.leave(3);
        return 3;
    };
    // The hand is on: the bot will get up only after it.
    for (int i = 0; i < 200 && party.dealer.view(0).stage != host::Stage::Playing; ++i) party.step(false);
    party.ann.join(online::Join{online::kProtocol, "Ann", "03", 200});
    party.step(false);
    party.step(false);
    CHECK(party.ann.state() == GuestTable::State::Joining);
    party.ann.leave();
    for (int i = 0; i < 4000; ++i) party.step();
    CHECK(!party.dealer.table().seat(3).taken);
    CHECK_EQ(party.room.guests(), 0);
    CHECK_EQ(party.ann_paid, 0);
}
