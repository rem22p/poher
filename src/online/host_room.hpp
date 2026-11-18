// The host's side of a table that friends sit at. The table itself is the
// host's Dealer; the room seats the friends who ask, passes on their moves,
// and sends each of them the table as they may see it.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "host/dealer.hpp"
#include "online/fair_deck.hpp"
#include "online/online.hpp"

namespace poher::online {

class HostRoom {
public:
    HostRoom(host::Dealer& dealer, Online& online, TableInfo table);
    ~HostRoom();

    // Asked when a friend wants a seat and none is free: the owner may get a
    // bot up and return its seat (free once the hand is over), or return -1.
    std::function<int()> make_room;
    // Called when a friend sits down, so that the bots can remember them.
    std::function<void(const PeerId& who, const std::string& name)> on_seated;

    TableInfo& table() { return table_; }
    void invited(const PeerId& who) { invited_.insert(who); }

    void message(const PeerId& from, Kind kind, net::Reader& body);
    void link(const PeerId& who, bool online);
    // Call after the dealer's tick: seats those who wait and sends fresh pictures.
    void tick();
    // A notice of the dealer. True if it was about the seat of a friend.
    bool notice(const host::Notice& notice);
    // Tells everybody that the table is no more.
    void close();

    bool guest_seat(int seat) const;
    int guests() const;
    // Who sits where, for the history of hands: the friend in a seat or nothing.
    std::optional<PeerId> guest_in(int seat) const;

private:
    struct Guest {
        PeerId id{};
        std::string name;
        std::string avatar;
        Chips buy_in = 0;
        int seat = -1;            // -1 while waiting for a seat to empty
        int wanted_seat = -1;     // the seat a bot is giving up for them
        Chips added = 0;
        Chips left_with = -1;     // got up; the record goes once they have heard it
        bool online = true;
        std::uint64_t sent = 0;   // dealer changes when the last picture was sent
        bool fresh = true;        // has to get a picture even if the table did not change
        std::int64_t log_sent = -1;
        int promised = 0;         // the hand whose promise they were sent last
    };

    // The deck of one hand (SPEC 7.3).
    struct Round {
        int hand = 0;
        Digest secret{};
        Digest promise{};
        Seeds seeds;
        std::int64_t asked_at = -1;  // when the dealer first wanted this deck
        std::vector<game::Card> deck;
        bool revealed = false;
    };
    std::optional<std::vector<game::Card>> deck_for(int hand);
    void promise(int hand);
    void send_promise(Guest& guest);
    void reveal();

    void join(const PeerId& from, const Join& join);
    void refuse(const PeerId& to, Refusal why);
    bool send_picture(Guest& guest);
    std::string unique_name(std::string name) const;
    Guest* find(const PeerId& id);
    Guest* at_seat(int seat);

    host::Dealer& dealer_;
    Online& online_;
    TableInfo table_;
    std::set<PeerId> invited_;
    std::vector<Guest> guests_;
    Round next_;   // promised, not dealt yet
    Round dealt_;  // the hand in play or just over
};

// How long a hand waits for the guests' numbers before it is dealt without them.
inline constexpr std::int64_t kSeedWaitMs = 5000;

}  // namespace poher::online
