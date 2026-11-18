#include "ui/table_draw.hpp"

#include <algorithm>
#include <cmath>

#include "host/roster.hpp"
#include "ui/card_art.hpp"

namespace poher::ui {

namespace {

using game::Chips;
using host::Note;
using host::SeatView;
using host::Stage;
using host::TableView;

// Sizes for a window: the first row that fits wins. `scale` is the size of
// the cards, `avatar` the avatar box at the table, `portrait` the box of the
// big picture in the side panel.
struct Tier {
    int min_w;
    int min_h;
    int scale;
    int avatar;
    int portrait;
};

constexpr Tier kPlain[] = {{216, 62, 2, 32, 0}, {178, 46, 2, 24, 0}, {kMinWidth, kMinHeight, 1, 16, 0}};
constexpr Tier kWithPanel[] = {{224, 60, 2, 24, 48}, {190, 44, 2, 16, 32}, {138, 34, 1, 16, 24}};

constexpr int kInfoWidth = 16;  // the block with the name next to an avatar

// Where the other players sit, clockwise from the one at the bottom.
enum class Spot { Left, TopLeft, TopCentre, TopRight, Right };

std::vector<Spot> spots_for(int others) {
    switch (others) {
        case 1: return {Spot::TopCentre};
        case 2: return {Spot::TopLeft, Spot::TopRight};
        case 3: return {Spot::Left, Spot::TopCentre, Spot::Right};
        case 4: return {Spot::Left, Spot::TopLeft, Spot::TopRight, Spot::Right};
        default: return {Spot::Left, Spot::TopLeft, Spot::TopCentre, Spot::TopRight, Spot::Right};
    }
}

enum class SeatState { Normal, Active, Folded, Winner };

// The colour of the chip in front of a bet: the bigger the bet, the "dearer" the chip.
Rgb chip_colour(Chips amount, Chips big_blind) {
    if (amount < 5 * big_blind) return color::kRed;
    if (amount < 25 * big_blind) return color::kBlue;
    if (amount < 100 * big_blind) return color::kGreen;
    return color::kYellow;
}

class Painter {
public:
    Painter(Canvas& canvas, const TableView& view, const TableLook& look, Avatars& avatars, Tier tier, bool overlay)
        : canvas_(canvas), view_(view), look_(look), avatars_(avatars), words_(words(look.lang)) {
        full_w_ = canvas.width();
        h_ = canvas.height();
        k_ = tier.scale;
        a_ = tier.avatar;
        portrait_ = tier.portrait;
        panel_w_ = portrait_ > 0 ? portrait_ + 4 : 0;
        overlay_ = overlay;
        // A panel laid over the table does not make the table narrower.
        w_ = overlay ? full_w_ : full_w_ - panel_w_;

        card_ = card_size(k_);
        backs_ = backs_size(k_);
        gap_ = k_;
        ah_ = a_ / 2;
        side_w_ = a_ + 4;
        const int below = k_ == 1 ? 10 : 13;
        rim_ = k_ == 1 ? 3 : 5;

        tx0_ = side_w_ + 1;
        tx1_ = w_ - side_w_ - 1;
        ty0_ = 5;
        ty1_ = h_ - below;
        const int rows = ty1_ - ty0_;
        cx_ = (tx0_ + tx1_) / 2.0;
        cy_ = ty0_ + ty1_;
        half_w_ = (tx1_ - tx0_) / 2.0;
        half_h_ = rows;
        radius_ = std::min({static_cast<int>(std::lround(0.76 * rows)), static_cast<int>(half_w_), rows});
        centre_ = static_cast<int>(std::lround(cx_));
        mid_ = (ty0_ + ty1_) / 2;

        top_y_ = std::max(ah_ - backs_.h, ty0_ + (k_ == 1 ? 2 : 3));
        board_y_ = std::max(mid_ - card_.h / 2 - 1, top_y_ + backs_.h + 2);
        pot_y_ = board_y_ + card_.h + 2;

        hero_w_ = 2 * card_.w + gap_;
        hero_x_ = centre_ - hero_w_ / 2;
        hero_y_ = ty1_ - (k_ == 1 ? 3 : 4);
        hero_avatar_x_ = hero_x_ - a_ - 3;
        hero_info_x_ = hero_avatar_x_ - kInfoWidth - 2;
    }

