// The Tox implementation of net::Network. The only header that mentions Tox.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "net/network.hpp"

namespace poher::net {

struct ToxConfig {
    std::vector<std::uint8_t> saved;  // result of an earlier Network::save(); empty = new identity
    bool udp = true;                  // false: only relay nodes, as if UDP were blocked
    bool lan = true;                  // find friends on the same local network without the internet
};

// Returns nullptr and fills `error` if the network could not be started.
std::unique_ptr<Network> make_tox_network(const ToxConfig& config, Events events, std::string& error);

// Version of the Tox library the program was built with, like "0.2.24".
std::string tox_library_version();

}  // namespace poher::net
