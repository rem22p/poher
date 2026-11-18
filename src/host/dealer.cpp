#include "host/dealer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace poher::host {

namespace {

constexpr std::size_t kLogLines = 60;

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

}  // namespace

Pace Pace::none() {
    Pace pace;
    pace.think_min = pace.think_max = 0;
    pace.first_hand = pace.street = pace.run_out = pace.show = pace.win = pace.remark = 0;
    pace.after_hand = pace.after_showdown = 0;
    return pace;
}

Dealer::Dealer(game::Stakes stakes, int seats, std::uint64_t seed, Pace pace)
    : table_(stakes, seats),
      random_(seed),
      pace_(pace),
      who_(static_cast<std::size_t>(seats)),
      shown_(static_cast<std::size_t>(seats)),
      wanted_(static_cast<std::size_t>(seats), 0),
      wanted_most_(static_cast<std::size_t>(seats), 0),
      leaving_(static_cast<std::size_t>(seats), false),
      bank_(static_cast<std::size_t>(seats), 0),
      missed_(static_cast<std::size_t>(seats), 0),
      offline_(static_cast<std::size_t>(seats), false),
      since_(static_cast<std::size_t>(seats), 0),
      brains_(static_cast<std::size_t>(seats)),
      stack_before_(static_cast<std::size_t>(seats), 0) {
    // The first button goes to a seat picked at random.
    table_.set_button(static_cast<int>(random_.below(static_cast<std::uint32_t>(seats))));
}

bool Dealer::sit(int seat, Player who, Chips buy_in) {
    if (!table_.sit(seat, buy_in)) return false;
    who_[at(seat)] = std::move(who);
    brains_[at(seat)].reset();
    if (who_[at(seat)].bot) {
        brains_[at(seat)] = std::make_unique<bots::Brain>(bots::character(who_[at(seat)].name), random_.next());
    }
    shown_[at(seat)] = Shown{};
    shown_[at(seat)].stack = buy_in;
    wanted_[at(seat)] = 0;
    leaving_[at(seat)] = false;
    bank_[at(seat)] = clock_.bank_ms;
    missed_[at(seat)] = 0;
    offline_[at(seat)] = false;
    table_.set_away(seat, false);
    touch();
    return true;
}

int Dealer::people() const {
    int count = 0;
    for (int seat = 0; seat < table_.seat_count(); ++seat) {
        if (table_.seat(seat).taken && !who_[at(seat)].bot) ++count;
    }
    return count;
}

bool Dealer::clock_runs() const {
    return clock_.turn_ms > 0 && people() >= 2;
}

void Dealer::set_offline(int seat, bool offline) {
    if (seat < 0 || seat >= table_.seat_count() || !table_.seat(seat).taken || offline_[at(seat)] == offline) return;
    offline_[at(seat)] = offline;
    since_[at(seat)] = now_;
    missed_[at(seat)] = 0;
    table_.set_away(seat, offline);
    touch();
}

void Dealer::come_back(int seat) {
    if (seat < 0 || seat >= table_.seat_count() || !table_.seat(seat).away || offline_[at(seat)]) return;
    table_.set_away(seat, false);
    missed_[at(seat)] = 0;
    touch();
}

// Away and offline players keep their seat for a while, then get up.
void Dealer::watch_seats() {
    for (int seat = 0; seat < table_.seat_count(); ++seat) {
        const game::Seat& place = table_.seat(seat);
        if (!place.taken || !place.away || leaving_[at(seat)]) continue;
        const int keep = offline_[at(seat)] ? clock_.offline_seat_ms : clock_.away_seat_ms;
        if (now_ - since_[at(seat)] >= keep) leave(seat);
    }
}

