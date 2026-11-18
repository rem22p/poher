// The network as the game sees it: friends, who is online, and small messages
// between them. Nothing here knows about Tox; the game talks only to this
// interface, so the network underneath can be replaced without touching it.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace poher::net {

// Permanent identity of a player in the network: their public key.
using PeerId = std::array<std::uint8_t, 32>;

using Bytes = std::span<const std::uint8_t>;

// How we are connected to the network or to a friend.
enum class Link {
    None,    // not connected
    Relay,   // through a relay node (TCP): works behind strict routers and VPNs, but slower
    Direct,  // straight to the other computer (UDP)
};

enum class AddResult { Ok, AlreadyAdded, BadCode, OwnCode, Failed };

struct Friend {
    PeerId id;
    std::string name;
    Link link;
};

// What the network reports back. All of these are called from inside
// Network::poll(), on the thread that called it.
struct Events {
    std::function<void(Link)> self_link;                                         // our own connection changed
    std::function<void(const PeerId&, std::string_view hello)> friend_request;   // someone entered our code
    std::function<void(const PeerId&, Link)> friend_link;
    std::function<void(const PeerId&, std::string_view name)> friend_name;
    std::function<void(const PeerId&, Bytes)> message;    // sent with send(): always arrives, in order
    std::function<void(const PeerId&, Bytes)> datagram;   // sent with send_datagram(): may get lost
};

// The largest message send() and send_datagram() accept, in bytes.
inline constexpr std::size_t kMaxMessage = 1200;

class Network {
public:
    virtual ~Network() = default;

    virtual PeerId id() const = 0;
    // The text a friend types in to add us.
    virtual std::string code() const = 0;
    virtual void set_name(std::string_view name) = 0;
    virtual std::string name() const = 0;
    virtual Link link() const = 0;

    // `hello` is shown to the other side together with the request.
    virtual AddResult add_friend(std::string_view code, std::string_view hello) = 0;
    // Answers a friend_request event.
    virtual bool accept_friend(const PeerId& id) = 0;
    virtual std::vector<Friend> friends() const = 0;

    // Both return false if the friend is offline or the message is too long.
    virtual bool send(const PeerId& to, Bytes data) = 0;
    virtual bool send_datagram(const PeerId& to, Bytes data) = 0;

    // Does the pending network work and calls Events. Call it in a loop; the
    // result is how long the caller may sleep before the next call.
    virtual std::chrono::milliseconds poll() = 0;

    // Identity and friend list. Write it to a file and pass it back on the
    // next start, otherwise the player gets a new code and loses their friends.
    virtual std::vector<std::uint8_t> save() const = 0;
};

inline std::string to_hex(Bytes data) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::uint8_t byte : data) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

// First 8 digits of the key: enough to tell friends apart in a log.
inline std::string short_id(const PeerId& id) {
    return to_hex(Bytes(id).first(4));
}

}  // namespace poher::net
