// The player in the network: friends, what each of them is doing, friend
// requests, invitations, and the messages of a table. It owns the network and
// cuts long messages into pieces that fit and glues them back together.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "net/network.hpp"
#include "online/protocol.hpp"

namespace poher::online {

using net::PeerId;

// A friend as the lists show them.
struct Mate {
    PeerId id{};
    std::string name;
    net::Link link = net::Link::None;
    std::optional<Status> status;  // what they said last; nothing until they say something
};

struct Request {
    PeerId id{};
    std::string hello;  // the text that came with it
};

struct Invitation {
    PeerId from{};
    std::string name;
    TableInfo table;
};

class Online {
public:
    // Starts the network. `factory` gets the events to report; it returns
    // nothing if the network cannot start, and `error()` says why.
    using Factory = std::function<std::unique_ptr<net::Network>(net::Events, std::string& error)>;
    explicit Online(const Factory& factory);
    ~Online();

    bool started() const { return network_ != nullptr; }
    const std::string& error() const { return error_; }

    // Lets the network do its work. Call it often.
    void poll();

    std::string code() const;
    net::Link link() const;
    void set_name(const std::string& name);
    PeerId id() const;

    net::AddResult add(const std::string& code);
    void accept(const PeerId& id);
    void decline(const PeerId& id);
    const std::vector<Request>& requests() const { return requests_; }
    std::vector<Mate> mates() const;
    std::optional<Mate> mate(const PeerId& id) const;

    // What this player is doing. Every friend hears it now and whenever they come online.
    void set_status(const Status& status);

    void invite(const PeerId& to, const TableInfo& table);
    // The newest invitation not yet answered.
    std::optional<Invitation> invitation() const;
    void drop_invitation();

    // A message for a table: to the host from a guest or back.
    bool send(const PeerId& to, Kind kind, const net::Writer& body);
    std::function<void(const PeerId& from, Kind kind, net::Reader& body)> on_table;
    std::function<void(const PeerId& who, bool online)> on_link;

    // The identity and the friend list, to be kept in a file. True once
    // something worth keeping has changed since the last call.
    std::vector<std::uint8_t> save() const;
    bool take_changed() { return std::exchange(changed_, false); }

private:
    struct Incoming {
        std::uint8_t number = 0;  // which message the pieces belong to
        std::vector<std::uint8_t> bytes;
        bool open = false;
    };

    void received(const PeerId& from, net::Bytes bytes);
    void handle(const PeerId& from, net::Bytes message);
    void tell_status(const PeerId& to);

    std::unique_ptr<net::Network> network_;
    std::string error_;
    std::map<PeerId, Incoming> incoming_;
    std::map<PeerId, std::uint8_t> numbers_;  // the number of the last message sent to each friend
    std::map<PeerId, Status> statuses_;
    std::map<PeerId, net::Link> links_;
    std::vector<Request> requests_;
    std::vector<Invitation> invitations_;
    std::optional<Status> status_;
    bool changed_ = false;
};

}  // namespace poher::online