    void paint() {
        paint_felt();
        paint_board();
        paint_others();
        paint_hero();
        // After the seats: the frame round a top seat to act must not cover it.
        paint_title();
        paint_middle();
        paint_actions();
        if (panel_w_ > 0) paint_panel();
        paint_bottom();
    }

private:
    // ---------------------------------------------------------------- the table itself
    // The colour of one dot of the table; `felt` tells whether it is cloth.
    // Dots are half a cell high, so y runs twice as fast as rows.
    Rgb dot(double px, double py, bool& felt) const {
        felt = false;
        const double qx = std::fabs(px - cx_) - (half_w_ - radius_);
        const double qy = std::fabs(py - cy_) - (half_h_ - radius_);
        const double d = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - radius_;
        if (d > 0) return color::kBackground;
        if (d > -1) return color::kEdge;
        if (d > -1 - (rim_ - 1) / 2.0) return color::kWood;
        if (d > -rim_) return color::kWoodLight;
        felt = true;
        // The cloth gets darker towards the edge, in a few flat steps.
        double t = std::hypot((px - cx_) / half_w_, (py - cy_) / half_h_);
        t = std::pow(std::min(1.0, t), 1.6);
        t = std::round(t * 5) / 5;
        return mix(color::kFelt, color::kFeltEdge, t);
    }

    bool is_felt(int x, int row) const {
        bool top = false;
        bool low = false;
        dot(x + 0.5, row * 2 + 0.5, top);
        dot(x + 0.5, row * 2 + 1.5, low);
        return top && low;
    }

    // The first column of cloth in a row, from the left.
    int felt_left(int row) const {
        int x = tx0_;
        while (x < cx_ && !is_felt(x, row)) ++x;
        return x;
    }

    // The x nearest to `wanted` where something `width` wide lies fully on the cloth.
    int on_felt(int wanted, int row, int width) const {
        const int low = felt_left(row) + 1;
        return std::min(std::max(wanted, low), tx0_ + tx1_ - low - width);
    }

    void paint_felt() {
        for (int row = ty0_; row < ty1_; ++row) {
            for (int x = tx0_; x < tx1_; ++x) {
                bool felt = false;
                const Rgb top = dot(x + 0.5, row * 2 + 0.5, felt);
                const Rgb low = dot(x + 0.5, row * 2 + 1.5, felt);
                canvas_.halves(x, row, top, low);
            }
        }
    }

    void paint_title() {
        canvas_.fill(0, 0, full_w_, 1, color::kBar);
        const game::Suit suits[] = {game::Suit::Spades, game::Suit::Hearts, game::Suit::Diamonds, game::Suit::Clubs};
        for (int i = 0; i < 4; ++i) canvas_.put(1 + i, 0, suit_sign(suits[i]), suit_colour_on_dark(suits[i]), color::kBar);
        canvas_.put(6, 0, "POHER", color::kWhite, color::kBar, true);

        std::string line;
        if (!look_.title.empty()) line += look_.title + "  ·  ";
        line += std::string(words_.game) + "  ·  " + std::string(words_.blinds) + " " +
                chips_text(view_.stakes.small_blind) + "/" + chips_text(view_.stakes.big_blind);
        if (view_.hand_number > 0) line += "  ·  " + std::string(words_.hand) + std::to_string(view_.hand_number);
        const std::string money = std::string(words_.bankroll) + " " + chips_text(look_.bankroll);
        const int room = full_w_ - 14 - text_width(money) - 4;
        canvas_.put(14, 0, fit_text(line, room), color::kDim, color::kBar);
        canvas_.put_right(full_w_ - 2, 0, money, color::kText, color::kBar);
    }

    // ---------------------------------------------------------------- seats
    SeatState state_of(int index) const {
        const SeatView& seat = view_.seats[static_cast<std::size_t>(index)];
        if (index == view_.acting) return SeatState::Active;
        if (seat.won > 0) return SeatState::Winner;
        if (seat.folded || seat.away || (!seat.in_hand && view_.stage != Stage::Idle)) return SeatState::Folded;
        return SeatState::Normal;
    }

