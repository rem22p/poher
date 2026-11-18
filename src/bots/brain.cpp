#include "bots/brain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "bots/preflop.hpp"
#include "bots/talk.hpp"
#include "game/outs.hpp"

namespace poher::bots {

using game::Action;
using game::Card;
using game::Move;
using game::Street;

namespace {

constexpr int kSamples = 400;

double lerp(double from, double to, double part) {
    return from + (to - from) * part;
}

// A random number around zero, mostly within -1..1: four dice rather than one.
double bell(game::Random& random) {
    return (random.unit() + random.unit() + random.unit() + random.unit() - 2.0) * 1.73;
}

// The share of starting hands a solid player raises with when nobody has
// entered the pot yet, by the number of players still to act after them.
double base_open(int after, int dealt) {
    if (dealt == 2) return after == 1 ? 0.72 : 0.5;
    switch (after) {
        case 0: return 0.20;  // the big blind raising those who just called
        case 1: return 0.40;  // the small blind
        case 2: return 0.42;  // the button
        case 3: return 0.27;
        case 4: return 0.20;
        default: return 0.15;
    }
}

// Rounds to a number a person would bet: whole blinds while the amount is
// small, then two leading digits ("140", "1 500"), by fives when they are big ("75", "450").
Chips human_amount(Chips amount, Chips big_blind) {
    if (amount < 20 * big_blind) {
        const Chips unit = amount < 6 * big_blind ? std::max<Chips>(big_blind / 2, 1) : big_blind;
        return (amount + unit / 2) / unit * unit;
    }
    Chips scale = 1;
    while (amount / scale >= 100) scale *= 10;
    const Chips unit = amount / scale >= 40 ? scale * 5 : scale;
    return (amount + unit / 2) / unit * unit;
}

Action raise_to(const Spot& spot, Chips to) {
    const game::Choices& choices = spot.choices;
    to = std::clamp(human_amount(to, spot.big_blind), choices.min_raise_to, choices.max_raise_to);
    // With most of the stack going in anyway the rest follows.
    if ((to - spot.bet) * 10 >= (choices.max_raise_to - spot.bet) * 7) to = choices.max_raise_to;
    return Action{Move::Raise, to};
}

// A bet or raise of the given part of the pot.
Action bet(const Spot& spot, double part) {
    const Chips matched = spot.bet + spot.choices.to_call;
    const Chips pot_after_call = spot.pot + spot.choices.to_call;
    return raise_to(spot, matched + static_cast<Chips>(std::llround(part * static_cast<double>(pot_after_call))));
}

// Whatever was decided, the move must be one the rules allow.
Action legal(Action action, const game::Choices& choices) {
    switch (action.move) {
        case Move::Fold:
            if (choices.can_check) action.move = Move::Check;
            break;
        case Move::Check:
            if (!choices.can_check) action.move = Move::Fold;
            break;
        case Move::Call:
            if (choices.can_check) action.move = Move::Check;
            break;
        case Move::Bet:
        case Move::Raise:
            if (!choices.can_raise) {
                action.move = choices.can_check ? Move::Check : Move::Call;
            } else {
                action.move = Move::Raise;
                action.to = std::clamp(action.to, choices.min_raise_to, choices.max_raise_to);
            }
            break;
    }
    return action;
}

}  // namespace

Spot spot_of(const game::Hand& hand) {
    Spot spot;
    const auto& players = hand.players();
    const auto index = static_cast<std::size_t>(hand.to_act());
    const game::HandPlayer& me = players[index];
    spot.seat = me.seat;
    spot.cards = me.cards;
    spot.board = hand.board();
    spot.street = hand.street();
    spot.pot = hand.pot();
    spot.bet = me.bet;
    spot.stack = me.stack;
    spot.big_blind = hand.big_blind();
    spot.choices = hand.choices();
    // After the flop the button acts last; we are last if nobody between us
    // and the button can still bet.
    spot.last = true;
    for (std::size_t i = index; i != hand.button();) {
        i = (i + 1) % players.size();
        if (!players[i].folded && !players[i].all_in) {
            spot.last = false;
            break;
        }
    }
    return spot;
}

Brain::Brain(const Character& who, std::uint64_t seed) : who_(&who), random_(seed) {
    if (who_->gears.size() > 1) {
        gear_ = random_.below(static_cast<std::uint32_t>(who_->gears.size()));
        shift_in_ = who_->shift_min +
                    static_cast<int>(random_.below(static_cast<std::uint32_t>(who_->shift_max - who_->shift_min + 1)));
    }
}

Style Brain::style() const {
    Style style = who_->gears[gear_];
    // Steaming: more hands, more bets, less sense.
    const double steam = tilt_;
    style.loose *= 1 + 0.4 * steam;
    style.aggr = std::min(1.0, style.aggr + 0.2 * steam);
    style.bluff = std::min(0.6, style.bluff * (1 + steam) + 0.04 * steam);
    style.sticky = std::min(1.0, style.sticky + 0.2 * steam);
    style.fear *= 1 - 0.7 * steam;
    style.trap *= 1 - 0.5 * steam;
    style.size *= 1 + 0.25 * steam;
    style.noise += 0.03 * steam;
    // Ahead for the evening: some sit on it.
    const double ahead = style.guard * std::clamp((session_ - 60) / 100, 0.0, 1.0);
    style.loose *= 1 - 0.25 * ahead;
    style.bluff *= 1 - 0.4 * ahead;
    return style;
}

Decision Brain::decide(const Spot& spot, const Watch& watch) {
    const Style style = this->style();
    Decision decision =
        spot.street == Street::Preflop ? before_flop(spot, watch, style) : after_flop(spot, watch, style);
    decision.action = legal(decision.action, spot.choices);
    decision.effort = std::clamp(decision.effort, 0.0, 1.0);
    return decision;
}

int Brain::hello() {
    const Style& base = who_->gears[gear_];
    if (!random_.chance(0.7 * base.talk)) return -1;
    quiet_ = 0;
    return pick_phrase(who_->name, Occasion::Hello, random_);
}

int Brain::hand_over(const Outcome& outcome) {
    const Style& base = who_->gears[gear_];
    session_ += outcome.won;
    ++record_.hands;
    if (outcome.showdown) {
        ++record_.showdowns;
        record_.at_showdown += outcome.won;
    } else {
        record_.without += outcome.won;
    }
    // Losing as the favourite hurts more.
    const bool unlucky = outcome.won <= -25 && outcome.showdown && peak_ >= 0.75;
    if (outcome.won <= -25) {
        tilt_ += 0.6 * base.tilt * std::clamp(-outcome.won / 80, 0.3, 1.0) * (unlucky ? 1.4 : 1.0);
    } else if (outcome.won >= 25) {
        tilt_ *= 0.6;
    }
    // It wears off: half of it is gone in eight hands.
    tilt_ = std::clamp(tilt_ * 0.92, 0.0, 1.0);

    // Something to say? Rarely, and not twice in a row.
    int line = -1;
    ++quiet_;
    if (quiet_ > 5 && base.talk > 0) {
        Occasion occasion = Occasion::Hello;
        double chance = 0;
        if (outcome.won >= 30 && outcome.showdown) {
            occasion = Occasion::WonBig;
            chance = 0.3;
        } else if (outcome.won >= 12 && !outcome.showdown && fired_) {
            occasion = Occasion::Bluffed;
            chance = 0.3;
        } else if (outcome.won <= -30 && unlucky) {
            occasion = Occasion::BadBeat;
            chance = 0.5;
        } else if (outcome.won <= -30) {
            occasion = Occasion::LostBig;
            chance = 0.3;
        } else if (outcome.biggest >= 60 && outcome.won <= 0 && outcome.won > -10) {
            occasion = Occasion::NiceHand;
            chance = 0.1;
        }
        if (chance > 0 && random_.chance(chance * base.talk)) {
            line = pick_phrase(who_->name, occasion, random_);
            if (line >= 0) quiet_ = 0;
        }
    }

    trapping_ = false;
    had_draw_ = false;
    fired_ = false;
    peak_ = 0;

    if (who_->gears.size() > 1 && --shift_in_ <= 0) {
        const auto others = static_cast<std::uint32_t>(who_->gears.size() - 1);
        gear_ = (gear_ + 1 + random_.below(others)) % who_->gears.size();
        shift_in_ = who_->shift_min +
                    static_cast<int>(random_.below(static_cast<std::uint32_t>(who_->shift_max - who_->shift_min + 1)));
    }
    return line;
}

// The share of starting hands an opponent is believed to play the way they did.
double Brain::believed_range(const Line& line, const Watch& watch, const Style& style) const {
    if (!line.entered) return 1;  // a blind who has not chosen anything
    double range = 0.02;
    switch (line.level) {
        case 0: range = 0.6; break;  // just called the big blind
        case 1: range = line.raised ? base_open(line.after, watch.dealt()) : (line.after == 0 ? 0.45 : 0.3); break;
        case 2: range = line.raised ? 0.07 : 0.12; break;
        case 3: range = line.raised ? 0.03 : 0.045; break;
        default: break;
    }
    // A short stack goes all-in with much more.
    if (line.shove > 0) {
        if (line.shove <= 8) {
            range = std::max(range, 0.45);
        } else if (line.shove <= 15) {
            range = std::max(range, 0.25);
        } else if (line.shove <= 25) {
            range = std::max(range, 0.12);
        }
    }
    // What is remembered: someone who raises every other hand has less each time.
    const Notes* notes = watch.notes(line.seat);
    if (notes != nullptr && notes->hands >= 12) {
        const double sure = style.adapt * std::min(1.0, notes->hands / (60 - 45 * style.adapt));
        const double habit = line.raised ? std::clamp(notes->raises() / 0.18, 0.5, 2.5)
                                         : std::clamp(notes->enters() / 0.28, 0.5, 2.5);
        range *= lerp(1, habit, sure);
    }
    // A poor reader hardly narrows it down at all.
    return std::pow(std::min(range, 1.0), style.read);
}

std::vector<Foe> Brain::foes(const Spot& spot, const Watch& watch, const Style& style, bool only_committed) const {
    std::vector<Foe> out;
    const Chips to_match = spot.bet + spot.choices.to_call;
    for (const Line& line : watch.lines()) {
        if (line.seat == spot.seat || line.folded) continue;
        if (only_committed && !line.all_in && line.bet < to_match) continue;
        Foe foe;
        foe.range = believed_range(line, watch, style);
        // A bet from someone who rarely bets means more than one from a maniac.
        double trust = 1;
        const Notes* notes = watch.notes(line.seat);
        if (notes != nullptr && notes->hands >= 12) {
            const double sure = style.adapt * std::min(1.0, notes->hands / (60 - 45 * style.adapt));
            trust = lerp(1, std::clamp(1.6 - 0.3 * notes->pushes(), 0.6, 1.4), sure);
        }
        foe.story = line.story * style.read * trust;
        out.push_back(foe);
    }
    return out;
}

// How often the opponents still in the hand fold to a bet, as far as the bot remembers and cares.
double Brain::fold_habit(const Spot& spot, const Watch& watch, const Style& style) const {
    double sum = 0;
    int count = 0;
    for (const Line& line : watch.lines()) {
        if (line.seat == spot.seat || line.folded) continue;
        double habit = 0.45;
        const Notes* notes = watch.notes(line.seat);
        if (notes != nullptr && notes->faced >= 8) {
            const double sure = style.adapt * std::min(1.0, notes->faced / (40 - 30 * style.adapt));
            habit = lerp(0.45, notes->gives_up(), sure);
        }
        sum += habit;
        ++count;
    }
    return count > 0 ? sum / count : 0.45;
}

double Brain::pick_part(const Style& style, double single, bool bluffing, int to_come) {
    // What a player who gives nothing away would pick.
    static constexpr double kParts[] = {0.33, 0.5, 0.66, 0.75, 1.0};
    static constexpr double kWeights[] = {2, 3, 3, 1.5, 1};
    double point = random_.unit() * 10.5;
    double even = kParts[4];
    for (int i = 0; i < 5; ++i) {
        if (point < kWeights[i]) {
            even = kParts[i];
            break;
        }
        point -= kWeights[i];
    }
    // An honest player sizes by the hand: small with little, big with a lot.
    const double strength = bluffing ? 0.0 : std::clamp((single - 0.55) / 0.4, 0.0, 1.0);
    double part = lerp(even, 0.33 + 0.67 * strength, style.honest) * style.size;
    if (to_come == 0 && !bluffing && single >= 0.9 && random_.chance(0.15 * style.aggr)) part = 1.25;

    static constexpr double kNamed[] = {0.25, 0.33, 0.5, 0.66, 0.75, 1.0, 1.25, 1.5};
    double best = kNamed[0];
    for (const double named : kNamed) {
        if (std::abs(named - part) < std::abs(best - part)) best = named;
    }
    return best;
}

Decision Brain::before_flop(const Spot& spot, const Watch& watch, const Style& style) {
    const game::Choices& choices = spot.choices;
    const Line* me = watch.line(spot.seat);
    const Chips bb = spot.big_blind;
    const int dealt = watch.dealt();
    const int after = me != nullptr ? me->after : 0;
    const int raises = watch.raises();
    const Chips all = spot.stack + spot.bet;
    const double stack_bb = static_cast<double>(all) / static_cast<double>(bb);
    const Chips to_match = spot.bet + choices.to_call;

    const double real = start_strength(spot.cards);
    // How good the hand feels. Some love any ace, any pair, anything suited.
    double felt = real;
    {
        const Card a = spot.cards[0];
        const Card b = spot.cards[1];
        double bonus = 0;
        if (a.rank == b.rank) bonus += 0.15;
        if (a.suit == b.suit) bonus += 0.10;
        if (a.rank == game::kAce || b.rank == game::kAce) bonus += 0.12;
        if (std::abs(a.rank - b.rank) == 1 && std::min(a.rank, b.rank) >= 5) bonus += 0.06;
        felt = std::max(real, std::min(0.93, real + bonus * style.fancy));
        felt = std::clamp(felt + bell(random_) * (0.015 + 0.3 * style.noise), 0.0, 1.0);
    }
    const bool premium = real >= 0.975;  // the top fortieth: big pairs, ace-king suited
    const double open = std::min(0.92, base_open(after, dealt) * style.loose);
    const bool small_blind = after == 1 && dealt > 2;
    const bool big_blind = after == 0;

    const auto fold = [&](double range) {
        // The further below the line, the less there is to think about.
        const double near = std::clamp(1 - ((1 - range) - felt) / 0.2, 0.0, 1.0);
        return Decision{Action{choices.can_check ? Move::Check : Move::Fold}, 0.02 + 0.12 * near + some(0, 0.05)};
    };
    const auto push = [&] { return choices.can_raise ? Action{Move::Raise, choices.max_raise_to} : Action{Move::Call}; };

    // A call that takes two fifths of the stack or more is a decision for all of it.
    const bool committed = choices.to_call > 0 && choices.to_call * 5 >= spot.stack * 2;
    if (committed || (stack_bb <= 12 && raises > 0)) return all_in_spot(spot, watch, style);

    if (raises == 0) {
        const int limpers = watch.limpers();
        if (stack_bb <= 12) {
            // Too short to play poker: all of it or nothing.
            if (choices.can_raise && felt >= 1 - std::min(0.9, open * 1.2)) return {push(), some(0.2, 0.35)};
            return fold(open);
        }
        const double raise_range = open * (limpers > 0 ? 0.7 : 1.0) * std::max(style.pre_raise, 0.08);
        if (choices.can_raise && felt >= 1 - raise_range) {
            // A trapper sometimes just calls with the very best, to raise again later.
            if (premium && !choices.can_check && !trapping_ && random_.chance(style.trap * 0.35)) {
                trapping_ = true;
                return {Action{Move::Call}, some(0.15, 0.3)};
            }
            double times = 1.6 + 1.4 * style.size;
            if (random_.chance(0.25)) times += random_.unit() * (style.size > 1.1 ? 2.0 : 0.5);
            if (premium) times += 0.8 * style.honest;
            if (dealt == 2) times -= 0.5;
            const Chips to = static_cast<Chips>(std::llround(times * static_cast<double>(bb))) + limpers * bb;
            return {raise_to(spot, to), some(0.1, 0.25)};
        }
        if (choices.can_check) return {Action{Move::Check}, some(0.03, 0.12)};
        double call_range = open;
        if (choices.to_call < bb) {
            call_range = std::min(0.95, open * 1.5);  // half a bet to complete
        } else if (limpers > 0) {
            call_range = std::min(0.95, open * 1.1);
        }
        // Someone who always comes in with a raise does not call first in.
        const bool may_call = limpers > 0 || style.pre_raise < 0.9;
        if (may_call && felt >= 1 - call_range) return {Action{Move::Call}, some(0.1, 0.22)};
        return fold(open);
    }

    // How wide the raiser raises. Reading skill is left out here: how loosely
    // the bot answers is already in its style.
    Style reader = style;
    reader.read = 1;
    const Line* raiser = watch.line(watch.opener());
    const double theirs = raiser != nullptr ? believed_range(*raiser, watch, reader) : 0.25;
    int callers = 0;
    for (const Line& line : watch.lines()) {
        if (line.seat != spot.seat && !line.folded && line.seat != watch.opener() && line.bet >= to_match) ++callers;
    }
    const bool mine = me != nullptr && me->raised;  // we raised and were raised back

    if (raises == 1) {
        const double raise_bb = static_cast<double>(to_match) / static_cast<double>(bb);
        double call_range = theirs * (big_blind ? 1.25 : small_blind ? 0.6 : after == 2 ? 0.95 : 0.75);
        call_range *= std::pow(style.loose, 0.8) * (1 + 0.12 * callers);
        // A bigger raise is called with less, unless the player cannot let go.
        call_range *= std::pow(3.0 / std::max(raise_bb, 2.0), 0.6 * (1 - 0.7 * style.sticky));
        if (me != nullptr && me->entered) call_range *= 1.15;
        call_range = std::min(call_range, 0.85);
        const double value_range =
            std::min(call_range, theirs * (0.18 + 0.2 * style.aggr) * std::sqrt(std::max(style.pre_raise, 0.05)));

        const bool for_value = felt >= 1 - value_range;
        const bool as_bluff = !for_value && felt >= 1 - std::min(0.6, theirs * 1.8) && random_.chance(style.bluff * 0.3);
        if (choices.can_raise && (for_value || as_bluff)) {
            if (for_value && premium && !trapping_ && random_.chance(style.trap * 0.35)) {
                trapping_ = true;
                return {Action{Move::Call}, some(0.2, 0.35)};
            }
            double times = (big_blind || small_blind ? 3.6 : 3.0) * (0.85 + 0.15 * style.size);
            Chips to = static_cast<Chips>(std::llround(times * static_cast<double>(to_match))) +
                       callers * to_match * 4 / 5;
            if (to * 5 >= all * 2) to = choices.max_raise_to;
            return {raise_to(spot, to), some(0.28, 0.45)};
        }
        if (felt >= 1 - call_range) return {Action{Move::Call}, some(0.15, 0.3)};
        return fold(call_range);
    }

    // Raised and raised again. How wide the last raiser does that, next to the usual.
    const double usual = raises == 2 ? 0.07 : 0.03;
    const double wider = std::pow(std::clamp(theirs / usual, 0.6, 2.5), 0.7);
    if (raises == 2) {
        const double value_range = (0.022 + 0.02 * style.aggr) * std::sqrt(std::max(style.pre_raise, 0.05)) * wider;
        const double call_range =
            (0.07 * std::pow(style.loose, 0.7) * (mine ? 1.3 : 0.8) * (after == 2 ? 1.15 : 1.0) + 0.05 * style.sticky) *
            wider;
        const bool for_value = felt >= 1 - value_range;
        const bool as_bluff = !for_value && felt >= 0.85 && random_.chance(style.bluff * 0.12);
        if (choices.can_raise && (for_value || as_bluff)) {
            Chips to = static_cast<Chips>(std::llround(2.3 * static_cast<double>(to_match)));
            if (to * 5 >= all * 2) to = choices.max_raise_to;
            return {raise_to(spot, to), some(0.35, 0.55)};
        }
        if (felt >= 1 - call_range) return {Action{Move::Call}, some(0.3, 0.5)};
        return fold(call_range);
    }
    const double value_range = 0.02 * (0.6 + 0.8 * style.aggr) * wider;
    const double call_range = (0.03 * std::pow(style.loose, 0.5) + 0.04 * style.sticky) * wider;
    if (felt >= 1 - value_range) return {push(), some(0.3, 0.5)};
    if (felt >= 1 - call_range) return {Action{Move::Call}, some(0.4, 0.6)};
    return fold(call_range);
}

// Before the flop, with all the chips (or most of them) at stake: the chance
// to win against the hands that put them in, set against the price.
Decision Brain::all_in_spot(const Spot& spot, const Watch& watch, const Style& given) {
    const game::Choices& choices = spot.choices;
    // Even a poor reader knows that all the chips rarely go in with rubbish.
    Style style = given;
    style.read = 0.5 + 0.5 * given.read;
    std::vector<Foe> against = foes(spot, watch, style, true);
    if (against.empty()) against = foes(spot, watch, style, false);
    const double equity = equity_against(spot.cards, {}, against, kSamples, random_);
    const double felt = std::clamp(equity + bell(random_) * style.noise, 0.0, 1.0);
    const double price =
        static_cast<double>(choices.to_call) / static_cast<double>(spot.pot + choices.to_call);
    const double need = price + 0.02 + 0.08 * style.fear - 0.05 * style.sticky;
    const double close = 1 - std::clamp(std::abs(felt - need) / 0.15, 0.0, 1.0);
    const double effort = 0.25 + 0.5 * close + some(0, 0.1);
    if (felt < need) return {Action{Move::Fold}, effort};
    peak_ = std::max(peak_, felt);
    // Chips left after a call would go in on any flop: put them in now.
    if (choices.can_raise && choices.to_call < spot.stack) return {Action{Move::Raise, choices.max_raise_to}, effort};
    return {Action{Move::Call}, effort};
}

Decision Brain::after_flop(const Spot& spot, const Watch& watch, const Style& style) {
    const game::Choices& choices = spot.choices;
    const std::vector<Foe> against = foes(spot, watch, style, false);
    const int count = std::max<int>(1, static_cast<int>(against.size()));
    const double equity = equity_against(spot.cards, spot.board, against, kSamples, random_);
    const double felt = std::clamp(equity + bell(random_) * style.noise, 0.0, 1.0);
    // The same strength as if measured against one opponent.
    const double single = std::pow(felt, 1.0 / count);

    const int to_come = spot.street == Street::Flop ? 2 : spot.street == Street::Turn ? 1 : 0;
    const game::Outs outs = game::count_outs(spot.cards, spot.board);
    const bool draw = to_come > 0 && (outs.flush_draw || outs.open_ended || outs.count >= 8);
    const bool half_draw = to_come > 0 && !draw && (outs.gutshot || outs.count >= 4);
    const bool holding = fade_rate(spot.cards, spot.board) <= 0.8;  // a pair of our own, a real draw, or better
    if (draw) had_draw_ = true;

    const double folders = fold_habit(spot, watch, style);
    const double pot = static_cast<double>(spot.pot);

    if (choices.can_check) {
        if (!choices.can_raise) return {Action{Move::Check}, some(0.03, 0.1)};

        // Bet for value: strong enough to be called by worse. Those who call a lot get bet thinner.
        const double value_line = 0.60 + 0.03 * (2 - to_come) + 0.10 * (1 - style.aggr) - 0.25 * (0.45 - folders);
        if (single >= value_line) {
            const bool monster = single >= 0.88;
            if (monster && to_come > 0 && random_.chance(style.trap * (spot.last ? 0.5 : 0.9))) {
                trapping_ = true;
                return {Action{Move::Check}, some(0.2, 0.35)};
            }
            const bool thin = single < value_line + 0.10;
            if (random_.chance(thin ? 0.4 + 0.5 * style.aggr : 0.8 + 0.2 * style.aggr)) {
                peak_ = std::max(peak_, single);
                return {bet(spot, pick_part(style, single, false, to_come)),
                        some(0.2, 0.35) + (to_come == 0 ? 0.1 : 0.0)};
            }
            return {Action{Move::Check}, some(0.18, 0.3)};
        }

        // Not enough for that. The reasons to bet anyway:
        const double crowd = count == 1 ? 1.0 : count == 2 ? 0.5 : 0.15;
        double chance = 0;
        if (draw) {
            chance = (0.2 + 0.55 * style.aggr) * (count >= 3 ? 0.6 : 1.0);
        } else if (half_draw) {
            chance = (0.05 + 0.3 * style.aggr) * crowd;
        }
        // The raiser from before the flop keeps telling the same story.
        if (spot.street == Street::Flop && watch.opener() == spot.seat) {
            chance = std::max(chance, (0.25 + 0.5 * style.aggr) * crowd);
        }
        double bluff = style.bluff * crowd * std::clamp(folders / 0.45, 0.4, 2.0);
        if (spot.last) bluff *= 1.5;                   // everybody checked to us
        if (to_come == 0 && had_draw_) bluff *= 1.6;   // the draw missed: only the story is left
        if (fired_) bluff *= 1.5;                      // bet before and got called: fire again
        if (single > 0.45) bluff *= 0.3;               // a hand that may win at showdown is not thrown away as a bluff
        chance = 1 - (1 - chance) * (1 - std::min(bluff, 0.9));
        if (random_.chance(chance)) {
            const Action action = bet(spot, pick_part(style, single, true, to_come));
            // A bluff that takes most of the stack is rarely worth it.
            if ((action.to - spot.bet) * 2 <= spot.stack || random_.chance(style.bluff * 0.5)) {
                fired_ = true;
                return {action, some(0.25, 0.45)};
            }
        }
        return {Action{Move::Check}, some(0.06, 0.18) + (single > 0.4 ? 0.08 : 0.0)};
    }

    // There is a bet to answer.
    const double price = static_cast<double>(choices.to_call) / (pot + static_cast<double>(choices.to_call));
    bool more_betting = false;
    for (const Line& line : watch.lines()) {
        if (line.seat != spot.seat && !line.folded && !line.all_in) more_betting = true;
    }
    // A call that ends the betting needs no more than the price.
    const bool final = to_come == 0 || !more_betting || choices.to_call >= spot.stack;
    double need = price + (final ? 0.01 : 0.02 + 0.025 * to_come + (spot.last ? 0.0 : 0.015));
    if (!final && draw) need -= 0.04 + 0.06 * style.chase;
    if (!final && half_draw) need -= 0.05 * style.chase;
    need *= 1 - (holding ? 0.35 : 0.1) * style.sticky;
    // The bet as a part of the pot it was made into.
    const double part = static_cast<double>(choices.to_call) / std::max(pot - static_cast<double>(choices.to_call), 1.0);
    const bool big = part >= 0.6 || choices.to_call * 10 >= spot.stack * 3;
    if (big) need += style.fear * (0.10 + (single < 0.85 ? 0.10 : 0.0));
    need = std::clamp(need, 0.02, 0.95);

    const double close = 1 - std::clamp(std::abs(felt - need) / 0.2, 0.0, 1.0);
    const double weight = std::clamp(2.0 * static_cast<double>(choices.to_call) / std::max<double>(spot.stack, 1), 0.0, 1.0);
    const double effort =
        0.12 + 0.4 * close + 0.3 * weight * (0.5 + 0.5 * close) + (to_come == 0 ? 0.08 : 0.0) + some(0, 0.08);

    const auto raise = [&](bool bluffing) {
        static constexpr double kParts[] = {0.5, 0.66, 0.66, 0.75, 1.0};
        const double part_of_pot = kParts[random_.below(5)] * style.size;
        const Action action = bet(spot, part_of_pot);
        if (bluffing && (action.to - spot.bet) * 2 > spot.stack && !random_.chance(style.bluff * 0.5)) {
            return Action{Move::Fold};  // too dear for a bluff
        }
        return action;
    };

    const double raise_line = 0.80 + 0.12 * (1 - style.aggr) + (watch.raises() >= 2 ? 0.07 : 0.0);
    if (choices.can_raise && single >= raise_line) {
        peak_ = std::max(peak_, single);
        // Keep the trap shut for one more street, or spring one set earlier.
        if (!trapping_ && to_come > 0 && random_.chance(style.trap * 0.6)) {
            trapping_ = true;
            return {Action{Move::Call}, some(0.25, 0.4)};
        }
        return {raise(false), std::max(effort, some(0.3, 0.5))};
    }
    if (choices.can_raise && watch.raises() == 1) {
        const bool with_draw = draw && random_.chance((0.05 + 0.3 * style.aggr) * (count == 1 ? 1.0 : 0.5));
        const bool with_nothing = count == 1 && felt < need && against.front().story < 1.4 &&
                                  random_.chance(style.bluff * 0.2 * std::clamp(folders / 0.45, 0.4, 2.0));
        if (with_draw || with_nothing) {
            const Action action = raise(true);
            if (action.move == Move::Raise) {
                fired_ = true;
                return {action, std::max(effort, some(0.35, 0.55))};
            }
        }
    }
    if (felt >= need) {
        peak_ = std::max(peak_, single);
        return {Action{Move::Call}, effort};
    }
    // "I just had to see it."
    if (final && to_come == 0 && holding && price <= 0.34 && random_.chance(0.3 * (1 - style.read))) {
        return {Action{Move::Call}, std::max(effort, 0.45)};
    }
    return {Action{Move::Fold}, effort};
}

}  // namespace poher::bots
