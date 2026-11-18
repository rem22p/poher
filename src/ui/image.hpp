// Pictures for avatars: reading a file and making it smaller.
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "ui/color.hpp"

namespace poher::ui {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<Rgb> pixels;  // row by row

    Rgb at(int x, int y) const {
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

// Reads a JPEG, PNG, BMP or GIF file. Empty if the file is missing, is not a
// picture, or is unreasonably big (more than 32 MB or 8192 dots a side).
// Transparent parts come out white.
std::optional<Image> load_image(const std::filesystem::path& file);

// The square part of a picture with its top left corner at (x, y). The
// numbers are brought into the picture if they stick out.
Image crop_square(const Image& picture, int x, int y, int side);
// The biggest square from the middle of a picture.
Image centre_square(const Image& picture);

// The picture squeezed or stretched to the new size (Lanczos filter, the same
// as the mockups used). No sharpening and no colour boost.
Image resize(const Image& picture, int width, int height);

}  // namespace poher::ui
