// What bots remember about the players they have met: plain counts of how
// someone plays, gathered from what everybody at the table can see.
#pragma once

#include <map>
#include <string>
#include <string_view>

namespace poher::bots {

struct Notes {
    int hands = 0;     // was dealt in
    int entered = 0;   // put chips in before the flop by choice
    int raised = 0;    // raised before the flop
    int bets = 0;      // bets and raises after the flop
    int calls = 0;     // calls after the flop
    int faced = 0;     // had to answer a bet after the flop
    int gave_up = 0;   // and folded

    // The usual numbers of poker trackers. With nothing seen yet they give
    // the values of an ordinary player.
    double enters() const { return hands > 0 ? static_cast<double>(entered) / hands : 0.28; }
    double raises() const { return hands > 0 ? static_cast<double>(raised) / hands : 0.18; }
    double pushes() const { return calls > 0 ? static_cast<double>(bets) / calls : (bets > 0 ? 4.0 : 2.0); }
    double gives_up() const { return faced > 0 ? static_cast<double>(gave_up) / faced : 0.45; }
};

class Notebook {
public:
    Notes& of(std::string_view player) {
        const auto found = pages_.find(player);
        if (found != pages_.end()) return found->second;
        return pages_.emplace(std::string(player), Notes{}).first->second;
    }
    const Notes* find(std::string_view player) const {
        const auto found = pages_.find(player);
        return found != pages_.end() ? &found->second : nullptr;
    }
    const std::map<std::string, Notes, std::less<>>& pages() const { return pages_; }

private:
    std::map<std::string, Notes, std::less<>> pages_;
};

}  // namespace poher::bots