    // The third line of a seat: what the player did last.
    std::string third_line(int index, bool& bright) const {
        const SeatView& seat = view_.seats[static_cast<std::size_t>(index)];
        bright = false;
        if (index == view_.acting) {
            // The viewer's own turn is announced next to the buttons.
            bright = true;
            if (index == view_.viewer) return {};
            const std::string clock = clock_text();
            return clock.empty() ? std::string(words_.thinking) : std::string(words_.thinking) + " · " + clock;
        }
        if (seat.offline) return std::string(words_.offline);
        // Once the cards are up, the hand they make says more than the last move.
        if (seat.shown && seat.cards && view_.board.size() >= 3) {
            std::vector<game::Card> all(view_.board);
            all.push_back((*seat.cards)[0]);
            all.push_back((*seat.cards)[1]);
            bright = seat.won > 0;
            return std::string(category_name(game::evaluate(all).category(), look_.lang));
        }
        bright = seat.note == Note::Win;
        return note_text(seat.note, seat.note_amount, look_.lang);
    }

    // "23 s" while there is time for the move, then "reserve 45 s"; empty without a clock.
    std::string clock_text() const {
        if (view_.turn_ms < 0) return {};
        const auto seconds = [](int ms) { return std::to_string((ms + 999) / 1000); };
        if (view_.turn_ms > 0) return fill(words_.clock_left, {seconds(view_.turn_ms)});
        return fill(words_.clock_reserve, {seconds(std::max(view_.bank_ms, 0))});
    }

    Rgb name_colour(const SeatView& seat, int index) const {
        if (seat.bot) return color::kBotTag;
        if (index == view_.viewer) return color::kHero;
        static constexpr Rgb people[] = {{198, 84, 156}, {228, 138, 48}, {96, 190, 170}, {170, 120, 220}, {206, 190, 90}};
        return people[static_cast<std::size_t>(index) % std::size(people)];
    }

    // The avatar at (ax, ay) and the three-line block with the name at (ix, iy).
    void paint_seat(int index, int ax, int ay, int ix, int iy, int iw) {
        const SeatView& seat = view_.seats[static_cast<std::size_t>(index)];
        const SeatState state = state_of(index);
        const bool folded = state == SeatState::Folded;
        const bool lit = state == SeatState::Active || state == SeatState::Winner;

        const AvatarArt& art = avatars_.art(seat.avatar, a_, seat.bot, name_colour(seat, index));
        draw_avatar(canvas_, ax, ay, art, folded ? std::optional<Rgb>(color::kSeatFolded) : std::nullopt);
        if (lit) canvas_.outline(ax, ay, a_, ah_, color::kYellow);

        const Rgb back = lit ? color::kSeatActive : folded ? color::kSeatFolded : color::kSeat;
        const Rgb name_fg = lit ? color::kYellow : folded ? color::kDim : color::kText;
        canvas_.fill(ix, iy, iw, 3, back);

        const int name_room = iw - 2 - (seat.bot ? 6 : 0);
        const int name_w = canvas_.put(ix + 1, iy, fit_text(seat.name, name_room), name_fg, back, true);
        if (seat.bot) {
            const Rgb tag = folded ? mix(color::kBotTag, back, 0.6) : color::kBotTag;
            canvas_.put(ix + 2 + name_w, iy, " BOT ", folded ? color::kDim : color::kWhite, tag, true);
        }

        const int stack_w = canvas_.put(ix + 1, iy + 1, chips_text(seat.stack), folded ? color::kDim : color::kText, back);
        if (seat.won > 0) {
            canvas_.put(ix + 2 + stack_w, iy + 1, fit_text("+" + chips_text(seat.won), iw - 3 - stack_w),
                        color::kYellow, back, true);
        }

        bool bright = false;
        const std::string third = third_line(index, bright);
        canvas_.put(ix + 1, iy + 2, fit_text(third, iw - 2), bright ? color::kYellow : color::kDim, back, bright);
    }

