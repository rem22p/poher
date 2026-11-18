#include "ui/canvas.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <utility>

namespace poher::ui {

namespace {

struct Range {
    char32_t first;
    char32_t last;
};

bool in_ranges(char32_t glyph, std::span<const Range> ranges) {
    for (const Range& range : ranges) {
        if (glyph >= range.first && glyph <= range.last) return true;
    }
    return false;
}

// Marks that are drawn over the previous glyph, and invisible format codes.
constexpr Range kZeroWidth[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2}, {0x05C4, 0x05C5},
    {0x0610, 0x061A}, {0x064B, 0x065F}, {0x0670, 0x0670}, {0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8},
    {0x06EA, 0x06ED}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF},
    {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F},
    {0xFEFF, 0xFEFF}, {0x1F3FB, 0x1F3FF}, {0xE0100, 0xE01EF},
};

// Glyphs that take two cells: East Asian scripts and emoji.
constexpr Range kWide[] = {
    {0x1100, 0x115F}, {0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE30, 0xFE4F}, {0xFF00, 0xFF60},
    {0xFFE0, 0xFFE6}, {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF}, {0x1FA70, 0x1FAFF},
    {0x20000, 0x3FFFD},
};

// Which quarters of a cell a block glyph paints: 1 top left, 2 top right,
// 4 bottom left, 8 bottom right. -1 for a glyph that is not a block.
int quarters_of(char32_t glyph) {
    switch (glyph) {
        case U' ': return 0;
        case U'▘': return 1;
        case U'▝': return 2;
        case U'▀': return 3;
        case U'▖': return 4;
        case U'▌': return 5;
        case U'▞': return 6;
        case U'▛': return 7;
        case U'▗': return 8;
        case U'▚': return 9;
        case U'▐': return 10;
        case U'▜': return 11;
        case U'▄': return 12;
        case U'▙': return 13;
        case U'▟': return 14;
        case U'█': return 15;
        default: return -1;
    }
}

void append_colour(std::string& out, Rgb colour) {
    out.push_back(static_cast<char>(colour.r));
    out.push_back(static_cast<char>(colour.g));
    out.push_back(static_cast<char>(colour.b));
}

}  // namespace

