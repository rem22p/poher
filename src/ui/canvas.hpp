// The picture of one screen: a grid of character cells, each with a glyph, a
// text colour and a background colour. Everything the game shows is drawn
// here first and then handed to the terminal in one go, so the drawing code
// does not depend on the terminal and can be checked without one.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ui/color.hpp"

namespace poher::ui {

struct Cell {
    // What is drawn in the cell. 0 marks the right half of a double-width
    // glyph that starts in the cell to the left.
    char32_t glyph = U' ';
    Rgb fg = color::kText;
    Rgb bg = color::kBackground;
    bool bold = false;
};

// "Leave this colour as it is" in calls to Canvas::put.
inline constexpr std::nullopt_t kKeep = std::nullopt;

class Canvas {
public:
    Canvas(int width, int height, Rgb background = color::kBackground);

    int width() const { return width_; }
    int height() const { return height_; }
    // Any position may be asked for: outside the picture the answer is a blank cell.
    const Cell& cell(int x, int y) const;

    // Writes text from (x, y) to the right, one cell per glyph. Parts that
    // fall outside the picture are dropped. Returns the width of the text in cells.
    int put(int x, int y, std::string_view text, std::optional<Rgb> fg = kKeep, std::optional<Rgb> bg = kKeep,
            bool bold = false);
    // The same, but centred on x or ending just before x.
    int put_centred(int x, int y, std::string_view text, std::optional<Rgb> fg = kKeep,
                    std::optional<Rgb> bg = kKeep, bool bold = false);
    int put_right(int x, int y, std::string_view text, std::optional<Rgb> fg = kKeep, std::optional<Rgb> bg = kKeep,
                  bool bold = false);

    // A solid rectangle.
    void fill(int x, int y, int w, int h, Rgb colour);
    // One cell as two dots, one above the other.
    void halves(int x, int y, Rgb top, Rgb bottom);
    // A picture of one colour, drawn two dots per cell in height. Each row of
    // `rows` is a string where '#' is ink. `scale` enlarges every dot;
    // `dot_x` and `dot_y` shift the picture by that many dots from (x, y).
    void stamp(int x, int y, std::span<const std::string_view> rows, Rgb ink, Rgb paper, int scale = 1,
               int dot_x = 0, int dot_y = 0);
    // A thin outline around the rectangle, just outside it.
    void outline(int x, int y, int w, int h, Rgb colour);
    // Fades a rectangle towards a colour: 0 changes nothing, 1 leaves only `towards`.
    void fade(int x, int y, int w, int h, Rgb towards, double part);

    // The picture as terminal escape codes, one line of text per row.
    std::string to_ansi() const;
    // The glyphs alone, for checking the layout in tests.
    std::vector<std::string> to_text() const;
    // The picture as an image file (PPM), 2 x 4 dots per cell. Block glyphs
    // come out exact, letters become a tinted cell.
    std::string to_ppm() const;

private:
    Cell* at(int x, int y);

    int width_;
    int height_;
    std::vector<Cell> cells_;
};

// Text helpers. All text in the game is UTF-8.
std::u32string decode_utf8(std::string_view text);
void append_utf8(std::string& out, char32_t glyph);
// How many cells a glyph takes: 0 for marks that attach to the previous
// glyph and for control codes, 2 for wide glyphs, otherwise 1.
int glyph_width(char32_t glyph);
int text_width(std::string_view text);
// Cuts the text so that it fits into `cells`; a cut text ends with "…".
std::string fit_text(std::string_view text, int cells);

}  // namespace poher::ui
