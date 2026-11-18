// Every text the game shows, in English and in Russian.
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "game/hand.hpp"
#include "game/hand_value.hpp"
#include "host/view.hpp"

namespace poher::ui {

enum class Lang { En, Ru };

// The language of the system: Russian if the system is set to it, English otherwise.
Lang system_lang();

struct Words {
    std::string_view language;  // the name of this language in itself
    std::string_view you;       // the player's own name at the table, until profiles exist

    // The table.
    std::string_view game;       // "No-Limit Hold'em"
    std::string_view blinds;
    std::string_view hand;       // "hand #"
    std::string_view bankroll;
    std::string_view pot;
    std::string_view your_move;
    std::string_view thinking;
    std::string_view clock_left;     // "{} s": time left for a move
    std::string_view clock_reserve;  // "reserve {} s"
    std::string_view offline;        // a player lost the connection
    std::string_view away_prompt;    // the player missed moves and sits out
    std::string_view waiting_for_players;
    std::string_view next_hand_soon;

    // Under a player's stack.
    std::string_view note_waiting;
    std::string_view note_away;
    std::string_view note_small_blind;
    std::string_view note_big_blind;
    std::string_view note_fold;
    std::string_view note_check;
    std::string_view note_call;
    std::string_view note_bet;
    std::string_view note_raise;
    std::string_view note_all_in;
    std::string_view note_muck;
    std::string_view note_wins;

    // Buttons.
    std::string_view fold;
    std::string_view check;
    std::string_view call;
    std::string_view bet;
    std::string_view raise;
    std::string_view bet_to;     // "Bet {}"
    std::string_view raise_to;   // "Raise to {}"
    std::string_view all_in;     // "All-in {}"
    std::string_view size_half;
    std::string_view size_two_thirds;
    std::string_view size_pot;
    std::string_view size_all_in;
    std::string_view raise_keys;   // the hint line while choosing the amount
    std::string_view raise_limits; // "from {} to {}"
    std::string_view out_of_chips;
    std::string_view buy_in_key;   // "B  buy in for {}"
    std::string_view refill_key;   // "B  refill the bankroll to {} and buy in"
    std::string_view chips_coming; // "{} more chips after this hand"
    std::string_view table_keys;   // the bottom line
    std::string_view panel_log;    // the heading over the record of the hand

    // The middle of the table when a hand ends.
    std::string_view wins;         // "{} wins {}"
    std::string_view split;        // "split pot: {}"

    // The record of the hand.
    std::string_view log_hand;     // "hand #{}"
    std::string_view log_blind;    // "{} posts {}"
    std::string_view log_fold;
    std::string_view log_check;
    std::string_view log_call;
    std::string_view log_bet;
    std::string_view log_raise;
    std::string_view log_all_in;
    std::string_view log_shows;
    std::string_view log_mucks;
    std::string_view log_returned;
    std::string_view log_wins;
    std::string_view log_rebuy;
    std::string_view log_says;     // "“{}”": the quotation marks of the language

    // Menus.
    std::string_view menu_play;
    std::string_view menu_refill;          // "Refill the bankroll to {}"
    std::string_view menu_profile;
    std::string_view menu_history;
    std::string_view menu_settings;
    std::string_view menu_quit;
    std::string_view menu_keys;
    std::string_view setup_title;
    std::string_view setup_stakes;
    std::string_view setup_seats;
    std::string_view setup_buy_in;
    std::string_view setup_bots;
    std::string_view setup_start;
    std::string_view setup_range;          // "from {} to {}", the limits of the buy-in
    std::string_view setup_keys;
    std::string_view setup_need_bankroll;  // "needs a bankroll of {}"
    std::string_view setup_not_here;       // a bot that does not sit at this level
    std::string_view setup_pick_bots;      // "pick from 1 to {} bots"
    std::string_view level_names[5];
    std::string_view strength_names[3];
    std::string_view settings_title;
    std::string_view settings_language;
    std::string_view settings_hint;
    std::string_view settings_sound;
    std::string_view settings_talk;
    std::string_view settings_keys;
    std::string_view hint_off;
    std::string_view hint_name;
    std::string_view hint_full;
    std::string_view hint_outs;            // "outs: {} · improves {}%"
    std::string_view hint_win;             // "wins {}%"
    std::string_view on;
    std::string_view off;
    std::string_view on_many;              // the same for a plural word: Russian needs another ending
    std::string_view off_many;
    std::string_view back;
    std::string_view pause_title;
    std::string_view pause_resume;
    std::string_view pause_leave;
    std::string_view pause_refill;         // "Refill the bankroll to {}"
    std::string_view pause_quit;
    std::string_view refilled;             // "bankroll refilled to {}"

