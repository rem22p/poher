// How cards look. A card comes in two sizes: 7 x 5 cells (scale 1) and
// 12 x 9 cells (scale 2). The rank is drawn with blocks nearly as big as the
// card, in the colour of the suit; the suit sign sits under it.
#pragma once

#include <array>
#include <string_view>

#include "game/cards.hpp"
#include "ui/canvas.hpp"

namespace poher::ui {

struct Size {
    int w = 0;
    int h = 0;
};

Size card_size(int scale);
// Two face-down cards side by side.
Size backs_size(int scale);
// Two face-up cards of another player, smaller than the cards of the board.
Size shown_size(int scale);

std::string_view suit_sign(game::Suit suit);
// The colour of a suit on a white card and on a dark background.
Rgb suit_colour(game::Suit suit);
Rgb suit_colour_on_dark(game::Suit suit);

void draw_card(Canvas& canvas, int x, int y, game::Card card, int scale, bool shadow);
// The place of a board card that has not come yet.
void draw_slot(Canvas& canvas, int x, int y, int scale);
void draw_backs(Canvas& canvas, int x, int y, int scale);
void draw_shown(Canvas& canvas, int x, int y, const std::array<game::Card, 2>& cards, int scale);

// The word POHER in big letters, 10 x 7 cells each at scale 2. Returns the width.
int draw_logo(Canvas& canvas, int x, int y, int scale);
int logo_width(int scale);

}  // namespace poher::ui
