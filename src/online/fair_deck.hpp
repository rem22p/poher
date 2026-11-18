// A deck that neither the host nor any player can choose (SPEC 7.3).
//
// Before a hand the host picks a secret and sends everybody its commitment:
// a hash that fixes the secret without telling it. Each guest answers with a
// random number of their own. The deck is made from the secret and all the
// numbers together. After the hand the host tells the secret and the
// numbers, and every guest checks that the commitment fits, that their own
// number was used, and that the cards they saw are where that deck put them.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <vector>

#include "game/cards.hpp"
#include "online/sha256.hpp"

namespace poher::online {

// What went into the deck of one hand: the guests' numbers by seat.
using Seeds = std::map<int, Digest>;

Digest commitment(const Digest& secret);
std::vector<game::Card> fair_deck(const Digest& secret, int hand_number, const Seeds& seeds);
// 32 bytes from the system's source of randomness.
Digest random_digest();

// Where a deck puts the cards when these seats are dealt in, in the way
// game::Hand deals: one card at a time from the left of the button, twice
// around, then the board.
struct Dealt {
    std::map<int, std::array<game::Card, 2>> holes;  // by seat
    std::vector<game::Card> board;                   // all five
};
std::optional<Dealt> deal_out(const std::vector<game::Card>& deck, const std::vector<int>& seats, int button);

}  // namespace poher::online
