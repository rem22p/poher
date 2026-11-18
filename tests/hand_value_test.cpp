#include <array>

#include "check.hpp"
#include "game/hand_value.hpp"

using namespace poher::game;

namespace {

HandValue value(const char* text) {
    return evaluate(parse_cards(text));
}

}  // namespace

TEST(cards_text) {
    Card card;
    CHECK(parse_card("As", card) && card.rank == kAce && card.suit == Suit::Spades);
    CHECK(parse_card("Td", card) && card.rank == 10 && card.suit == Suit::Diamonds);
    CHECK(!parse_card("1s", card));
    CHECK(!parse_card("Ax", card));
    CHECK(to_text(Card{7, Suit::Clubs}) == "7c");
    CHECK(parse_cards("As Kd 7c").size() == 3);
    CHECK(parse_cards("As Kx").empty());
}

TEST(deck_has_every_card_once) {
    std::uint32_t state = 12345;
    const auto deck = shuffled_deck([&](std::uint32_t bound) {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) % bound;
    });
    CHECK_EQ(deck.size(), 52);
    int seen[4][15] = {};
    for (const Card card : deck) ++seen[static_cast<int>(card.suit)][card.rank];
    for (int suit = 0; suit < 4; ++suit) {
        for (int rank = 2; rank <= kAce; ++rank) CHECK_EQ(seen[suit][rank], 1);
    }
    const auto fresh = fresh_deck();
    CHECK(!std::equal(deck.begin(), deck.end(), fresh.begin()));
}

TEST(categories) {
    CHECK(value("As Kd 7c 5h 2s").category() == Category::HighCard);
    CHECK(value("As Ad 7c 5h 2s").category() == Category::Pair);
    CHECK(value("As Ad 7c 7h 2s").category() == Category::TwoPair);
    CHECK(value("As Ad Ac 7h 2s").category() == Category::Trips);
    CHECK(value("9s 8d 7c 6h 5s").category() == Category::Straight);
    CHECK(value("As Js 7s 5s 2s").category() == Category::Flush);
    CHECK(value("As Ad Ac 7h 7s").category() == Category::FullHouse);
    CHECK(value("As Ad Ac Ah 7s").category() == Category::Quads);
    CHECK(value("9s 8s 7s 6s 5s").category() == Category::StraightFlush);
}

TEST(category_order) {
    const char* weakest_first[] = {
        "As Kd 7c 5h 2s", "2s 2d 4c 5h 7s", "2s 2d 3c 3h 4s", "2s 2d 2c 4h 5s", "5s 4d 3c 2h As",
        "2s 3s 4s 5s 7s", "2s 2d 2c 3h 3s", "2s 2d 2c 2h 3s", "5s 4s 3s 2s As",
    };
    for (std::size_t i = 1; i < std::size(weakest_first); ++i) {
        CHECK(value(weakest_first[i - 1]) < value(weakest_first[i]));
    }
}

TEST(ace_plays_low_in_a_straight) {
    const HandValue wheel = value("As 2d 3c 4h 5s");
    CHECK(wheel.category() == Category::Straight);
    CHECK_EQ(wheel.ranks()[0], 5);
    CHECK(wheel < value("2d 3c 4h 5s 6s"));
    CHECK(value("As Ks Qs Js Ts").ranks()[0] == kAce);
    // Q-K-A-2-3 is not a straight.
    CHECK(value("Qs Kd Ac 2h 3s").category() == Category::HighCard);
}

TEST(kickers_decide) {
    CHECK(value("As Ad Kc 5h 2s") > value("Ah Ac Qc Jh 9s"));
    CHECK(value("As Ad Kc 5h 2s") > value("Ah Ac Kd 4h 3s"));
    CHECK(value("As Ad Kc 5h 2s") == value("Ah Ac Kd 5c 2d"));
    CHECK(value("Ks Kd 7c 7h As") > value("Kh Kc 7d 7s Qs"));
    CHECK(value("As Js 7s 5s 3s") > value("Ad Jd 7d 5d 2d"));
    CHECK(value("7s 7d 7c Ah Ks") > value("7h 6d 6c 6h As"));
}