// The move of a person: waits while they think, moves for them when they
// are gone or their time is up. True if a move was made.
bool Dealer::run_clock(int seat) {
    game::Hand& hand = *table_.hand();
    const auto move = [&](bool fold) {
        const bool can_check = hand.choices().can_check;
        if (!hand.act(game::Action{fold || !can_check ? game::Move::Fold : game::Move::Check})) {
            hand.act(game::Action{game::Move::Check});
        }
        turn_seat_ = -1;
        queue_events();
        return true;
    };
    // Leaving: the cards go. Offline: a check if it is free, as SPEC 7.4 says.
    if (leaving_[at(seat)] || stopped_) return move(true);
    if (offline_[at(seat)]) return move(false);
    if (turn_seat_ != seat) {
        turn_seat_ = seat;
        turn_started_ = now_;
        touch();  // the clock appears on the screen
    }
    if (!clock_runs()) return false;
    if (now_ < turn_started_ + clock_.turn_ms + bank_[at(seat)] + clock_.grace_ms) return false;
    bank_[at(seat)] = 0;
    if (++missed_[at(seat)] >= clock_.away_after && !table_.seat(seat).away) {
        table_.set_away(seat, true);
        since_[at(seat)] = now_;
    }
    return move(false);
}

void Dealer::tick(std::int64_t now) {
    now_ = std::max(now, now_);
    bool dealt = false;
    for (;;) {
        // Something already happened by the rules but is not on the table yet.
        if (!steps_.empty()) {
            if (!stopped_ && now_ < step_at_) return;
            const Step step = std::move(steps_.front());
            steps_.pop_front();
            show(step.event);
            touch();
            if (!steps_.empty()) {
                step_at_ = now_ + steps_.front().delay;
            } else if (table_.hand()->finished()) {
                stage_ = Stage::Over;
                deal_at_ = now_ + (showdown_ ? pace_.after_showdown : pace_.after_hand);
                notices_.push_back(notice::HandOver{hand_number_});
                tell_bots();
            }
            continue;
        }

        if (table_.playing()) {
            game::Hand& hand = *table_.hand();
            if (hand.finished()) {
                if (remark_ && (stopped_ || now_ >= remark_at_)) {
                    if (!stopped_) {
                        log(std::move(*remark_));
                        touch();
                    }
                    remark_.reset();
                }
                if (!stopped_ && now_ < deal_at_) return;
                settle();
                continue;
            }
            const int seat = table_.seat_to_act();
            const bool gone = leaving_[at(seat)];
            if (!who_[at(seat)].bot) {
                if (run_clock(seat)) continue;
                watch_seats();  // the person is still deciding
                return;
            }
            bots::Brain* brain = gone ? nullptr : brains_[at(seat)].get();
            // A bot makes up its mind when its turn comes and then takes the time the decision deserves.
            if (brain != nullptr && !decided_) {
                decided_ = brain->decide(bots::spot_of(hand), watch_);
                think_until_ = now_ + think_time(*decided_, *brain);
            }
            if (brain != nullptr && !stopped_ && now_ < think_until_) return;
            think_until_ = -1;
            game::Action action{game::Move::Fold};
            if (brain != nullptr) action = decided_->action;
            decided_.reset();
            if (!hand.act(action)) {
                // Should not happen; keep the game going rather than hang.
                if (!hand.act(game::Action{game::Move::Check})) hand.act(game::Action{game::Move::Fold});
            }
            queue_events();
            continue;
        }

        watch_seats();
        if (stopped_ || !table_.can_deal()) {
            deal_at_ = -1;
            if (stage_ != Stage::Idle) {
                stage_ = Stage::Idle;
                touch();
            }
            return;
        }
        if (deal_at_ < 0) {
            deal_at_ = now_ + pace_.first_hand;
            if (!greeted_) greet();
        }
        // One hand per call at most, so that a table of bots alone playing
        // without pauses still lets the caller in between hands.
        if (now_ < deal_at_ || dealt) return;
        if (!deal()) return;
        dealt = true;
    }
}