    void paint_bet(int x, int y, Chips amount, int align) {
        const std::string text = "● " + chips_text(amount);
        const int width = text_width(text);
        if (align > 0) x -= width;           // ends at x
        if (align == 0) x -= width / 2;      // centred on x
        canvas_.put(x, y, "●", chip_colour(amount, view_.stakes.big_blind));
        canvas_.put(x + 2, y, chips_text(amount), color::kWhite, kKeep, true);
    }

    void paint_dealer(int x, int y) {
        canvas_.put(x, y, " D ", color::kDealer, color::kYellow, true);
    }

    // Cards of another player: face down, face up at the showdown, or none.
    // (x, y) is where face-down cards go; face-up ones are bigger and are
    // lined up with them by the given edge. Returns the x and width used.
    void paint_cards(const SeatView& seat, int x, int y, bool align_right, int& used_x, int& used_w) {
        used_x = x;
        used_w = backs_.w;
        if (!seat.in_hand || seat.folded) return;
        if (seat.cards) {
            const Size shown = shown_size(k_);
            const int sx = align_right ? x + backs_.w - shown.w : x;
            draw_shown(canvas_, sx, y + backs_.h - shown.h, *seat.cards, k_);
            used_x = sx;
            used_w = shown.w;
        } else {
            draw_backs(canvas_, x, y, k_);
        }
    }

    void paint_others() {
        const int seats = static_cast<int>(view_.seats.size());
        if (seats < 2) return;
        const int bottom = view_.viewer >= 0 ? view_.viewer : 0;
        const std::vector<Spot> spots = spots_for(seats - 1);

        // Top seats: avatar and block side by side, the avatar overlapping the table edge.
        const int group = a_ + 1 + kInfoWidth;
        const int apart = std::min((w_ - group) / 2 - 2, std::max((tx1_ - tx0_ - group) / 2, group + 3));
        // Side seats: the block under the avatar, clear of the top row of avatars.
        const int side_y = std::max(mid_ - (ah_ + 3) / 2, ah_ + 2);
        const int side_ax = (side_w_ - a_) / 2;
        const int side_cards_y = mid_ - backs_.h;
        const int side_cards_x = felt_left(side_cards_y) + 2;

        for (int i = 1; i < seats; ++i) {
            const int index = (bottom + i) % seats;
            const SeatView& seat = view_.seats[static_cast<std::size_t>(index)];
            if (!seat.taken) continue;
            const Spot spot = spots[static_cast<std::size_t>(i - 1)];
            int used_x = 0;
            int used_w = 0;

            if (spot == Spot::Left || spot == Spot::Right) {
                const bool right = spot == Spot::Right;
                const int column = right ? w_ - side_w_ : 0;
                paint_seat(index, column + side_ax, side_y, column + 1, side_y + ah_, side_w_ - 2);
                const int x = right ? tx0_ + tx1_ - side_cards_x - backs_.w : side_cards_x;
                paint_cards(seat, x, side_cards_y, right, used_x, used_w);
                const int below = side_cards_y + backs_.h;
                if (seat.bet > 0) paint_bet(right ? x + backs_.w - 1 : x + 1, below, seat.bet, right ? 1 : -1);
                if (index == view_.button) paint_dealer(right ? x + backs_.w - 4 : x + 1, below + 1);
                continue;
            }

            const int left = spot == Spot::TopLeft    ? centre_ - apart - group / 2
                             : spot == Spot::TopRight ? centre_ + apart - group / 2
                                                      : centre_ - group / 2;
            // On the right the picture is mirrored: block first, then the avatar.
            const bool mirrored = spot == Spot::TopRight;
            const int info_x = mirrored ? left : left + a_ + 1;
            const int avatar_x = mirrored ? left + kInfoWidth + 1 : left;
            paint_seat(index, avatar_x, 1, info_x, 1, kInfoWidth);

            // The cards lie under the block, level with the bottom of the avatar.
            const int x = on_felt(info_x + (kInfoWidth - backs_.w) / 2, top_y_, backs_.w);
            paint_cards(seat, x, top_y_, mirrored, used_x, used_w);
            if (seat.bet > 0) paint_bet(x + 1, top_y_ + backs_.h, seat.bet, -1);
            if (index == view_.button) {
                paint_dealer(mirrored ? used_x - 4 : used_x + used_w + 1, top_y_ + backs_.h - 1);
            }
        }
    }

