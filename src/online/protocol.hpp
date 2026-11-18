// The messages friends' programs send each other, and how they are packed.
// Every message is one Kind byte and a body. Bodies are read with net::Reader,
// which never trusts what came from the network.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bots/notes.hpp"
#include "online/fair_deck.hpp"
#include "host/view.hpp"
#include "net/wire.hpp"

namespace poher::online {

using game::Chips;

// Raised whenever a message changes so that older programs cannot read it.
inline constexpr int kProtocol = 1;

enum class Kind : std::uint8_t {
    // Between friends.
    Status = 1,     // what the sender is doing; sent to every friend on a change
    Invite = 2,     // "come to my table"
    // A table: from a guest to the host...
    Join = 10,      // a seat, please
    Resume = 11,    // the connection is back: go on with my seat
    Act = 12,       // my move
    WantChips = 13, // add chips to my stack
    Leave = 14,     // I get up
    Back = 15,      // I was away and play on
    Seed = 16,      // my number for the deck of a hand
    // ...and from the host to a guest.
    Refused = 20,   // no seat
    Snapshot = 21,  // the table as the guest sees it
    Closed = 22,    // the table is no more
    Promise = 23,   // the commitment to the secret of a coming hand
    Reveal = 24,    // the secret of a hand that is over, and what made its deck
};

// A table as friends see it in the list and in an invitation.
struct TableInfo {
    std::uint8_t level = 0;   // which of host::kLevels
    std::uint8_t seats = 0;
    std::uint8_t people = 0;  // seats taken by people
    std::uint8_t bots = 0;    // and by bots
    bool open = false;        // friends may sit down without an invitation
    std::uint8_t turn_seconds = 0;  // 0 = no clock on moves
};

// What a player is doing right now.
enum class Activity : std::uint8_t {
    Menu,      // in the menus, free
    WithBots,  // at a table of their own that friends cannot join
    Hosting,   // at a table of their own that friends can join
    Guest,     // at a friend's table
};

// The numbers of a profile that friends see (SPEC 6.2).
struct PublicStats {
    std::int64_t hands = 0;
    std::int64_t hands_won = 0;
    Chips biggest_pot = 0;
    Chips bankroll = 0;
    std::int64_t refills = 0;
    Chips from_bots = 0;
    Chips from_people = 0;
};

struct Status {
    int protocol = kProtocol;
    std::string version;      // of the program, for "please update"
    Activity activity = Activity::Menu;
    TableInfo table;          // for Hosting
    std::string avatar;
    PublicStats stats;
};

enum class Refusal : std::uint8_t {
    Full,      // no free seat and no bot to make room
    Closed,    // only by invitation
    Version,   // the programs cannot talk to each other
    NoTable,   // the host has no table now
    Bankroll,  // the guest cannot afford the buy-in
};

struct Join {
    int protocol = kProtocol;
    std::string name;
    std::string avatar;
    Chips buy_in = 0;
};

// The table as one guest sees it. The log carries only lines the guest has
// not had yet, unless `log_reset` says it is the whole log.
struct Snapshot {
    host::TableView view;
    bool log_reset = false;
    TableInfo table;
    Chips added = 0;       // all chips put on the guest's seat, the buy-in included
    Chips left_with = -1;  // the guest got up with these chips; -1 while seated
    bots::Notes notes;     // what the table noted about the guest's play
};

// The fair deck (SPEC 7.3): a promise before a hand, each guest's number,
// and after the hand the secret with all that went into the deck.
struct Promise {
    int hand = 0;
    Digest promise{};
};
struct Seed {
    int hand = 0;
    Digest seed{};
};
struct Reveal {
    int hand = 0;
    Digest secret{};
    int button = -1;
    std::vector<int> seats;  // dealt in, in the order of the table
    Seeds seeds;             // the guests' numbers that went into the deck, by seat
};

// Each returns false if the body is broken. A body read from the network
// is accepted only if it is whole and nothing is left over.
void write(net::Writer& out, const Status& status);
bool read(net::Reader& in, Status& status);
void write(net::Writer& out, const TableInfo& table);
bool read(net::Reader& in, TableInfo& table);
void write(net::Writer& out, const Join& join);
bool read(net::Reader& in, Join& join);
void write(net::Writer& out, const Snapshot& snapshot);
bool read(net::Reader& in, Snapshot& snapshot);
void write(net::Writer& out, const Promise& promise);
bool read(net::Reader& in, Promise& promise);
void write(net::Writer& out, const Seed& seed);
bool read(net::Reader& in, Seed& seed);
void write(net::Writer& out, const Reveal& reveal);
bool read(net::Reader& in, Reveal& reveal);
void write(net::Writer& out, const game::Action& action, int hand_number);
bool read(net::Reader& in, game::Action& action, int& hand_number);

// A message: the kind byte and the body.
std::vector<std::uint8_t> message(Kind kind, const net::Writer& body);

}  // namespace poher::online
