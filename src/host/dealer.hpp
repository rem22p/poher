// The host of a table. It deals hand after hand, lets the bots think and
// move, takes the moves of people, and paces everything so that a person can
// follow it: a bet is seen before the next card comes, cards are turned over
// one player at a time.
//
// The dealer knows nothing about the screen or the network. Whoever owns it
// calls tick() often, passes on the moves of people, and reads view().
#pragma once

#include <cstdint>
#include <functional>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "bots/brain.hpp"
#include "game/random.hpp"
#include "game/table.hpp"
#include "host/view.hpp"

namespace poher::host {

struct Player {
    std::string name;
    std::string avatar;
    bool bot = false;
};

// Pauses, in milliseconds.
struct Pace {
    int think_min = 1000;       // a bot's move takes from this, when there is nothing to think about...
    int think_max = 15000;      // ...to this, for the hardest decisions
    int first_hand = 1200;      // before the very first hand
    int street = 800;           // between the last move of a street and the next card
    int run_out = 1500;         // between cards when everybody is all-in
    int show = 800;             // before each hand is turned over
    int win = 800;              // before the pot is handed out
    int remark = 900;           // before a bot says something about the hand that just ended
    int after_hand = 3000;      // the result stays on the screen this long...
    int after_showdown = 5000;  // ...or this long if hands were shown

    // No pauses at all: for tests and for playing a hand out at once.
    static Pace none();
};

// The clock on people's moves (SPEC 3.3). Bots keep their own pace.
struct TurnClock {
    int turn_ms = 0;             // for each move; 0 = no clock
    int bank_ms = 60000;         // the reserve, spent once a move takes longer
    int bank_back_ms = 5000;     // given back after every hand the player was dealt into
    int grace_ms = 2000;         // waited beyond what is shown, so that a slow network does not eat the move
    int away_after = 2;          // moves missed in a row before the player counts as away
    int away_seat_ms = 15 * 60 * 1000;    // how long the seat of an away player is kept
    int offline_seat_ms = 3 * 60 * 1000;  // and of a player who lost the connection
};

// Things the owner of the dealer has to know about and act on.
namespace notice {
struct ChipsAdded { int seat; Chips amount; };  // take them off the bankroll of the player or bot
struct Left { int seat; Chips chips; };         // the player got up with these chips
struct HandOver { int number; };
}  // namespace notice

using Notice = std::variant<notice::ChipsAdded, notice::Left, notice::HandOver>;

class Dealer {
public:
    Dealer(game::Stakes stakes, int seats, std::uint64_t seed, Pace pace = {});

    // Seats a player. False if the seat is taken or the buy-in is outside the limits.
    bool sit(int seat, Player who, Chips buy_in);

    // Moves the game on. `now` is a clock in milliseconds that never runs backwards.
    void tick(std::int64_t now);

    // A move by a person. False if it is not their turn or the rules forbid the move.
    bool act(int seat, game::Action action);
    // Asks for chips: up to `target` in the stack, but no more than `most`
    // chips added. They are added between hands; a ChipsAdded notice tells
    // how many it really was.
    void want_chips(int seat, Chips target, Chips most);
    // Gets the player up. In the middle of a hand their cards are folded when
    // their turn comes and they leave when the hand is over. A Left notice follows.
    void leave(int seat);
    // The clock on moves. It runs only while two or more people sit at the
    // table: alone with bots a person may take all the time they like.
    void set_clock(TurnClock clock) { clock_ = clock; }
    const TurnClock& clock() const { return clock_; }
    // A person lost the connection (true) or got it back (false). Until then
    // they are not dealt in, their moves are checks or folds, and after
    // TurnClock::offline_seat_ms they are got up.
    void set_offline(int seat, bool offline);
    // A person who was away (missed moves) is back and is dealt in again.
    void come_back(int seat);
    // How many people, not bots, sit at the table.
    int people() const;
    // Plays the current hand to its end without any pauses and deals no more.
    void finish_now();
    // Whether the bots say their lines now and then. They do unless told otherwise.
    void set_talk(bool talk) { talk_ = talk; }
    // Where the deck of each hand comes from. Given the number the hand will
    // have, it returns the deck, or nothing to have the hand wait a little.
    // Without one the dealer shuffles by itself.
    using DeckSource = std::function<std::optional<std::vector<game::Card>>(int hand_number)>;
    void set_deck_source(DeckSource source) { deck_source_ = std::move(source); }
    const DeckSource& deck_source() const { return deck_source_; }
    // Who was dealt into the latest hand and where the button was.
    struct Deal {
        int hand_number = 0;
        int button = -1;
        std::vector<int> seats;  // in the order of the table
    };
    const Deal& last_deal() const { return last_deal_; }

