#include "ui/image.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>

#include "ui/stb.hpp"

namespace poher::ui {

namespace {

constexpr std::uintmax_t kMaxFileSize = 32u * 1024 * 1024;
constexpr int kMaxSide = 8192;

// Which dots of the old picture make one dot of the new one, and how much of each.
struct Tap {
    int first = 0;
    std::vector<float> weights;
};

double lanczos(double x) {
    x = std::fabs(x);
    if (x < 1e-9) return 1.0;
    if (x >= 3.0) return 0.0;
    const double pix = std::numbers::pi * x;
    return 3.0 * std::sin(pix) * std::sin(pix / 3.0) / (pix * pix);
}

std::vector<Tap> taps(int from, int to) {
    std::vector<Tap> out(static_cast<std::size_t>(to));
    const double scale = static_cast<double>(from) / to;
    // When shrinking, the filter is widened so that every old dot counts.
    const double stretch = std::max(scale, 1.0);
    const double reach = 3.0 * stretch;
    for (int i = 0; i < to; ++i) {
        const double centre = (i + 0.5) * scale;
        const int first = std::max(static_cast<int>(std::floor(centre - reach)), 0);
        const int last = std::min(static_cast<int>(std::ceil(centre + reach)), from - 1);
        Tap& tap = out[static_cast<std::size_t>(i)];
        tap.first = first;
        double sum = 0;
        for (int j = first; j <= last; ++j) {
            const double weight = lanczos((j + 0.5 - centre) / stretch);
            tap.weights.push_back(static_cast<float>(weight));
            sum += weight;
        }
        if (sum != 0) {
            for (float& weight : tap.weights) weight = static_cast<float>(weight / sum);
        }
    }
    return out;
}

std::uint8_t to_byte(float value) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

}  // namespace

std::optional<Image> load_image(const std::filesystem::path& file) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error || size == 0 || size > kMaxFileSize) return std::nullopt;

    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return std::nullopt;

    int width = 0;
    int height = 0;
    int channels = 0;
    const int length = static_cast<int>(bytes.size());
    if (!stbi_info_from_memory(bytes.data(), length, &width, &height, &channels)) return std::nullopt;
    if (width < 1 || height < 1 || width > kMaxSide || height > kMaxSide) return std::nullopt;

    unsigned char* data = stbi_load_from_memory(bytes.data(), length, &width, &height, &channels, 4);
    if (data == nullptr) return std::nullopt;

    Image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        const unsigned char* dot = data + i * 4;
        // Lay see-through dots over white.
        const int alpha = dot[3];
        const auto over_white = [alpha](int value) {
            return static_cast<std::uint8_t>((value * alpha + 255 * (255 - alpha) + 127) / 255);
        };
        image.pixels[i] = Rgb{over_white(dot[0]), over_white(dot[1]), over_white(dot[2])};
    }
    stbi_image_free(data);
    return image;
}

Image crop_square(const Image& picture, int x, int y, int side) {
    side = std::clamp(side, 1, std::min(picture.width, picture.height));
    x = std::clamp(x, 0, picture.width - side);
    y = std::clamp(y, 0, picture.height - side);
    Image out;
    out.width = side;
    out.height = side;
    out.pixels.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) out.pixels.push_back(picture.at(x + column, y + row));
    }
    return out;
}

Image centre_square(const Image& picture) {
    const int side = std::min(picture.width, picture.height);
    return crop_square(picture, (picture.width - side) / 2, (picture.height - side) / 2, side);
}

Image resize(const Image& picture, int width, int height) {
    Image out;
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    if (picture.width < 1 || picture.height < 1 || width < 1 || height < 1) return out;

    // First across, then down; the colours stay fractional in between.
    const std::vector<Tap> across = taps(picture.width, width);
    const std::vector<Tap> down = taps(picture.height, height);
    const auto w = static_cast<std::size_t>(width);
    std::vector<float> rows(static_cast<std::size_t>(picture.height) * w * 3);
    for (int y = 0; y < picture.height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Tap& tap = across[static_cast<std::size_t>(x)];
            float r = 0;
            float g = 0;
            float b = 0;
            for (std::size_t k = 0; k < tap.weights.size(); ++k) {
                const Rgb dot = picture.at(tap.first + static_cast<int>(k), y);
                r += tap.weights[k] * dot.r;
                g += tap.weights[k] * dot.g;
                b += tap.weights[k] * dot.b;
            }
            float* cell = &rows[(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 3];
            cell[0] = r;
            cell[1] = g;
            cell[2] = b;
        }
    }
    for (int y = 0; y < height; ++y) {
        const Tap& tap = down[static_cast<std::size_t>(y)];
        for (int x = 0; x < width; ++x) {
            float r = 0;
            float g = 0;
            float b = 0;
            for (std::size_t k = 0; k < tap.weights.size(); ++k) {
                const float* cell =
                    &rows[((static_cast<std::size_t>(tap.first) + k) * w + static_cast<std::size_t>(x)) * 3];
                r += tap.weights[k] * cell[0];
                g += tap.weights[k] * cell[1];
                b += tap.weights[k] * cell[2];
            }
            out.pixels[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                Rgb{to_byte(r), to_byte(g), to_byte(b)};
        }
    }
    return out;
}

}  // namespace poher::ui
