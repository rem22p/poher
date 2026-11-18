#include "game/cards.hpp"

#include <utility>

namespace poher::game {
namespace {

constexpr std::string_view kRanks = "23456789TJQKA";
constexpr std::string_view kSuits = "shdc";

}  // namespace

std::string to_text(Card card) {
    if (card.rank < 2 || card.rank > kAce) return "??";
    return {kRanks[card.rank - 2], kSuits[static_cast<std::size_t>(card.suit)]};
}

bool parse_card(std::string_view text, Card& card) {
    if (text.size() != 2) return false;
    const std::size_t rank = kRanks.find(text[0]);
    const std::size_t suit = kSuits.find(text[1]);
    if (rank == std::string_view::npos || suit == std::string_view::npos) return false;
    card.rank = static_cast<std::uint8_t>(rank + 2);
    card.suit = static_cast<Suit>(suit);
    return true;
}

std::vector<Card> parse_cards(std::string_view text) {
    std::vector<Card> cards;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == ' ') {
            ++at;
            continue;
        }
        Card card;
        if (!parse_card(text.substr(at, 2), card)) return {};
        cards.push_back(card);
        at += 2;
    }
    return cards;
}

std::array<Card, kDeckSize> fresh_deck() {
    std::array<Card, kDeckSize> deck;
    std::size_t at = 0;
    for (int suit = 0; suit < 4; ++suit) {
        for (int rank = 2; rank <= kAce; ++rank) {
            deck[at++] = Card{static_cast<std::uint8_t>(rank), static_cast<Suit>(suit)};
        }
    }
    return deck;
}

std::vector<Card> shuffled_deck(const std::function<std::uint32_t(std::uint32_t bound)>& random) {
    const auto fresh = fresh_deck();
    std::vector<Card> deck(fresh.begin(), fresh.end());
    // Fisher-Yates: every order of the deck comes out equally often.
    for (std::uint32_t left = kDeckSize; left > 1; --left) {
        std::swap(deck[left - 1], deck[random(left)]);
    }
    return deck;
}

}  // namespace poher::game
