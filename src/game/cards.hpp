// Playing cards and the deck.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace poher::game {

enum class Suit : std::uint8_t { Spades, Hearts, Diamonds, Clubs };

inline constexpr int kJack = 11;
inline constexpr int kQueen = 12;
inline constexpr int kKing = 13;
inline constexpr int kAce = 14;

struct Card {
    std::uint8_t rank = 0;  // 2..10, then kJack..kAce
    Suit suit = Suit::Spades;

    friend bool operator==(const Card&, const Card&) = default;
};

// Two letters, the way hands are written down: "As", "Td", "7c".
std::string to_text(Card card);
// The reverse; false if the text is not a card.
bool parse_card(std::string_view text, Card& card);
// Cards separated by spaces: "As Kd 7c". Wrong text gives an empty list.
std::vector<Card> parse_cards(std::string_view text);

inline constexpr int kDeckSize = 52;

// All 52 cards in a fixed order: by suit, twos first.
std::array<Card, kDeckSize> fresh_deck();

// Shuffles a fresh deck. `random` returns a number from 0 to bound-1, each
// equally likely; where it gets them from decides how fair the shuffle is.
std::vector<Card> shuffled_deck(const std::function<std::uint32_t(std::uint32_t bound)>& random);

}  // namespace poher::game
