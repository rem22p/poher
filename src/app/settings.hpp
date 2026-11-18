// What the player has chosen on the settings screen.
#pragma once

#include "ui/lang.hpp"
#include "ui/table_draw.hpp"

namespace poher::app {

struct Settings {
    ui::Lang lang = ui::Lang::En;
    ui::Hint hint = ui::Hint::Name;  // what is written next to own cards
    bool sound = true;               // ring when it is the player's turn
    bool talk = true;                // bots say a line now and then
};

}  // namespace poher::app