TEST(best_five_of_seven) {
    // Three pairs: the top two play, with the best remaining card.
    const HandValue pairs = value("As Ad Kc Kh 7s 7d Qc");
    CHECK(pairs.category() == Category::TwoPair);
    CHECK_EQ(pairs.ranks()[0], kAce);
    CHECK_EQ(pairs.ranks()[1], kKing);
    CHECK_EQ(pairs.ranks()[2], kQueen);

    // Two sets of three make a full house.
    const HandValue full = value("9s 9d 9c 4h 4s 4d Ac");
    CHECK(full.category() == Category::FullHouse);
    CHECK_EQ(full.ranks()[0], 9);
    CHECK_EQ(full.ranks()[1], 4);

    // Six cards of a suit: the five highest count.
    const HandValue flush = value("As Ks 9s 7s 4s 2s Ad");
    CHECK(flush.category() == Category::Flush);
    CHECK_EQ(flush.ranks()[4], 4);

    // A straight flush hiding inside a higher plain flush.
    const HandValue hidden = value("As 9s 8s 7s 6s 5s Kd");
    CHECK(hidden.category() == Category::StraightFlush);
    CHECK_EQ(hidden.ranks()[0], 9);

    // A straight and a flush on different cards: the flush wins.
    CHECK(value("9s 8d 7s 6s 5d 2s Ks").category() == Category::Flush);

    // Quads with a pair on the side keep the best kicker.
    const HandValue quads = value("7s 7d 7c 7h Ks Kd 2c");
    CHECK(quads.category() == Category::Quads);
    CHECK_EQ(quads.ranks()[1], kKing);
}

// Every five-card hand there is. The number of hands of each kind is known
// from combinatorics, so any mistake in the evaluator shows up here.
TEST(all_five_card_hands) {
    const auto deck = fresh_deck();
    long long counts[9] = {};
    std::array<Card, 5> hand;
    for (int a = 0; a < 52; ++a) {
        hand[0] = deck[a];
        for (int b = a + 1; b < 52; ++b) {
            hand[1] = deck[b];
            for (int c = b + 1; c < 52; ++c) {
                hand[2] = deck[c];
                for (int d = c + 1; d < 52; ++d) {
                    hand[3] = deck[d];
                    for (int e = d + 1; e < 52; ++e) {
                        hand[4] = deck[e];
                        ++counts[static_cast<int>(evaluate(hand).category())];
                    }
                }
            }
        }
    }
    CHECK_EQ(counts[static_cast<int>(Category::StraightFlush)], 40);
    CHECK_EQ(counts[static_cast<int>(Category::Quads)], 624);
    CHECK_EQ(counts[static_cast<int>(Category::FullHouse)], 3744);
    CHECK_EQ(counts[static_cast<int>(Category::Flush)], 5108);
    CHECK_EQ(counts[static_cast<int>(Category::Straight)], 10200);
    CHECK_EQ(counts[static_cast<int>(Category::Trips)], 54912);
    CHECK_EQ(counts[static_cast<int>(Category::TwoPair)], 123552);
    CHECK_EQ(counts[static_cast<int>(Category::Pair)], 1098240);
    CHECK_EQ(counts[static_cast<int>(Category::HighCard)], 1302540);
}

// The same for all 133 784 560 seven-card hands, which is what the game
// actually evaluates: two hole cards and five on the board.
TEST(all_seven_card_hands) {
    const auto deck = fresh_deck();
    long long counts[9] = {};
    std::array<Card, 7> hand;
    for (int a = 0; a < 52; ++a) {
        hand[0] = deck[a];
        for (int b = a + 1; b < 52; ++b) {
            hand[1] = deck[b];
            for (int c = b + 1; c < 52; ++c) {
                hand[2] = deck[c];
                for (int d = c + 1; d < 52; ++d) {
                    hand[3] = deck[d];
                    for (int e = d + 1; e < 52; ++e) {
                        hand[4] = deck[e];
                        for (int f = e + 1; f < 52; ++f) {
                            hand[5] = deck[f];
                            for (int g = f + 1; g < 52; ++g) {
                                hand[6] = deck[g];
                                ++counts[static_cast<int>(evaluate(hand).category())];
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK_EQ(counts[static_cast<int>(Category::StraightFlush)], 41584);
    CHECK_EQ(counts[static_cast<int>(Category::Quads)], 224848);
    CHECK_EQ(counts[static_cast<int>(Category::FullHouse)], 3473184);
    CHECK_EQ(counts[static_cast<int>(Category::Flush)], 4047644);
    CHECK_EQ(counts[static_cast<int>(Category::Straight)], 6180020);
    CHECK_EQ(counts[static_cast<int>(Category::Trips)], 6461620);
    CHECK_EQ(counts[static_cast<int>(Category::TwoPair)], 31433400);
    CHECK_EQ(counts[static_cast<int>(Category::Pair)], 58627800);
    CHECK_EQ(counts[static_cast<int>(Category::HighCard)], 23294460);
}