    TableView view(int viewer) const;
    // Grows whenever the picture of the table has changed.
    std::uint64_t changes() const { return changes_; }
    std::vector<Notice> take_notices();

    const game::Table& table() const { return table_; }
    // The chips of a seat right now, in the middle of a hand too.
    Chips stack(int seat) const;
    bool stopped() const { return stopped_; }
    std::int64_t now() const { return now_; }
    // What the bots of this table have noted about every player.
    const bots::Notebook& notebook() const { return notebook_; }
    // What was noted about a player at earlier tables: the bots start from it.
    void remember(std::string_view player, const bots::Notes& notes) { notebook_.of(player) = notes; }
    // The mind of the bot in a seat; nullptr for a person or an empty seat.
    const bots::Brain* brain(int seat) const { return brains_[at(seat)].get(); }

private:
    // What the table shows about one seat. It trails behind the rules while
    // the queued steps are played.
    struct Shown {
        Chips stack = 0;
        Chips bet = 0;
        bool in_hand = false;
        bool folded = false;
        bool all_in = false;
        bool cards_up = false;
        Note note = Note::None;
        Chips note_amount = 0;
        Chips won = 0;
        std::array<Card, 2> cards{};
    };

    struct Step {
        int delay = 0;  // milliseconds to wait before it is shown
        game::Event event;
    };

    bool deal();  // false if the deck is not ready yet
    void queue_events();
    void show(const game::Event& event);
    void settle();
    int think_time(const bots::Decision& decision, const bots::Brain& brain);
    void tell_bots();
    void greet();
    void bots_buy_chips();
    bool run_clock(int seat);
    void watch_seats();
    bool clock_runs() const;
    void add_wanted_chips();
    void add_chips_to(int seat);
    void log(LogLine line);
    LogLine line_for(int seat, LogLine::Kind kind, Chips amount = 0) const;
    void touch() { ++changes_; }
    std::size_t at(int seat) const { return static_cast<std::size_t>(seat); }

    game::Table table_;
    game::Random random_;
    DeckSource deck_source_;
    Deal last_deal_;
    Pace pace_;
    std::vector<Player> who_;
    std::vector<Shown> shown_;
    std::vector<Chips> wanted_;      // stack the player asked to be topped up to; 0 = no request
    std::vector<Chips> wanted_most_; // and the most chips that may take
    std::vector<bool> leaving_;

    // The clock on people's moves.
    TurnClock clock_;
    std::vector<int> bank_;               // reserve left, by seat
    std::vector<int> missed_;             // moves missed in a row
    std::vector<bool> offline_;
    std::vector<std::int64_t> since_;     // when the seat went away or offline
    int turn_seat_ = -1;                  // whose move the clock is running for; -1 = nobody's yet
    std::int64_t turn_started_ = 0;

    // The bots: one mind per seat, a shared record of the hand and shared notes on the players.
    bots::Notebook notebook_;
    bots::Watch watch_{&notebook_};
    std::vector<std::unique_ptr<bots::Brain>> brains_;
    std::optional<bots::Decision> decided_;  // the move the bot to act has made up its mind about
    std::vector<Chips> stack_before_;        // at the start of the hand, by seat
    bool talk_ = true;
    bool greeted_ = false;
    std::optional<LogLine> remark_;          // a line a bot is about to say
    std::int64_t remark_at_ = 0;

    std::deque<Step> steps_;
    std::int64_t now_ = 0;
    std::int64_t step_at_ = 0;       // when the first queued step is due
    std::int64_t think_until_ = -1;  // when the bot to act makes its move; -1 = not started thinking
    std::int64_t deal_at_ = -1;      // when the next hand starts; -1 = not planned

    Stage stage_ = Stage::Idle;
    int hand_number_ = 0;
    int button_ = -1;
    int aggressor_ = -1;
    int last_actor_ = -1;
    std::vector<Card> board_;
    Chips pot_ = 0;
    bool showdown_ = false;          // hands were turned over in this hand
    bool run_out_ = false;           // cards are coming with nobody left to act
    bool stopped_ = false;           // finish_now() was called: no more hands

    std::deque<LogLine> log_;
    std::int64_t logged_ = 0;        // lines written in all
    std::vector<Notice> notices_;
    std::uint64_t changes_ = 0;
};

}  // namespace poher::host