    void paint_board() {
        if (view_.stage == Stage::Idle) return;
        const int step = card_.w + gap_;
        const int x = centre_ - (5 * card_.w + 4 * gap_) / 2;
        for (int i = 0; i < 5; ++i) {
            if (static_cast<std::size_t>(i) < view_.board.size()) {
                draw_card(canvas_, x + i * step, board_y_, view_.board[static_cast<std::size_t>(i)], k_, true);
            } else {
                draw_slot(canvas_, x + i * step, board_y_, k_);
            }
        }
    }

    // The pot, or how the hand ended.
    void paint_middle() {
        if (view_.stage == Stage::Idle) {
            canvas_.put_centred(centre_, pot_y_, words_.waiting_for_players, color::kFeltText);
            return;
        }
        std::vector<const SeatView*> winners;
        for (const SeatView& seat : view_.seats) {
            if (seat.taken && seat.won > 0) winners.push_back(&seat);
        }
        if (!winners.empty()) {
            std::string text;
            if (winners.size() == 1) {
                text = fill(words_.wins, {winners[0]->name, chips_text(winners[0]->won)});
            } else {
                std::string list;
                for (const SeatView* seat : winners) {
                    if (!list.empty()) list += ", ";
                    list += seat->name + " +" + chips_text(seat->won);
                }
                text = fill(words_.split, {list});
            }
            canvas_.put_centred(centre_, pot_y_, fit_text(text, tx1_ - tx0_ - 2 * rim_ - 4), color::kYellow, kKeep, true);
            return;
        }
        if (view_.pot <= 0) return;
        canvas_.put_right(centre_ - 3, pot_y_, words_.pot, color::kFeltText);
        canvas_.put(centre_ - 2, pot_y_, "●", color::kRed);
        canvas_.put(centre_ - 1, pot_y_, "●", color::kBlue);
        canvas_.put(centre_, pot_y_, "●", color::kYellow);
        canvas_.put(centre_ + 2, pot_y_, chips_text(view_.pot), color::kWhite, kKeep, true);
    }

    // The viewer: avatar to the left of the cards, the block to the left of the avatar.
    void paint_hero() {
        const int index = view_.viewer;
        if (index < 0 || static_cast<std::size_t>(index) >= view_.seats.size()) return;
        const SeatView& seat = view_.seats[static_cast<std::size_t>(index)];
        if (!seat.taken) return;

        paint_seat(index, hero_avatar_x_, hero_y_ + card_.h - ah_, hero_info_x_, hero_y_ + card_.h - 3, kInfoWidth);
        if (seat.bet > 0) paint_bet(centre_, hero_y_ - (k_ == 1 ? 1 : 2), seat.bet, 0);
        if (index == view_.button) paint_dealer(hero_x_ + hero_w_ + 2, hero_y_);
        if (!seat.in_hand || !seat.cards) return;

        draw_card(canvas_, hero_x_, hero_y_, (*seat.cards)[0], k_, false);
        draw_card(canvas_, hero_x_ + card_.w + gap_, hero_y_, (*seat.cards)[1], k_, false);
        if (seat.folded) {
            canvas_.fade(hero_x_, hero_y_, hero_w_, card_.h, color::kBackground, 0.6);
            return;
        }
        if (look_.hint == Hint::Off) return;
        const std::string name = describe_cards(*seat.cards, view_.board, look_.lang);
        const int x = hero_x_ + hero_w_ + 3;
        const bool more = look_.hint == Hint::Full && !look_.hint_more.empty();
        // With nothing to name before the flop the chances move up into the empty line.
        if (more && name.empty()) {
            canvas_.put(x, hero_y_ + card_.h - 2, fit_text(look_.hint_more, w_ - x - 1), color::kDim);
            return;
        }
        canvas_.put(x, hero_y_ + card_.h - 2, fit_text(name, w_ - x - 1), color::kText);
        if (more) canvas_.put(x, hero_y_ + card_.h - 1, fit_text(look_.hint_more, w_ - x - 1), color::kDim);
    }

