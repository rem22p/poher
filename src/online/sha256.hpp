// SHA-256, for the fair deck: a commitment that cannot be taken back and a
// deck nobody alone could choose. Small and slow enough to read; it hashes a
// few hundred bytes per hand.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace poher::online {

using Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256();
    Sha256& add(std::span<const std::uint8_t> bytes);
    Sha256& add(std::string_view text);
    Sha256& add_number(std::uint64_t value);  // eight bytes, high first
    Digest finish();

private:
    void block(const std::uint8_t* data);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;  // bytes added
};

}  // namespace poher::online
