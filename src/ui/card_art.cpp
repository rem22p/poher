#include "ui/card_art.hpp"

#include <string>

namespace poher::ui {

namespace {

using Glyph = std::array<std::string_view, 7>;

// Ranks as 5 x 7 dots, from the two to the ace.
constexpr Glyph kRanks[] = {
    {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"},  // 2
    {".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."},  // 3
    {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."},  // 4
    {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."},  // 5
    {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."},  // 6
    {"#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.."},  // 7
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},  // 8
    {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."},  // 9
    {"#.###", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.#.#", "#.###"},  // 10
    {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."},  // J
    {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"},  // Q
    {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"},  // K
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},  // A
};

constexpr Glyph kLogo[] = {
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."},  // P
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."},  // O
    {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},  // H
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"},  // E
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"},  // R
};

const Glyph& rank_glyph(int rank) {
    return kRanks[rank >= 2 && rank <= game::kAce ? rank - 2 : 0];
}

std::string rank_text(int rank) {
    switch (rank) {
        case game::kAce: return "A";
        case game::kKing: return "K";
        case game::kQueen: return "Q";
        case game::kJack: return "J";
        default: return std::to_string(rank);
    }
}

}  // namespace

Size card_size(int scale) {
    return scale == 1 ? Size{7, 5} : Size{12, 9};
}

Size backs_size(int scale) {
    return scale == 1 ? Size{7, 2} : Size{14, 4};
}

Size shown_size(int scale) {
    return scale == 1 ? Size{7, 2} : Size{15, 5};
}

std::string_view suit_sign(game::Suit suit) {
    switch (suit) {
        case game::Suit::Spades: return "♠";
        case game::Suit::Hearts: return "♥";
        case game::Suit::Diamonds: return "♦";
        case game::Suit::Clubs: return "♣";
    }
    return "?";
}

Rgb suit_colour(game::Suit suit) {
    switch (suit) {
        case game::Suit::Spades: return Rgb{24, 25, 31};
        case game::Suit::Hearts: return Rgb{208, 36, 50};
        case game::Suit::Diamonds: return Rgb{30, 96, 216};
        case game::Suit::Clubs: return Rgb{16, 132, 62};
    }
    return color::kText;
}

Rgb suit_colour_on_dark(game::Suit suit) {
    switch (suit) {
        case game::Suit::Spades: return Rgb{200, 204, 212};
        case game::Suit::Hearts: return Rgb{232, 70, 82};
        case game::Suit::Diamonds: return Rgb{84, 146, 244};
        case game::Suit::Clubs: return Rgb{64, 190, 110};
    }
    return color::kText;
}

void draw_card(Canvas& canvas, int x, int y, game::Card card, int scale, bool shadow) {
    const Size size = card_size(scale);
    const Rgb ink = suit_colour(card.suit);
    canvas.fill(x, y, size.w, size.h, color::kCard);
    canvas.stamp(x, y, rank_glyph(card.rank), ink, color::kCard, scale, 1, 1);
    const std::string_view sign = suit_sign(card.suit);
    if (scale == 1) {
        canvas.put(x + 3, y + size.h - 1, sign, ink, color::kCard, true);
    } else {
        canvas.put(x + 1, y + size.h - 1, sign, ink, color::kCard, true);
        canvas.put(x + size.w - 2, y + size.h - 1, sign, ink, color::kCard, true);
    }
    if (shadow) {
        canvas.put(x + size.w, y, "▄", color::kShadow);
        canvas.fill(x + size.w, y + 1, 1, size.h - 1, color::kShadow);
        for (int i = 0; i < size.w; ++i) canvas.put(x + 1 + i, y + size.h, "▀", color::kShadow);
    }
}

void draw_slot(Canvas& canvas, int x, int y, int scale) {
    const Size size = card_size(scale);
    canvas.fill(x, y, size.w, size.h, color::kSlot);
}

void draw_backs(Canvas& canvas, int x, int y, int scale) {
    const int w = scale == 1 ? 3 : 6;
    const int h = scale == 1 ? 2 : 4;
    const int gap = scale == 1 ? 1 : 2;
    for (const int dx : {0, w + gap}) {
        for (int row = 0; row < h; ++row) {
            for (int column = 0; column < w; ++column) {
                canvas.put(x + dx + column, y + row, "▒", color::kBackLight, color::kBack);
            }
        }
    }
}

void draw_shown(Canvas& canvas, int x, int y, const std::array<game::Card, 2>& cards, int scale) {
    if (scale != 1) {
        // Big window: the small card with its block rank.
        draw_card(canvas, x, y, cards[0], 1, false);
        draw_card(canvas, x + 8, y, cards[1], 1, false);
        return;
    }
    // Small window: the rank as a letter over the suit sign.
    for (int i = 0; i < 2; ++i) {
        const game::Card card = cards[static_cast<std::size_t>(i)];
        const int cx = x + i * 4;
        const Rgb ink = suit_colour(card.suit);
        canvas.fill(cx, y, 3, 2, color::kCard);
        const std::string rank = rank_text(card.rank);
        canvas.put(cx + (rank.size() > 1 ? 0 : 1), y, rank, ink, color::kCard, true);
        canvas.put(cx + 1, y + 1, suit_sign(card.suit), ink, color::kCard, true);
    }
}

int logo_width(int scale) {
    return 5 * 5 * scale + 4 * scale;
}

int draw_logo(Canvas& canvas, int x, int y, int scale) {
    const game::Suit suits[] = {game::Suit::Spades, game::Suit::Hearts, game::Suit::Diamonds, game::Suit::Clubs,
                                game::Suit::Spades};
    for (int i = 0; i < 5; ++i) {
        const int lx = x + i * 6 * scale;
        canvas.stamp(lx, y, kLogo[i], suit_colour_on_dark(suits[i]), color::kBackground, scale);
    }
    return logo_width(scale);
}

}  // namespace poher::ui