bool Dealer::deal() {
    std::vector<game::Card> deck;
    if (deck_source_) {
        std::optional<std::vector<game::Card>> given = deck_source_(hand_number_ + 1);
        // Not ready yet: try again on the next tick.
        if (!given) return false;
        deck = std::move(*given);
    } else {
        deck = game::shuffled_deck([this](std::uint32_t bound) { return random_.below(bound); });
    }
    deal_at_ = -1;
    for (int seat = 0; seat < table_.seat_count(); ++seat) stack_before_[at(seat)] = table_.seat(seat).stack;
    if (!table_.deal(std::move(deck))) return true;
    last_deal_.hand_number = hand_number_ + 1;
    last_deal_.button = table_.button();
    last_deal_.seats.clear();
    for (const game::HandPlayer& player : table_.hand()->players()) last_deal_.seats.push_back(player.seat);
    std::vector<std::string> names;
    for (const Player& who : who_) names.push_back(who.name);
    watch_.begin(*table_.hand(), names);

    ++hand_number_;
    stage_ = Stage::Playing;
    button_ = table_.button();
    board_.clear();
    pot_ = 0;
    aggressor_ = -1;
    last_actor_ = -1;
    showdown_ = false;
    run_out_ = false;
    turn_seat_ = -1;
    for (int seat = 0; seat < table_.seat_count(); ++seat) {
        const game::Seat& place = table_.seat(seat);
        Shown& shown = shown_[at(seat)];
        shown = Shown{};
        shown.stack = place.stack;
        shown.in_hand = place.in_hand;
        if (place.in_hand) bank_[at(seat)] = std::min(clock_.bank_ms, bank_[at(seat)] + clock_.bank_back_ms);
    }
    LogLine line;
    line.kind = LogLine::Kind::NewHand;
    line.number = hand_number_;
    log(std::move(line));
    queue_events();
    touch();
    return true;
}

void Dealer::queue_events() {
    const bool was_empty = steps_.empty();
    bool won = false;
    for (game::Event& event : table_.hand()->take_events()) {
        watch_.see(event);
        const int delay = std::visit(
            Overloaded{
                [](const game::event::Blind&) { return 0; },
                [](const game::event::Dealt&) { return 0; },
                [](const game::event::Acted&) { return 0; },
                [&](const game::event::Board&) {
                    // The first card after the betting comes sooner than the
                    // cards of an all-in, which people want to sweat one by one.
                    const int pause = run_out_ ? pace_.run_out : pace_.street;
                    run_out_ = true;
                    return pause;
                },
                [&](const game::event::Returned&) { return pace_.win / 2; },
                [&](const game::event::Showed&) {
                    run_out_ = true;
                    return pace_.show;
                },
                [&](const game::event::Mucked&) { return pace_.show; },
                [&](const game::event::Won&) {
                    const int pause = won ? 0 : pace_.win;
                    won = true;
                    return pause;
                },
            },
            event);
        steps_.push_back(Step{delay, std::move(event)});
    }
    // If the hand goes on, somebody still has a move to make: the next card
    // will again be the first one after betting.
    if (!table_.hand()->finished()) run_out_ = false;
    if (was_empty && !steps_.empty()) step_at_ = now_ + steps_.front().delay;
}

