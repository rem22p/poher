#include "ui/avatar.hpp"

#include <algorithm>
#include <cctype>
#include <array>
#include <string_view>

namespace poher::ui {

namespace {

// How much closer to the picture a split into quarters must be than a plain
// top half and bottom half before it is used (sum of squared colour
// differences over the cell). Higher gives a softer picture, lower a sharper
// and harsher one. The value was picked by eye on the mockups.
constexpr double kEdgeGain = 3500;

constexpr std::string_view kQuadGlyphs[] = {" ", "▘", "▝", "▀", "▖", "▌", "▞", "▛"};

struct Mean {
    double r = 0;
    double g = 0;
    double b = 0;
};

// The average colour of the dots picked by the mask, and how far they are from it.
double spread(const std::array<Rgb, 4>& dots, int mask, Mean& mean) {
    int count = 0;
    mean = Mean{};
    for (int i = 0; i < 4; ++i) {
        if (!((mask >> i) & 1)) continue;
        mean.r += dots[static_cast<std::size_t>(i)].r;
        mean.g += dots[static_cast<std::size_t>(i)].g;
        mean.b += dots[static_cast<std::size_t>(i)].b;
        ++count;
    }
    if (count == 0) return 0;
    mean.r /= count;
    mean.g /= count;
    mean.b /= count;
    double error = 0;
    for (int i = 0; i < 4; ++i) {
        if (!((mask >> i) & 1)) continue;
        const Rgb dot = dots[static_cast<std::size_t>(i)];
        error += (dot.r - mean.r) * (dot.r - mean.r) + (dot.g - mean.g) * (dot.g - mean.g) +
                 (dot.b - mean.b) * (dot.b - mean.b);
    }
    return error;
}

Rgb to_colour(const Mean& mean) {
    const auto channel = [](double value) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
    };
    return Rgb{channel(mean.r), channel(mean.g), channel(mean.b)};
}

// dots: top left, top right, bottom left, bottom right.
//
// A cell is normally a plain top half and bottom half, which keeps smooth
// areas smooth. Only where a real edge would be smeared by that is the cell
// split into quarters instead.
Quad make_quad(const std::array<Rgb, 4>& dots) {
    Mean top;
    Mean low;
    const double halves_error = spread(dots, 0b0011, top) + spread(dots, 0b1100, low);

    double best_error = -1;
    int best_mask = 3;
    Mean best_first;
    Mean best_second;
    for (const int mask : {1, 2, 4, 5, 6, 7}) {
        Mean first;
        Mean second;
        const double error = spread(dots, mask, first) + spread(dots, 15 & ~mask, second);
        if (best_error < 0 || error < best_error) {
            best_error = error;
            best_mask = mask;
            best_first = first;
            best_second = second;
        }
    }
    if (halves_error - best_error > kEdgeGain) {
        return Quad{static_cast<std::uint8_t>(best_mask), to_colour(best_first), to_colour(best_second)};
    }
    return Quad{3, to_colour(top), to_colour(low)};
}

// Stand-ins for a missing picture, 12 x 12 dots.
constexpr std::string_view kFace[] = {
    "...hhhhhh...", "..hhhhhhhh..", ".hhhssssshh.", ".hssssssssh.", ".kkkkkkkkkk.", ".skkksskkks.",
    ".ssssssssss.", ".ssssssssss.", "..ssmmmmss..", "..ssssssss..", "...ssssss...", "............",
};
constexpr std::string_view kRobot[] = {
    ".....rr.....", ".....gg.....", "..gggggggg..", ".gggggggggg.", ".gyyggggyyg.", ".gyyggggyyg.",
    ".gggggggggg.", ".ggddddddgg.", ".gggggggggg.", "..gggggggg..", "...gg..gg...", "............",
};

Rgb stand_in_colour(char letter, Rgb backdrop) {
    switch (letter) {
        case 's': return Rgb{238, 188, 140};
        case 'h': return Rgb{84, 52, 30};
        case 'k': return Rgb{22, 22, 28};
        case 'm': return Rgb{170, 60, 66};
        case 'g': return Rgb{156, 166, 182};
        case 'd': return Rgb{40, 46, 58};
        case 'y': return Rgb{250, 212, 84};
        case 'r': return Rgb{232, 72, 72};
        default: return backdrop;
    }
}

AvatarArt make_stand_in(int box, bool bot, Rgb backdrop) {
    const auto& art = bot ? kRobot : kFace;
    AvatarArt out;
    out.box = box;
    const int rows = box / 2;
    out.cells.reserve(static_cast<std::size_t>(box) * static_cast<std::size_t>(rows));
    const auto dot = [&](int column, int y) {
        const auto row = static_cast<std::size_t>(y * 12 / box);
        const auto col = static_cast<std::size_t>(column * 12 / box);
        return stand_in_colour(art[row][col], backdrop);
    };
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < box; ++column) {
            out.cells.push_back(Quad{3, dot(column, 2 * row), dot(column, 2 * row + 1)});
        }
    }
    return out;
}

