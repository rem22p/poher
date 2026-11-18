// Pieces every menu screen is made of: the bars at the top and the bottom,
// a line that lights up when picked, a value with arrows.
#pragma once

#include <string_view>

#include "game/table.hpp"
#include "ui/canvas.hpp"
#include "ui/card_art.hpp"
#include "ui/color.hpp"
#include "ui/lang.hpp"

namespace poher::app::parts {

using game::Chips;
using ui::Canvas;
namespace color = ui::color;

constexpr int kColumn = 66;         // the width of a menu column
constexpr int kFlashTime = 4000;    // how long a short message stays, in milliseconds

// The bars at the top and at the bottom of every menu screen.
inline void draw_bars(Canvas& canvas, const ui::Words& w, Chips bankroll, std::string_view keys) {
    const int width = canvas.width();
    canvas.fill(0, 0, width, 1, color::kBar);
    const game::Suit suits[] = {game::Suit::Spades, game::Suit::Hearts, game::Suit::Diamonds, game::Suit::Clubs};
    for (int i = 0; i < 4; ++i) {
        canvas.put(1 + i, 0, ui::suit_sign(suits[i]), ui::suit_colour_on_dark(suits[i]), color::kBar);
    }
    canvas.put(6, 0, "POHER", color::kWhite, color::kBar, true);
    canvas.put_right(width - 2, 0, std::string(w.bankroll) + " " + ui::chips_text(bankroll), color::kText,
                     color::kBar);
    canvas.fill(0, canvas.height() - 1, width, 1, color::kBar);
    canvas.put(2, canvas.height() - 1, keys, color::kDim, color::kBar);
}

// One line of a menu: lit up when it is the picked one.
inline void draw_row(Canvas& canvas, int x, int y, int width, bool picked) {
    canvas.fill(x, y, width, 1, picked ? color::kSeatActive : color::kBackground);
    if (picked) canvas.put(x, y, "▌", color::kYellow, color::kSeatActive);
}

// "◀ value ▶" with the arrows greyed out at the ends of the range.
inline void draw_choice(Canvas& canvas, int x, int y, std::string_view value, bool picked, bool less, bool more) {
    const ui::Rgb back = picked ? color::kSeatActive : color::kBackground;
    const ui::Rgb arrow = picked ? color::kYellow : color::kDim;
    canvas.put(x, y, "◀", less ? arrow : color::kRule, back);
    const int used = canvas.put(x + 2, y, value, picked ? color::kWhite : color::kText, back, picked);
    canvas.put(x + 3 + used, y, "▶", more ? arrow : color::kRule, back);
}

}  // namespace poher::app::parts