void Dealer::show(const game::Event& event) {
    const auto gather_bets = [this] {
        for (Shown& shown : shown_) shown.bet = 0;
    };
    std::visit(
        Overloaded{
            [&](const game::event::Blind& e) {
                Shown& shown = shown_[at(e.seat)];
                shown.stack -= e.amount;
                shown.bet += e.amount;
                pot_ += e.amount;
                shown.all_in = shown.stack == 0;
                shown.note = e.big ? Note::BigBlind : Note::SmallBlind;
                shown.note_amount = shown.bet;
                log(line_for(e.seat, LogLine::Kind::Blind, e.amount));
            },
            [&](const game::event::Dealt& e) { shown_[at(e.seat)].cards = e.cards; },
            [&](const game::event::Acted& e) {
                Shown& shown = shown_[at(e.seat)];
                shown.stack -= e.added;
                shown.bet += e.added;
                pot_ += e.added;
                last_actor_ = e.seat;
                LogLine::Kind kind = LogLine::Kind::Fold;
                switch (e.move) {
                    case game::Move::Fold:
                        shown.folded = true;
                        shown.note = Note::Fold;
                        break;
                    case game::Move::Check:
                        shown.note = Note::Check;
                        kind = LogLine::Kind::Check;
                        break;
                    case game::Move::Call:
                        shown.note = Note::Call;
                        kind = LogLine::Kind::Call;
                        break;
                    case game::Move::Bet:
                        shown.note = Note::Bet;
                        kind = LogLine::Kind::Bet;
                        aggressor_ = e.seat;
                        break;
                    case game::Move::Raise:
                        shown.note = Note::Raise;
                        kind = LogLine::Kind::Raise;
                        aggressor_ = e.seat;
                        break;
                }
                shown.note_amount = shown.bet;
                if (e.all_in) {
                    shown.all_in = true;
                    shown.note = Note::AllIn;
                    kind = LogLine::Kind::AllIn;
                }
                log(line_for(e.seat, kind, shown.bet));
            },
            [&](const game::event::Board& e) {
                gather_bets();
                for (Shown& shown : shown_) {
                    if (shown.in_hand && !shown.folded && !shown.all_in && !shown.cards_up) shown.note = Note::None;
                }
                board_.insert(board_.end(), e.cards.begin(), e.cards.end());
                aggressor_ = -1;
                LogLine line;
                line.kind = LogLine::Kind::Board;
                line.cards = board_;
                log(std::move(line));
            },
            [&](const game::event::Returned& e) {
                Shown& shown = shown_[at(e.seat)];
                shown.stack += e.amount;
                shown.bet = std::max<Chips>(shown.bet - e.amount, 0);
                pot_ -= e.amount;
                log(line_for(e.seat, LogLine::Kind::Returned, e.amount));
            },
            [&](const game::event::Showed& e) {
                gather_bets();
                Shown& shown = shown_[at(e.seat)];
                shown.cards = e.cards;
                shown.cards_up = true;
                shown.note = Note::Show;
                showdown_ = true;
                LogLine line = line_for(e.seat, LogLine::Kind::Shows);
                line.cards.assign(e.cards.begin(), e.cards.end());
                log(std::move(line));
            },
            [&](const game::event::Mucked& e) {
                gather_bets();
                shown_[at(e.seat)].note = Note::Muck;
                showdown_ = true;
                log(line_for(e.seat, LogLine::Kind::Mucks));
            },
            [&](const game::event::Won& e) {
                gather_bets();
                Shown& shown = shown_[at(e.seat)];
                shown.stack += e.amount;
                shown.won += e.amount;
                pot_ -= e.amount;
                shown.note = Note::Win;
                shown.note_amount = shown.won;
                log(line_for(e.seat, LogLine::Kind::Wins, e.amount));
            },
        },
        event);
}

// Lets every bot that played the hand know how it went for it. One of them
// may have something to say about it.
void Dealer::tell_bots() {
    const double big_blind = static_cast<double>(table_.stakes().big_blind);
    const auto won = [&](const game::HandPlayer& player) {
        return static_cast<double>(player.stack - stack_before_[at(player.seat)]) / big_blind;
    };
    double biggest = 0;
    for (const game::HandPlayer& player : table_.hand()->players()) biggest = std::max(biggest, won(player));

    std::vector<std::pair<int, int>> voices;  // seat and line
    for (const game::HandPlayer& player : table_.hand()->players()) {
        bots::Brain* brain = brains_[at(player.seat)].get();
        if (brain == nullptr) continue;
        const Shown& shown = shown_[at(player.seat)];
        bots::Outcome outcome;
        outcome.won = won(player);
        // A losing hand thrown away unseen went to the showdown all the same.
        outcome.showdown = shown.cards_up || shown.note == Note::Muck;
        outcome.biggest = biggest;
        const int line = brain->hand_over(outcome);
        if (line >= 0) voices.emplace_back(player.seat, line);
    }
    // One voice per hand is enough.
    if (voices.empty() || !talk_ || stopped_) return;
    const auto [seat, line] = voices[random_.below(static_cast<std::uint32_t>(voices.size()))];
    remark_ = line_for(seat, LogLine::Kind::Says);
    remark_->number = line;
    remark_at_ = now_ + pace_.remark;
}

// Before the first hand one of the bots may say hello.
void Dealer::greet() {
    greeted_ = true;
    std::vector<int> seats;
    for (int seat = 0; seat < table_.seat_count(); ++seat) {
        if (brains_[at(seat)] != nullptr) seats.push_back(seat);
    }
    if (seats.empty() || !talk_) return;
    const int seat = seats[random_.below(static_cast<std::uint32_t>(seats.size()))];
    const int line = brains_[at(seat)]->hello();
    if (line < 0) return;
    LogLine said = line_for(seat, LogLine::Kind::Says);
    said.number = line;
    log(std::move(said));
    touch();
}

