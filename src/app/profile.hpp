// What the game keeps between runs: the player's profile, the accounts of the
// bots, the settings and the history of hands. Everything is plain text in
// one folder on the player's computer; there is no server to restore it from.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "app/settings.hpp"
#include "bots/notes.hpp"
#include "host/roster.hpp"
#include "host/view.hpp"

namespace poher::app {

using game::Chips;

struct Stats {
    long long hands_won = 0;
    Chips biggest_pot = 0;      // the biggest pot won
    Chips from_bots = 0;        // won minus lost at tables with bots only
    Chips from_people = 0;      // the same at tables with people
    long long showdowns = 0;
    long long showdowns_won = 0;
    // How the player plays, counted the way the bots count it about everybody:
    // hands dealt, entered, raised before the flop, bets, calls, folds to a bet.
    bots::Notes play;
};

// A bot has a bankroll of its own and a record against this player's tables.
struct BotAccount {
    Chips bankroll = 0;
    int refills = 0;
    long long hands = 0;
    Chips won = 0;  // won minus lost
    bots::Notes seen;  // how it has been playing lately, as the table saw it
};

struct Profile {
    Profile();

    std::string nick;  // empty until the player gives one
    std::string avatar{host::kDefaultAvatar};
    Chips bankroll = host::kStartBankroll;
    int refills = 0;
    Stats stats;
    // What the bots remember of how the player plays. Unlike the statistics
    // it fades: old hands weigh less than the latest ones.
    bots::Notes seen;
    std::array<BotAccount, host::kBots.size()> bots;  // in the order of host::kBots
    // What this player's bots remember of friends who sat at their table, by
    // the friend's network id written in hex.
    std::map<std::string, bots::Notes> people;
};

// One finished hand in the history.
struct PastHand {
    int number = 0;           // its number at the table
    std::int64_t when = 0;    // seconds since 1970
    std::size_t level = 0;    // which of host::kLevels
    std::optional<std::array<game::Card, 2>> cards;
    Chips result = 0;         // chips won minus chips put in
    std::vector<host::LogLine> lines;
};

class Storage {
public:
    // With an empty folder nothing is read and nothing is written.
    explicit Storage(std::filesystem::path folder = {});

    bool enabled() const { return !folder_.empty(); }
    const std::filesystem::path& folder() const { return folder_; }

    // What is not found in the files keeps the value it had.
    void load(Settings& settings) const;
    void load(Profile& profile) const;
    void save(const Settings& settings) const;
    void save(const Profile& profile) const;

    void add(const PastHand& hand) const;

    // A file of bytes kept as it is: the network identity.
    void save_bytes(const std::string& name, const std::vector<std::uint8_t>& bytes) const;
    std::vector<std::uint8_t> load_bytes(const std::string& name) const;
    // The latest hands, the newest first.
    std::vector<PastHand> history(std::size_t most) const;

private:
    std::filesystem::path folder_;
};

// The longest nick, in glyphs.
inline constexpr std::size_t kNickLength = 12;

}  // namespace poher::app