std::u32string decode_utf8(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        int extra = 0;
        char32_t glyph = 0;
        if (lead < 0x80) {
            glyph = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            glyph = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            glyph = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            glyph = lead & 0x07;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool good = i + static_cast<std::size_t>(extra) < text.size() || extra == 0;
        for (int k = 1; good && k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((next & 0xC0) != 0x80) {
                good = false;
            } else {
                glyph = (glyph << 6) | (next & 0x3F);
            }
        }
        if (!good) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(glyph);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

void append_utf8(std::string& out, char32_t glyph) {
    if (glyph < 0x80) {
        out.push_back(static_cast<char>(glyph));
    } else if (glyph < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (glyph >> 6)));
        out.push_back(static_cast<char>(0x80 | (glyph & 0x3F)));
    } else if (glyph < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (glyph >> 12)));
        out.push_back(static_cast<char>(0x80 | ((glyph >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (glyph & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (glyph >> 18)));
        out.push_back(static_cast<char>(0x80 | ((glyph >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((glyph >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (glyph & 0x3F)));
    }
}

int glyph_width(char32_t glyph) {
    if (glyph < 0x20 || (glyph >= 0x7F && glyph < 0xA0)) return 0;
    if (glyph < 0x300) return 1;
    if (in_ranges(glyph, kZeroWidth)) return 0;
    if (in_ranges(glyph, kWide)) return 2;
    return 1;
}

int text_width(std::string_view text) {
    int width = 0;
    for (const char32_t glyph : decode_utf8(text)) width += glyph_width(glyph);
    return width;
}

std::string fit_text(std::string_view text, int cells) {
    if (text_width(text) <= cells) return std::string(text);
    std::string out;
    int width = 0;
    for (const char32_t glyph : decode_utf8(text)) {
        const int glyph_cells = glyph_width(glyph);
        if (width + glyph_cells > cells - 1) break;
        append_utf8(out, glyph);
        width += glyph_cells;
    }
    if (cells >= 1) out += "…";
    return out;
}

Canvas::Canvas(int width, int height, Rgb background)
    : width_(std::max(width, 0)), height_(std::max(height, 0)) {
    Cell blank;
    blank.bg = background;
    cells_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), blank);
}

Cell* Canvas::at(int x, int y) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return nullptr;
    return &cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

const Cell& Canvas::cell(int x, int y) const {
    static const Cell blank;
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return blank;
    return cells_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

int Canvas::put(int x, int y, std::string_view text, std::optional<Rgb> fg, std::optional<Rgb> bg, bool bold) {
    int offset = 0;
    for (const char32_t glyph : decode_utf8(text)) {
        const int cells = glyph_width(glyph);
        if (cells == 0) continue;
        // A wide glyph that would stick out of the picture is replaced by a blank.
        const bool fits = cells == 1 || x + offset + 1 < width_;
        for (int part = 0; part < cells; ++part) {
            Cell* cell = at(x + offset + part, y);
            if (cell == nullptr) continue;
            cell->glyph = !fits ? U' ' : part == 0 ? glyph : U'\0';
            if (fg) cell->fg = *fg;
            if (bg) cell->bg = *bg;
            cell->bold = bold;
        }
        offset += cells;
    }
    return offset;
}

int Canvas::put_centred(int x, int y, std::string_view text, std::optional<Rgb> fg, std::optional<Rgb> bg,
                        bool bold) {
    return put(x - text_width(text) / 2, y, text, fg, bg, bold);
}

int Canvas::put_right(int x, int y, std::string_view text, std::optional<Rgb> fg, std::optional<Rgb> bg,
                      bool bold) {
    return put(x - text_width(text), y, text, fg, bg, bold);
}

void Canvas::fill(int x, int y, int w, int h, Rgb colour) {
    for (int row = std::max(y, 0); row < std::min(y + h, height_); ++row) {
        for (int column = std::max(x, 0); column < std::min(x + w, width_); ++column) {
            Cell* cell = at(column, row);
            cell->glyph = U' ';
            cell->bg = colour;
            cell->bold = false;
        }
    }
}

void Canvas::halves(int x, int y, Rgb top, Rgb bottom) {
    Cell* cell = at(x, y);
    if (cell == nullptr) return;
    cell->bold = false;
    if (top == bottom) {
        cell->glyph = U' ';
        cell->bg = top;
    } else {
        cell->glyph = U'▀';
        cell->fg = top;
        cell->bg = bottom;
    }
}

void Canvas::stamp(int x, int y, std::span<const std::string_view> rows, Rgb ink, Rgb paper, int scale, int dot_x,
                   int dot_y) {
    std::set<std::pair<int, int>> dots;
    int right = -1;
    int bottom = -1;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (std::size_t column = 0; column < rows[row].size(); ++column) {
            if (rows[row][column] != '#') continue;
            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    const int px = dot_x + static_cast<int>(column) * scale + dx;
                    const int py = dot_y + static_cast<int>(row) * scale + dy;
                    dots.insert({px, py});
                    right = std::max(right, px);
                    bottom = std::max(bottom, py);
                }
            }
        }
    }
    for (int row = 0; row <= bottom / 2; ++row) {
        for (int column = 0; column <= right; ++column) {
            const bool top = dots.contains({column, 2 * row});
            const bool low = dots.contains({column, 2 * row + 1});
            if (top && low) {
                fill(x + column, y + row, 1, 1, ink);
            } else if (top) {
                put(x + column, y + row, "▀", ink, paper);
            } else if (low) {
                put(x + column, y + row, "▄", ink, paper);
            }
        }
    }
}

void Canvas::outline(int x, int y, int w, int h, Rgb colour) {
    for (int column = x - 1; column <= x + w; ++column) {
        put(column, y - 1, "▄", colour);
        put(column, y + h, "▀", colour);
    }
    fill(x - 1, y, 1, h, colour);
    fill(x + w, y, 1, h, colour);
}

void Canvas::fade(int x, int y, int w, int h, Rgb towards, double part) {
    for (int row = y; row < y + h; ++row) {
        for (int column = x; column < x + w; ++column) {
            Cell* cell = at(column, row);
            if (cell == nullptr) continue;
            cell->fg = mix(cell->fg, towards, part);
            cell->bg = mix(cell->bg, towards, part);
        }
    }
}

std::string Canvas::to_ansi() const {
    std::string out;
    char code[64];
    for (int y = 0; y < height_; ++y) {
        const Cell* last = nullptr;
        for (int x = 0; x < width_; ++x) {
            const Cell& cell = this->cell(x, y);
            if (cell.glyph == U'\0') continue;
            if (last == nullptr || last->fg != cell.fg || last->bg != cell.bg || last->bold != cell.bold) {
                std::snprintf(code, sizeof code, "\x1b[0;%s38;2;%d;%d;%d;48;2;%d;%d;%dm", cell.bold ? "1;" : "",
                              cell.fg.r, cell.fg.g, cell.fg.b, cell.bg.r, cell.bg.g, cell.bg.b);
                out += code;
                last = &cell;
            }
            append_utf8(out, cell.glyph);
        }
        out += "\x1b[0m\n";
    }
    return out;
}

std::vector<std::string> Canvas::to_text() const {
    std::vector<std::string> lines;
    for (int y = 0; y < height_; ++y) {
        std::string line;
        for (int x = 0; x < width_; ++x) {
            const char32_t glyph = cell(x, y).glyph;
            if (glyph != U'\0') append_utf8(line, glyph);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string Canvas::to_ppm() const {
    std::string out = "P6 " + std::to_string(width_ * 2) + " " + std::to_string(height_ * 4) + " 255\n";
    std::string top;
    std::string low;
    for (int y = 0; y < height_; ++y) {
        top.clear();
        low.clear();
        for (int x = 0; x < width_; ++x) {
            const Cell& cell = this->cell(x, y);
            const int quarters = quarters_of(cell.glyph);
            Rgb dots[4];
            for (int i = 0; i < 4; ++i) {
                if (quarters < 0) {
                    dots[i] = mix(cell.bg, cell.fg, 0.55);
                } else {
                    dots[i] = (quarters >> i) & 1 ? cell.fg : cell.bg;
                }
            }
            append_colour(top, dots[0]);
            append_colour(top, dots[1]);
            append_colour(low, dots[2]);
            append_colour(low, dots[3]);
        }
        out += top;
        out += top;
        out += low;
        out += low;
    }
    return out;
}

}  // namespace poher::ui