void Dealer::settle() {
    remark_.reset();
    for (const game::Departure& gone : table_.settle()) {
        notices_.push_back(notice::Left{gone.seat, gone.chips});
        who_[at(gone.seat)] = Player{};
        offline_[at(gone.seat)] = false;
        brains_[at(gone.seat)].reset();
        wanted_[at(gone.seat)] = 0;
        leaving_[at(gone.seat)] = false;
    }
    for (int seat = 0; seat < table_.seat_count(); ++seat) {
        shown_[at(seat)] = Shown{};
        shown_[at(seat)].stack = table_.seat(seat).stack;
    }
    stage_ = Stage::Idle;
    board_.clear();
    pot_ = 0;
    aggressor_ = -1;
    last_actor_ = -1;
    add_wanted_chips();
    if (!stopped_) bots_buy_chips();
    deal_at_ = stopped_ ? -1 : now_;
    touch();
}

int Dealer::think_time(const bots::Decision& decision, const bots::Brain& brain) {
    if (pace_.think_max <= 0) return 0;
    // Easy moves stay near the minimum; only the hardest ones get close to the maximum.
    const double span = pace_.think_max - pace_.think_min;
    double time = pace_.think_min + span * std::pow(decision.effort, 1.5) * brain.style().tempo;
    time *= 0.85 + 0.3 * random_.unit();
    return static_cast<int>(std::clamp<double>(time, pace_.think_min, pace_.think_max));
}

void Dealer::bots_buy_chips() {
    const game::Stakes& stakes = table_.stakes();
    for (int seat = 0; seat < table_.seat_count(); ++seat) {
        const game::Seat& place = table_.seat(seat);
        if (!place.taken || !who_[at(seat)].bot || place.stack >= stakes.min_buy_in / 2) continue;
        // Back to somewhere between 60 and 100 big blinds, in whole big blinds.
        const Chips most = stakes.max_buy_in / stakes.big_blind;
        const Chips least = most * 6 / 10;
        const Chips target =
            (least + random_.below(static_cast<std::uint32_t>(most - least + 1))) * stakes.big_blind;
        const Chips amount = target - place.stack;
        if (amount > 0 && table_.add_chips(seat, amount)) {
            shown_[at(seat)].stack = table_.seat(seat).stack;
            notices_.push_back(notice::ChipsAdded{seat, amount});
            log(line_for(seat, LogLine::Kind::Rebuy, amount));
        }
    }
}

void Dealer::add_chips_to(int seat) {
    const Chips target = std::exchange(wanted_[at(seat)], 0);
    const game::Seat& place = table_.seat(seat);
    if (target <= 0 || !place.taken) return;
    const Chips amount =
        std::min(std::min(target, table_.stakes().max_buy_in) - place.stack, wanted_most_[at(seat)]);
    if (amount > 0 && table_.add_chips(seat, amount)) {
        shown_[at(seat)].stack = table_.seat(seat).stack;
        notices_.push_back(notice::ChipsAdded{seat, amount});
        log(line_for(seat, LogLine::Kind::Rebuy, amount));
        touch();
    }
}

void Dealer::add_wanted_chips() {
    for (int seat = 0; seat < table_.seat_count(); ++seat) add_chips_to(seat);
}

bool Dealer::act(int seat, game::Action action) {
    if (!steps_.empty() || !table_.playing() || table_.hand()->finished()) return false;
    if (seat < 0 || table_.seat_to_act() != seat || who_[at(seat)].bot) return false;
    if (!table_.hand()->act(action)) return false;
    // Time over the move comes out of the reserve.
    if (turn_seat_ == seat && clock_runs()) {
        const std::int64_t over = now_ - turn_started_ - clock_.turn_ms;
        if (over > 0) bank_[at(seat)] = static_cast<int>(std::max<std::int64_t>(0, bank_[at(seat)] - over));
    }
    missed_[at(seat)] = 0;
    turn_seat_ = -1;
    queue_events();
    tick(now_);  // show the move at once
    return true;
}

