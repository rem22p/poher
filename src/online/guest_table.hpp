// A guest's side of a friend's table. The friend's computer deals and keeps
// the table; this side holds the latest picture of it, sends the player's
// moves, and keeps the record of the hand that the host sends in pieces.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "host/dealer.hpp"
#include "online/fair_deck.hpp"
#include "online/online.hpp"

namespace poher::online {

class GuestTable {
public:
    GuestTable(Online& online, const PeerId& host, std::string host_name);

    enum class State {
        Joining,  // asked for a seat
        Seated,
        Refused,  // see refusal()
        Closed,   // the host closed the table
        Left,     // got up; left_with() is what was taken away
    };

    void join(const Join& join);
    void message(Kind kind, net::Reader& body);
    void link(bool online);
    void tick(std::int64_t now);

    State state() const { return state_; }
    Refusal refusal() const { return refusal_; }
    const PeerId& host() const { return host_; }
    const std::string& host_name() const { return host_name_; }
    const TableInfo& table() const { return table_; }

    // The table as the player sees it, the clock run down to now.
    host::TableView view() const;
    std::uint64_t changes() const { return changes_; }
    int seat() const { return view_.viewer; }
    // Chips behind in the own seat now, bets in front not counted.
    Chips stack() const;

    bool act(game::Action action);
    void want_chips(Chips target, Chips most);
    void leave();
    void come_back();

    // What happened to the own seat since the last call: chips added, getting
    // up, the end of a hand. The same notices a Dealer gives about a seat.
    std::vector<host::Notice> take_notices();
    // What the host's table noted about the player's play.
    const bots::Notes& notes() const { return notes_; }

    // The fair deck: hands whose deck was checked, and those where it did not
    // fit what had been promised or what was seen on the table.
    int decks_checked() const { return decks_checked_; }
    int deck_alarms() const { return deck_alarms_; }

    bool host_online() const { return host_online_; }
    // Since when the host has been out of reach; meaningful while !host_online().
    std::int64_t lost_since() const { return lost_since_; }

private:
    void take(Snapshot snapshot);
    void promised(const Promise& promise);
    void revealed(const Reveal& reveal);
    void send_seed(int hand);
    void note_cards(const host::TableView& view);
    bool cards_fit(int hand) const;

    // One hand of the fair deck, from the promise to the check.
    struct Round {
        Digest promise{};
        Digest seed{};
        std::int64_t sent_at = -1;   // when our number went, by our clock
        std::int64_t seen_at = -1;   // when the hand first showed on the table
        std::map<int, std::array<game::Card, 2>> cards;  // face up for us, by seat
        std::vector<game::Card> board;
        std::optional<Dealt> dealt;  // after the reveal
        bool promised = false;
        bool excused = false;        // the connection broke: a missing number is no proof
    };
    std::map<int, Round> rounds_;  // by hand number, the last few only
    int decks_checked_ = 0;
    int deck_alarms_ = 0;

    Online& online_;
    PeerId host_;
    std::string host_name_;
    State state_ = State::Joining;
    Refusal refusal_ = Refusal::Full;
    TableInfo table_;
    host::TableView view_;
    std::deque<host::LogLine> log_;
    std::int64_t received_at_ = 0;   // when the latest picture came, by our clock
    std::int64_t now_ = 0;
    std::uint64_t changes_ = 1;
    Chips added_ = 0;                // as the host counts it
    Chips told_added_ = 0;           // as passed on in notices
    Chips left_with_ = -1;
    bool told_left_ = false;
    int told_hand_ = 0;              // the last hand whose end was passed on
    bots::Notes notes_;
    bool host_online_ = true;
    std::int64_t lost_since_ = 0;
    std::vector<host::Notice> notices_;
};

}  // namespace poher::online
