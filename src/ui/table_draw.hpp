// Draws the table screen. The layout, sizes and colours follow the approved
// mockup, mockups/render.py; here the scene is not fixed but comes from a
// TableView.
#pragma once

#include <string>

#include "host/view.hpp"
#include "ui/avatar.hpp"
#include "ui/canvas.hpp"
#include "ui/lang.hpp"

namespace poher::ui {

// What is written next to the player's own cards.
enum class Hint : std::uint8_t {
    Off,
    Name,  // the name of the hand
    Full,  // and under it the outs and the chances
};

// What the screen itself adds to the state of the table: the choices of the
// person in front of it.
struct TableLook {
    Lang lang = Lang::En;
    bool panel = false;           // the side panel is open
    Hint hint = Hint::Name;
    std::string hint_more;        // the second line of the full hint: outs and chances
    std::string title;            // the name of the table
    game::Chips bankroll = 0;

    int focus = 1;                // the picked button: 0 fold, 1 check or call, 2 bet or raise
    bool raising = false;         // the amount of a bet or raise is being chosen
    game::Chips raise_to = 0;
    std::string typed;            // digits typed for the amount, if any

    game::Chips buy_amount = 0;   // chips the B key would add now; 0 = nothing to add
    bool refill = false;          // the bankroll has to be refilled for that
    int portrait_seat = -1;       // whose big picture is in the side panel
    std::string flash;            // a short message for the bottom line
};

// The smallest window the table can be drawn in.
inline constexpr int kMinWidth = 110;
inline constexpr int kMinHeight = 34;

bool table_fits(int width, int height);
// The "make the window bigger" screen.
void draw_too_small(Canvas& canvas);

void draw_table(Canvas& canvas, const host::TableView& view, const TableLook& look, Avatars& avatars);

// The amount a bet-size button stands for: 0 half the pot, 1 two thirds,
// 2 the pot, 3 all-in. Kept within what the rules allow.
game::Chips sized_raise(const host::TableView& view, int size);

}  // namespace poher::ui