    // ---------------------------------------------------------------- buttons
    // A button three rows high with its key on the left. Returns its width.
    int paint_button(int x, int y, std::string_view key, std::string_view text, Rgb colour, bool picked, int min_w) {
        const int width = std::max(min_w, text_width(text) + 7);
        const Rgb face = picked ? colour : mix(colour, color::kBackground, 0.55);
        const Rgb key_back = mix(face, Rgb{0, 0, 0}, 0.35);
        for (int i = 0; i < width; ++i) {
            canvas_.put(x + i, y, "▄", face);
            canvas_.put(x + i, y + 2, "▀", face);
        }
        canvas_.fill(x, y + 1, width, 1, face);
        canvas_.put(x + 1, y + 1, " " + std::string(key) + " ", color::kWhite, key_back, true);
        canvas_.put(x + 5, y + 1, text, picked ? color::kWhite : color::kButtonText, face, picked);
        return width;
    }

    bool bet_in_front() const {
        return std::any_of(view_.seats.begin(), view_.seats.end(), [](const SeatView& seat) { return seat.bet > 0; });
    }

    void paint_actions() {
        const int y = h_ - 6;
        const int x0 = std::max(hero_info_x_, 20);
        const int left = x0 - 18;
        const SeatView* me = nullptr;
        if (view_.viewer >= 0 && static_cast<std::size_t>(view_.viewer) < view_.seats.size()) {
            me = &view_.seats[static_cast<std::size_t>(view_.viewer)];
        }

        if (view_.choices && me != nullptr) {
            const game::Choices& choices = *view_.choices;
            canvas_.put(left, y + 1, words_.your_move, color::kYellow, kKeep, true);
            if (const std::string clock = clock_text(); !clock.empty()) {
                const bool late = view_.turn_ms == 0;
                canvas_.put(left, y + 2, clock, late ? color::kRed : color::kDim, kKeep, late);
            }
            int x = x0;
            if (!choices.can_check) {
                x += paint_button(x, y, "F", words_.fold, color::kFold, !look_.raising && look_.focus == 0, 13) + 2;
            }
            std::string call = std::string(words_.check);
            if (!choices.can_check) {
                const bool whole_stack = choices.to_call >= me->stack;
                call = fill(whole_stack ? words_.all_in : words_.call, {chips_text(choices.to_call)});
            }
            x += paint_button(x, y, "C", call, color::kCall, !look_.raising && look_.focus == 1, 13) + 2;
            if (choices.can_raise) {
                const bool raise = bet_in_front();
                std::string label = std::string(raise ? words_.raise : words_.bet);
                if (look_.raising) {
                    const std::string amount = look_.typed.empty() ? chips_text(look_.raise_to) : look_.typed + "_";
                    const bool whole_stack = look_.typed.empty() && look_.raise_to >= choices.max_raise_to;
                    label = fill(whole_stack ? words_.all_in : raise ? words_.raise_to : words_.bet_to, {amount});
                }
                x += paint_button(x, y, "R", label, color::kRaise, look_.raising || look_.focus == 2, 13) + 3;
                const std::string_view sizes[] = {words_.size_half, words_.size_two_thirds, words_.size_pot,
                                                  words_.size_all_in};
                bool matched = false;
                for (int i = 0; i < 4; ++i) {
                    // When several sizes come to the same amount, the first one is lit.
                    const bool picked = !matched && look_.raising && look_.typed.empty() &&
                                        sized_raise(view_, i) == look_.raise_to;
                    matched = matched || picked;
                    const std::string chip = " " + std::string(sizes[i]) + " ";
                    canvas_.put(x, y + 1, chip, picked ? color::kWhite : color::kDim,
                                picked ? mix(color::kRaise, color::kBackground, 0.4) : color::kChip, picked);
                    x += text_width(chip) + 1;
                }
                if (look_.raising) {
                    const std::string limits = fill(words_.raise_limits, {chips_text(choices.min_raise_to),
                                                                          chips_text(choices.max_raise_to)});
                    const std::string hint = std::string(words_.raise_keys) + "    " + limits;
                    canvas_.put(left, y + 3, fit_text(hint, w_ - left - 1), color::kDim);
                    return;
                }
            }
        } else if (me != nullptr && me->taken && me->away && !me->offline) {
            canvas_.put(left, y + 1, fit_text(std::string(words_.away_prompt), w_ - left - 1), color::kYellow, kKeep, true);
        } else if (me != nullptr && me->taken && me->stack == 0 && !me->in_hand) {
            canvas_.put(left, y + 1, words_.out_of_chips, color::kYellow, kKeep, true);
            if (look_.buy_amount > 0) {
                const std::string label = look_.refill ? fill(words_.refill_key, {chips_text(host::kStartBankroll)})
                                                       : fill(words_.buy_in_key, {chips_text(look_.buy_amount)});
                paint_button(left + text_width(words_.out_of_chips) + 3, y, "B", label, color::kCall, true, 13);
            }
        } else if (view_.chips_coming > 0) {
            canvas_.put(left, y + 1, words_.chips_coming, color::kDim);
        }

        // With the panel closed, the latest line of the record shows under the buttons.
        if (panel_w_ == 0 && !view_.log.empty()) paint_log_line(left, y + 3, view_.log.back(), w_ - left - 1);
    }

