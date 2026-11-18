// Avatars. A picture is shown in a box of A x A/2 cells, which is a square on
// the screen. Every cell is split into 2 x 2 dots and painted with two
// colours, so the box holds 2A x A dots.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ui/canvas.hpp"
#include "ui/image.hpp"

namespace poher::ui {

// One cell of an avatar. The mask says which of its dots take the first
// colour: 1 top left, 2 top right, 4 bottom left. The bottom right dot always
// takes the second colour.
struct Quad {
    std::uint8_t mask = 3;
    Rgb first;
    Rgb second;
};

struct AvatarArt {
    int box = 0;              // cells across; box / 2 cells down
    std::vector<Quad> cells;  // row by row
};

// Turns a square picture into an avatar of the given box.
AvatarArt make_art(const Image& square, int box);

// Draws an avatar with its top left corner at (x, y). `faded` greys it out
// towards that colour, for players who have folded.
void draw_avatar(Canvas& canvas, int x, int y, const AvatarArt& art, std::optional<Rgb> faded = std::nullopt);

// The set of avatars: picture files in one folder. The name of an avatar is
// the file name without the extension.
class Avatars {
public:
    // Looks through the folder. Pictures are read later, when first shown.
    void open_folder(const std::filesystem::path& folder);
    // Adds or replaces an avatar by a picture already in memory.
    void add(const std::string& name, Image square);

    // Names in alphabetical order.
    std::vector<std::string> names() const;
    bool has(const std::string& name) const;

    // The avatar in a box of `box` cells. An unknown name, or a file that
    // cannot be read, gives a drawn stand-in: a robot for bots and a face
    // for people, on the `backdrop` colour.
    const AvatarArt& art(const std::string& name, int box, bool bot, Rgb backdrop);

private:
    const Image* picture(const std::string& name);

    std::map<std::string, std::filesystem::path> files_;
    std::map<std::string, std::optional<Image>> pictures_;
    std::map<std::pair<std::string, int>, AvatarArt> arts_;
    std::map<std::pair<int, std::uint32_t>, AvatarArt> stand_ins_;
};

}  // namespace poher::ui
