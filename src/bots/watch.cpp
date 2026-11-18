#include "bots/watch.hpp"

#include <algorithm>

namespace poher::bots {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

// Later streets say more: a river bet is rarely a whim.
double weight_of(game::Street street) {
    switch (street) {
        case game::Street::Turn: return 1.1;
        case game::Street::River: return 1.2;
        default: return 1.0;
    }
}

}  // namespace

void Watch::begin(const game::Hand& hand, const std::vector<std::string>& names) {
    lines_.clear();
    street_ = game::Street::Preflop;
    big_blind_ = hand.big_blind();
    pot_ = 0;
    to_match_ = 0;
    raises_ = 0;
    limpers_ = 0;
    opener_ = -1;
    aggressor_ = -1;

    const auto& players = hand.players();
    const int count = static_cast<int>(players.size());
    for (int i = 0; i < count; ++i) {
        const game::HandPlayer& player = players[static_cast<std::size_t>(i)];
        Line line;
        line.seat = player.seat;
        if (player.seat >= 0 && static_cast<std::size_t>(player.seat) < names.size()) {
            line.name = names[static_cast<std::size_t>(player.seat)];
        }
        // The blinds are already posted; they come again as events.
        line.stack = player.stack + player.total;
        // Steps clockwise from the button: 1 is the small blind, 2 the big one.
        int steps = (i - static_cast<int>(hand.button()) + count) % count;
        if (count == 2) {
            line.after = steps == 0 ? 1 : 0;  // one on one the button is the small blind
        } else {
            if (steps == 0) steps = count;
            line.after = steps == 1 ? 1 : steps == 2 ? 0 : count - steps + 2;
        }
        lines_.push_back(std::move(line));
        if (Notes* notes = page(lines_.back())) ++notes->hands;
    }
}

Line* Watch::find(int seat) {
    for (Line& line : lines_) {
        if (line.seat == seat) return &line;
    }
    return nullptr;
}

const Line* Watch::line(int seat) const {
    for (const Line& line : lines_) {
        if (line.seat == seat) return &line;
    }
    return nullptr;
}

Notes* Watch::page(const Line& line) {
    if (notebook_ == nullptr || line.name.empty()) return nullptr;
    return &notebook_->of(line.name);
}

const Notes* Watch::notes(int seat) const {
    const Line* who = line(seat);
    if (notebook_ == nullptr || who == nullptr || who->name.empty()) return nullptr;
    return notebook_->find(who->name);
}

void Watch::see(const game::Event& event) {
    std::visit(
        Overloaded{
            [&](const game::event::Blind& e) {
                Line* line = find(e.seat);
                if (line == nullptr) return;
                line->stack -= e.amount;
                line->bet += e.amount;
                line->all_in = line->stack == 0;
                pot_ += e.amount;
                to_match_ = std::max(to_match_, line->bet);
            },
            [&](const game::event::Acted& e) {
                Line* line = find(e.seat);
                if (line == nullptr) return;
                Notes* notes = page(*line);
                const Chips pot_before = std::max<Chips>(pot_, 1);
                const double part = static_cast<double>(e.added) / static_cast<double>(pot_before);
                const bool facing = to_match_ > line->bet;
                const bool raise = e.move == game::Move::Bet || e.move == game::Move::Raise;

                if (street_ == game::Street::Preflop) {
                    if (e.move == game::Move::Call || raise) {
                        if (notes != nullptr && !line->entered) ++notes->entered;
                        line->entered = true;
                    }
                    if (e.move == game::Move::Call) {
                        if (raises_ == 0) ++limpers_;
                        line->raised = false;
                        line->level = raises_;
                    }
                    if (raise) {
                        if (notes != nullptr && line->level == 0 && !line->raised) ++notes->raised;
                        ++raises_;
                        line->raised = true;
                        line->level = raises_;
                        opener_ = e.seat;
                        aggressor_ = e.seat;
                        if (e.all_in) {
                            line->shove = static_cast<double>(line->bet + e.added) / static_cast<double>(big_blind_);
                        }
                    }
                } else {
                    if (facing && notes != nullptr) {
                        ++notes->faced;
                        if (e.move == game::Move::Fold) ++notes->gave_up;
                    }
                    if (e.move == game::Move::Check) ++line->checks;
                    if (e.move == game::Move::Call) {
                        if (notes != nullptr) ++notes->calls;
                        line->story += (0.25 + 0.3 * std::min(part, 1.0)) * weight_of(street_);
                    }
                    if (raise) {
                        if (notes != nullptr) ++notes->bets;
                        double weight = e.move == game::Move::Raise ? 1.6 : 1.0;
                        // The raiser from before the flop bets most flops, with or without a hand.
                        if (street_ == game::Street::Flop && e.move == game::Move::Bet && e.seat == opener_) {
                            weight *= 0.55;
                        }
                        line->story += weight * std::clamp(0.4 + 0.9 * part, 0.4, 1.5) * weight_of(street_);
                        ++line->bets;
                        ++raises_;
                        aggressor_ = e.seat;
                    }
                }

                if (e.move == game::Move::Fold) line->folded = true;
                line->stack -= e.added;
                line->bet += e.added;
                line->all_in = line->all_in || e.all_in;
                pot_ += e.added;
                to_match_ = std::max(to_match_, line->bet);
            },
            [&](const game::event::Board& e) {
                street_ = e.street;
                for (Line& line : lines_) line.bet = 0;
                to_match_ = 0;
                raises_ = 0;
                aggressor_ = -1;
            },
            [&](const game::event::Returned& e) {
                if (Line* line = find(e.seat)) {
                    line->stack += e.amount;
                    line->bet = std::max<Chips>(line->bet - e.amount, 0);
                    line->all_in = false;
                }
                pot_ -= e.amount;
            },
            [](const game::event::Dealt&) {},
            [](const game::event::Showed&) {},
            [](const game::event::Mucked&) {},
            [](const game::event::Won&) {},
        },
        event);
}

}  // namespace poher::bots