    // ---------------------------------------------------------------- the record and the side panel
    void paint_log_line(int x, int y, const host::LogLine& line, int room, Rgb back = color::kBackground) {
        const std::string text = log_text(line, look_.lang);
        if (line.kind == host::LogLine::Kind::Board) {
            // Board cards in the colours of their suits.
            int at = x;
            for (const game::Card card : line.cards) {
                if (at - x + 4 > room) break;
                at += canvas_.put(at, y, cards_text({card}), suit_colour_on_dark(card.suit), back, true) + 1;
            }
            return;
        }
        if (line.name.empty()) {
            canvas_.put(x, y, fit_text("— " + text + " —", room), color::kDim, back);
            return;
        }
        const Rgb colour = line.bot ? color::kBotTag : color::kHero;
        const int used = canvas_.put(x, y, fit_text(line.name, room), colour, back, true);
        // What somebody says stands out from what they do.
        const Rgb ink = line.kind == host::LogLine::Kind::Says ? color::kYellow : color::kText;
        if (room - used - 1 > 0) canvas_.put(x + used + 1, y, fit_text(text, room - used - 1), ink, back);
    }

    void paint_panel() {
        const int x0 = full_w_ - panel_w_;
        canvas_.fill(x0, 1, panel_w_, h_ - 2, color::kPanel);
        const int x = x0 + 2;
        int y = 2;

        const int index = look_.portrait_seat;
        if (index >= 0 && static_cast<std::size_t>(index) < view_.seats.size() &&
            view_.seats[static_cast<std::size_t>(index)].taken) {
            const SeatView& seat = view_.seats[static_cast<std::size_t>(index)];
            draw_avatar(canvas_, x, y, avatars_.art(seat.avatar, portrait_, seat.bot, name_colour(seat, index)));
            y += portrait_ / 2 + 1;
            const int name_w = canvas_.put(x, y, fit_text(seat.name, portrait_ - 6), color::kWhite, color::kPanel, true);
            if (seat.bot) canvas_.put(x + name_w + 2, y, " BOT ", color::kWhite, color::kBotTag, true);
            canvas_.put(x, y + 1, chips_text(seat.stack), color::kText, color::kPanel);
            bool bright = false;
            canvas_.put(x, y + 2, fit_text(third_line(index, bright), portrait_), color::kYellow, color::kPanel, true);
            y += 4;
        }
        for (int i = 0; i < portrait_; ++i) canvas_.put(x + i, y, "─", color::kRule, color::kPanel);
        ++y;

        // As many of the latest lines as fit, the newest at the bottom.
        const int room = h_ - 2 - y;
        const int count = std::min(room, static_cast<int>(view_.log.size()));
        for (int i = 0; i < count; ++i) {
            const host::LogLine& line = view_.log[view_.log.size() - static_cast<std::size_t>(count - i)];
            paint_log_line(x, y + i, line, portrait_, color::kPanel);
        }
    }