    // The profile.
    std::string_view profile_title;
    std::string_view profile_nick;
    std::string_view profile_no_nick;      // shown while no nick is set
    std::string_view profile_avatar;
    std::string_view profile_bankroll;
    std::string_view profile_refills;
    std::string_view profile_public;       // the heading over what friends will see
    std::string_view profile_private;      // and over what only the player sees
    std::string_view stat_hands;
    std::string_view stat_hands_won;
    std::string_view stat_biggest_pot;
    std::string_view stat_vs_bots;
    std::string_view stat_vs_people;
    std::string_view stat_enters;
    std::string_view stat_raises;
    std::string_view stat_pushes;
    std::string_view stat_showdowns_won;
    std::string_view profile_keys;
    std::string_view profile_nick_keys;    // while the nick is being typed
    std::string_view nick_taken;           // the nick is the name of a bot
    std::string_view setup_bot_account;    // "{name}: bankroll {} · hands {} · result {}"

    // Friends and their tables.
    std::string_view menu_friends;
    std::string_view pause_friends;
    std::string_view setup_title_online;
    std::string_view setup_friends;
    std::string_view setup_open;
    std::string_view setup_invited;
    std::string_view setup_clock;
    std::string_view setup_no_clock;
    std::string_view setup_clock_note;
    std::string_view table_of;  // "{}'s table"
    std::string_view table_got_up;  // the host got the player up
    std::string_view table_closed;
    std::string_view host_gone;
    std::string_view host_lost;  // "{m:ss}" left
    std::string_view joining;  // "{name}"
    std::string_view joining_keys;
    std::string_view refused_full;
    std::string_view refused_closed;
    std::string_view refused_version;
    std::string_view refused_no_table;
    std::string_view refused_bankroll;
    std::string_view friends_title;
    std::string_view net_connecting;
    std::string_view net_online;
    std::string_view my_code;
    std::string_view my_code_note;
    std::string_view add_friend;
    std::string_view code_prompt;
    std::string_view request_from;  // "{short id}" "{hello}"
    std::string_view request_keys;
    std::string_view friends_none;
    std::string_view friend_offline;
    std::string_view friend_online;
    std::string_view friend_menu;
    std::string_view friend_bots;
    std::string_view friend_hosting;  // "{level}" "{taken}" "{seats}"
    std::string_view friend_open;
    std::string_view friend_guest;
    std::string_view friend_version;  // "{version}"
    std::string_view link_direct;
    std::string_view link_relay;
    std::string_view friends_keys;
    std::string_view code_keys;
    std::string_view add_sent;
    std::string_view add_bad;
    std::string_view add_own;
    std::string_view add_already;
    std::string_view add_failed;
    std::string_view code_copied;
    std::string_view friend_sat_down;  // "{name}"
    std::string_view deck_alarm;
    std::string_view deck_doubt;  // in the title of the table
    std::string_view invite_sent;  // "{name}"
    std::string_view cannot_join;
    std::string_view need_bankroll;  // "{chips}"
    std::string_view invitation_title;  // "{name}"
    std::string_view invitation_table;  // "{level} {blinds}" "{taken}" "{seats}"
    std::string_view invitation_keys;  // "{buy-in}"
    std::string_view invitation_leave;

    // The history of hands.
    std::string_view history_title;
    std::string_view history_empty;
    std::string_view history_keys;
    std::string_view history_hand_keys;
    std::string_view history_hand;         // "hand #{}"
};

const Words& words(Lang lang);

// "{}" in the pattern is replaced by the arguments in order.
std::string fill(std::string_view pattern, std::initializer_list<std::string_view> arguments);

// 12480 -> "12 480".
std::string chips_text(game::Chips chips);

// "Two pair" and the like: the kind of hand without the cards.
std::string_view category_name(game::Category category, Lang lang);
// "Two pair: aces and sevens".
std::string describe_hand(game::HandValue value, Lang lang);
// The same for the cards a player holds together with the board. Before the
// flop only a pair is named; otherwise the result is empty.
std::string describe_cards(const std::array<game::Card, 2>& cards, const std::vector<game::Card>& board, Lang lang);

// The line under a player's stack: "call 40".
std::string note_text(host::Note note, game::Chips amount, Lang lang);
// One line of the record of the hand, without the name of the player: "bets 40".
std::string log_text(const host::LogLine& line, Lang lang);
// Cards as short text: "A♠ K♦ 7♣".
std::string cards_text(const std::vector<game::Card>& cards);

}  // namespace poher::ui