void Dealer::want_chips(int seat, Chips target, Chips most) {
    if (seat < 0 || seat >= table_.seat_count() || !table_.seat(seat).taken) return;
    wanted_[at(seat)] = target;
    wanted_most_[at(seat)] = most;
    // Not in a hand: the chips can be added right away.
    if (!table_.seat(seat).in_hand) add_chips_to(seat);
    touch();
}

void Dealer::leave(int seat) {
    if (seat < 0 || seat >= table_.seat_count() || !table_.seat(seat).taken) return;
    if (const std::optional<Chips> chips = table_.leave(seat)) {
        notices_.push_back(notice::Left{seat, *chips});
        who_[at(seat)] = Player{};
        offline_[at(seat)] = false;
        brains_[at(seat)].reset();
        shown_[at(seat)] = Shown{};
        wanted_[at(seat)] = 0;
    } else {
        leaving_[at(seat)] = true;
    }
    touch();
}

void Dealer::finish_now() {
    stopped_ = true;
    // The hand cannot take more steps than this; the limit only guards against a hang.
    for (int guard = 0; guard < 10000 && table_.playing(); ++guard) tick(now_);
    tick(now_);
}

std::vector<Notice> Dealer::take_notices() {
    return std::exchange(notices_, {});
}

Chips Dealer::stack(int seat) const {
    if (table_.playing()) {
        for (const game::HandPlayer& player : table_.hand()->players()) {
            if (player.seat == seat) return player.stack;
        }
    }
    return table_.seat(seat).stack;
}

void Dealer::log(LogLine line) {
    ++logged_;
    log_.push_back(std::move(line));
    while (log_.size() > kLogLines) log_.pop_front();
}

LogLine Dealer::line_for(int seat, LogLine::Kind kind, Chips amount) const {
    LogLine line;
    line.kind = kind;
    line.name = who_[at(seat)].name;
    line.bot = who_[at(seat)].bot;
    line.amount = amount;
    return line;
}

TableView Dealer::view(int viewer) const {
    TableView view;
    view.stakes = table_.stakes();
    view.hand_number = hand_number_;
    view.stage = stage_;
    view.viewer = viewer;
    view.button = button_;
    view.aggressor = aggressor_;
    view.last_actor = last_actor_;
    view.board = board_;
    view.pot = pot_;
    view.seats.resize(static_cast<std::size_t>(table_.seat_count()));
    for (int seat = 0; seat < table_.seat_count(); ++seat) {
        const game::Seat& place = table_.seat(seat);
        if (!place.taken) continue;
        const Shown& shown = shown_[at(seat)];
        const Player& who = who_[at(seat)];
        SeatView& out = view.seats[at(seat)];
        out.taken = true;
        out.name = who.name;
        out.avatar = who.avatar;
        out.bot = who.bot;
        out.stack = shown.stack;
        out.bet = shown.bet;
        out.in_hand = shown.in_hand;
        out.folded = shown.folded;
        out.all_in = shown.all_in;
        out.away = place.away;
        out.offline = offline_[at(seat)];
        out.note = shown.note;
        out.note_amount = shown.note_amount;
        out.shown = shown.cards_up;
        out.won = shown.won;
        if (shown.in_hand && (seat == viewer || shown.cards_up)) out.cards = shown.cards;
        if (!shown.in_hand && stage_ != Stage::Idle) out.note = place.away ? Note::Away : Note::Waiting;
    }
    if (steps_.empty() && table_.playing() && !table_.hand()->finished()) {
        view.acting = table_.seat_to_act();
        if (view.acting == viewer) view.choices = table_.hand()->choices();
        if (clock_runs() && !who_[at(view.acting)].bot && turn_seat_ == view.acting) {
            const std::int64_t used = now_ - turn_started_;
            view.turn_ms = static_cast<int>(std::max<std::int64_t>(0, clock_.turn_ms - used));
            const std::int64_t over = std::max<std::int64_t>(0, used - clock_.turn_ms);
            view.bank_ms = static_cast<int>(std::max<std::int64_t>(0, bank_[at(view.acting)] - over));
        }
    }
    if (viewer >= 0 && viewer < table_.seat_count()) view.chips_coming = wanted_[at(viewer)];
    view.log.assign(log_.begin(), log_.end());
    view.log_total = logged_;
    return view;
}

}  // namespace poher::host