    void paint_bottom() {
        canvas_.fill(0, h_ - 1, full_w_, 1, color::kBar);
        canvas_.put(2, h_ - 1, words_.table_keys, color::kDim, color::kBar);
        if (!look_.flash.empty()) canvas_.put_right(full_w_ - 2, h_ - 1, look_.flash, color::kYellow, color::kBar);
    }

    Canvas& canvas_;
    const TableView& view_;
    const TableLook& look_;
    Avatars& avatars_;
    const Words& words_;

    int full_w_ = 0;    // the whole window
    int w_ = 0;         // the part of it the table takes
    int h_ = 0;
    int k_ = 1;         // card scale
    int a_ = 16;        // avatar box
    int ah_ = 8;
    int portrait_ = 0;
    int panel_w_ = 0;
    bool overlay_ = false;
    Size card_;
    Size backs_;
    int gap_ = 1;
    int side_w_ = 0;    // the column of a side seat
    int rim_ = 3;
    int tx0_ = 0, tx1_ = 0, ty0_ = 0, ty1_ = 0;     // the table, in cells
    double cx_ = 0, cy_ = 0, half_w_ = 0, half_h_ = 0;  // the same in dots
    int radius_ = 0;
    int centre_ = 0;    // the middle column
    int mid_ = 0;       // the middle row
    int top_y_ = 0;     // cards of the top seats
    int board_y_ = 0;
    int pot_y_ = 0;
    int hero_w_ = 0, hero_x_ = 0, hero_y_ = 0, hero_avatar_x_ = 0, hero_info_x_ = 0;
};

}  // namespace

bool table_fits(int width, int height) {
    return width >= kMinWidth && height >= kMinHeight;
}

void draw_too_small(Canvas& canvas) {
    const int w = canvas.width();
    const int h = canvas.height();
    const std::string size = std::to_string(kMinWidth) + "×" + std::to_string(kMinHeight);
    const int y = std::max(0, h / 2 - 3);
    canvas.put_centred(w / 2, y, "POHER", color::kWhite, kKeep, true);
    canvas.put_centred(w / 2, y + 2, "Растяните окно: нужно не меньше " + size, color::kText);
    canvas.put_centred(w / 2, y + 3, "Make the window bigger: at least " + size, color::kDim);
    canvas.put_centred(w / 2, y + 4, std::to_string(w) + "×" + std::to_string(h), color::kYellow);
}

void draw_table(Canvas& canvas, const TableView& view, const TableLook& look, Avatars& avatars) {
    const int w = canvas.width();
    const int h = canvas.height();
    if (!table_fits(w, h)) {
        draw_too_small(canvas);
        return;
    }
    if (look.panel) {
        for (const Tier& tier : kWithPanel) {
            if (w >= tier.min_w && h >= tier.min_h) {
                Painter(canvas, view, look, avatars, tier, false).paint();
                return;
            }
        }
        // Too narrow for a table next to the panel: the panel covers the right of the table.
        Tier tier = kPlain[std::size(kPlain) - 1];
        tier.portrait = kWithPanel[std::size(kWithPanel) - 1].portrait;
        Painter(canvas, view, look, avatars, tier, true).paint();
        return;
    }
    for (const Tier& tier : kPlain) {
        if (w >= tier.min_w && h >= tier.min_h) {
            Painter(canvas, view, look, avatars, tier, false).paint();
            return;
        }
    }
}

Chips sized_raise(const TableView& view, int size) {
    if (!view.choices || view.viewer < 0) return 0;
    const game::Choices& choices = *view.choices;
    if (size >= 3) return choices.max_raise_to;
    const Chips mine = view.seats[static_cast<std::size_t>(view.viewer)].bet;
    // The pot as it will be once the bet in front is called; the raise adds a part of that on top.
    const Chips matched = mine + choices.to_call;
    const Chips pot = view.pot + choices.to_call;
    const double part = size == 0 ? 0.5 : size == 1 ? 2.0 / 3.0 : 1.0;
    const Chips to = matched + static_cast<Chips>(std::llround(part * static_cast<double>(pot)));
    return std::clamp(to, choices.min_raise_to, choices.max_raise_to);
}

}  // namespace poher::ui
