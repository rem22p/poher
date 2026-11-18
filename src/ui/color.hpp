// A colour and the palette of the game. The values come from the approved
// mockup, mockups/render.py.
#pragma once

#include <cmath>
#include <cstdint>

namespace poher::ui {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

// A colour between two others: 0 gives `from`, 1 gives `to`.
inline Rgb mix(Rgb from, Rgb to, double part) {
    const auto channel = [part](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(std::lround(a + (b - a) * part));
    };
    return Rgb{channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b)};
}

namespace color {

inline constexpr Rgb kBackground{14, 17, 22};
inline constexpr Rgb kText{222, 224, 228};
inline constexpr Rgb kDim{112, 118, 130};
inline constexpr Rgb kYellow{242, 200, 72};
inline constexpr Rgb kWhite{250, 250, 250};
inline constexpr Rgb kBar{24, 28, 37};
inline constexpr Rgb kPanel{20, 23, 30};
inline constexpr Rgb kRule{52, 58, 70};
inline constexpr Rgb kChip{36, 41, 52};

// Cards.
inline constexpr Rgb kCard{246, 244, 236};
inline constexpr Rgb kBack{38, 66, 146};
inline constexpr Rgb kBackLight{96, 134, 226};
inline constexpr Rgb kShadow{11, 60, 35};
inline constexpr Rgb kSlot{15, 78, 46};

// The table.
inline constexpr Rgb kEdge{52, 30, 16};
inline constexpr Rgb kWood{126, 78, 42};
inline constexpr Rgb kWoodLight{158, 104, 60};
inline constexpr Rgb kFelt{30, 124, 74};
inline constexpr Rgb kFeltEdge{17, 88, 52};
inline constexpr Rgb kFeltText{170, 214, 188};

// Seats.
inline constexpr Rgb kSeat{31, 36, 46};
inline constexpr Rgb kSeatActive{64, 55, 24};
inline constexpr Rgb kSeatFolded{21, 24, 31};
inline constexpr Rgb kBotTag{74, 86, 108};
inline constexpr Rgb kTimerRest{92, 80, 40};
inline constexpr Rgb kHero{66, 126, 220};

// Chips and buttons.
inline constexpr Rgb kRed{226, 72, 72};
inline constexpr Rgb kBlue{72, 132, 226};
inline constexpr Rgb kGreen{72, 200, 120};
inline constexpr Rgb kFold{176, 52, 58};
inline constexpr Rgb kCall{40, 164, 92};
inline constexpr Rgb kRaise{204, 150, 40};
inline constexpr Rgb kButtonText{200, 204, 210};
inline constexpr Rgb kDealer{30, 26, 10};

}  // namespace color

}  // namespace poher::ui