bool is_picture(const std::filesystem::path& file) {
    std::string extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" || extension == ".bmp" ||
           extension == ".gif";
}

}  // namespace

AvatarArt make_art(const Image& square, int box) {
    const int rows = box / 2;
    const Image dots = resize(square, box * 2, rows * 2);
    AvatarArt out;
    out.box = box;
    out.cells.reserve(static_cast<std::size_t>(box) * static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < box; ++column) {
            out.cells.push_back(make_quad({dots.at(2 * column, 2 * row), dots.at(2 * column + 1, 2 * row),
                                           dots.at(2 * column, 2 * row + 1), dots.at(2 * column + 1, 2 * row + 1)}));
        }
    }
    return out;
}

void draw_avatar(Canvas& canvas, int x, int y, const AvatarArt& art, std::optional<Rgb> faded) {
    const int rows = art.box / 2;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < art.box; ++column) {
            const Quad& quad =
                art.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(art.box) +
                          static_cast<std::size_t>(column)];
            Rgb first = quad.first;
            Rgb second = quad.second;
            if (faded) {
                first = mix(first, *faded, 0.65);
                second = mix(second, *faded, 0.65);
            }
            if (quad.mask == 0) {
                canvas.fill(x + column, y + row, 1, 1, second);
            } else {
                canvas.put(x + column, y + row, kQuadGlyphs[quad.mask & 7], first, second);
            }
        }
    }
}

void Avatars::open_folder(const std::filesystem::path& folder) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (!entry.is_regular_file(error) || !is_picture(entry.path())) continue;
        const std::u8string stem = entry.path().stem().u8string();
        files_[std::string(stem.begin(), stem.end())] = entry.path();
    }
}

void Avatars::add(const std::string& name, Image square) {
    pictures_[name] = std::move(square);
    files_.try_emplace(name);
    std::erase_if(arts_, [&](const auto& item) { return item.first.first == name; });
}

std::vector<std::string> Avatars::names() const {
    std::vector<std::string> out;
    for (const auto& [name, file] : files_) out.push_back(name);
    return out;
}

bool Avatars::has(const std::string& name) const {
    return files_.contains(name);
}

const Image* Avatars::picture(const std::string& name) {
    auto found = pictures_.find(name);
    if (found == pictures_.end()) {
        const auto file = files_.find(name);
        if (file == files_.end()) return nullptr;
        std::optional<Image> image = load_image(file->second);
        if (image && image->width != image->height) image = centre_square(*image);
        found = pictures_.emplace(name, std::move(image)).first;
    }
    return found->second ? &*found->second : nullptr;
}

const AvatarArt& Avatars::art(const std::string& name, int box, bool bot, Rgb backdrop) {
    const auto key = std::make_pair(name, box);
    if (const auto ready = arts_.find(key); ready != arts_.end()) return ready->second;
    if (const Image* image = picture(name)) return arts_.emplace(key, make_art(*image, box)).first->second;

    const std::uint32_t colour = (static_cast<std::uint32_t>(backdrop.r) << 16) |
                                 (static_cast<std::uint32_t>(backdrop.g) << 8) | backdrop.b;
    const auto stand_in_key = std::make_pair(bot ? -box : box, colour);
    auto found = stand_ins_.find(stand_in_key);
    if (found == stand_ins_.end()) found = stand_ins_.emplace(stand_in_key, make_stand_in(box, bot, backdrop)).first;
    return found->second;
}

}  // namespace poher::ui
