#include "online/fair_deck.hpp"

#include <algorithm>
#include <random>

namespace poher::online {

Digest commitment(const Digest& secret) {
    return Sha256().add("POHER commitment").add(secret).finish();
}

std::vector<game::Card> fair_deck(const Digest& secret, int hand_number, const Seeds& seeds) {
    Sha256 mix;
    mix.add("POHER deck").add(secret).add_number(static_cast<std::uint64_t>(hand_number));
    for (const auto& [seat, seed] : seeds) {
        mix.add_number(static_cast<std::uint64_t>(seat));
        mix.add(seed);
    }
    const Digest root = mix.finish();
    // Numbers for the shuffle: block after block of hashes of the root and a counter.
    std::uint64_t counter = 0;
    Digest block{};
    std::size_t used = block.size();
    const auto next32 = [&]() {
        if (used + 4 > block.size()) {
            block = Sha256().add(root).add_number(counter++).finish();
            used = 0;
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value = value << 8 | block[used++];
        return value;
    };
    return game::shuffled_deck([&](std::uint32_t bound) {
        // Throw away the few values that would make the low numbers more frequent.
        const std::uint64_t limit = (std::uint64_t{1} << 32) / bound * bound;
        std::uint64_t value;
        do {
            value = next32();
        } while (value >= limit);
        return static_cast<std::uint32_t>(value % bound);
    });
}

Digest random_digest() {
    std::random_device device;
    Digest digest;
    for (std::size_t i = 0; i < digest.size(); i += 4) {
        const std::uint32_t value = device();
        for (std::size_t j = 0; j < 4; ++j) digest[i + j] = static_cast<std::uint8_t>(value >> (8 * j));
    }
    return digest;
}

std::optional<Dealt> deal_out(const std::vector<game::Card>& deck, const std::vector<int>& seats, int button) {
    const std::size_t count = seats.size();
    if (count < 2 || deck.size() < 2 * count + 5) return std::nullopt;
    const auto at = std::find(seats.begin(), seats.end(), button);
    if (at == seats.end()) return std::nullopt;
    const std::size_t button_index = static_cast<std::size_t>(at - seats.begin());
    Dealt dealt;
    std::size_t next = 0;
    for (std::size_t round = 0; round < 2; ++round) {
        std::size_t index = button_index;
        for (std::size_t i = 0; i < count; ++i) {
            index = (index + 1) % count;
            dealt.holes[seats[index]][round] = deck[next++];
        }
    }
    dealt.board.assign(deck.begin() + static_cast<std::ptrdiff_t>(next), deck.begin() + static_cast<std::ptrdiff_t>(next + 5));
    return dealt;
}

}  // namespace poher::online
