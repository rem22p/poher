// A network inside one program: several players joined through a Hub, with
// nothing sent anywhere. Tests play whole network games through it, and it
// shows what the game expects from a real network.
#pragma once

#include <memory>
#include <string>

#include "net/network.hpp"

namespace poher::net {

class Hub {
public:
    Hub();
    ~Hub();

    // A new player. Their code is the hex of their id.
    std::unique_ptr<Network> join(std::string name, Events events);
    // Makes two players friends at once, without the request and the answer.
    void befriend(const PeerId& one, const PeerId& other);
    // Pulls the cable of a player, or puts it back.
    void set_online(const PeerId& who, bool online);
    // Messages sent from now on get lost on the way to this player (the link stays up).
    void set_deaf(const PeerId& who, bool deaf);

    struct Shared;

private:
    std::shared_ptr<Shared> shared_;
};

}  // namespace poher::net
