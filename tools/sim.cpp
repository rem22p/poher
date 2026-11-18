// Bots against bots, as fast as the computer can. A tool for development: it
// shows whether the characters really differ and who wins from whom.
//
//   poher-sim [HANDS] [SEED] [NAME...]
//
// Plays HANDS hands (20 000 unless given) at six-seat tables in sessions of
// 300 hands, each with six bots picked at random from the nine - or from the
// names given, where a name the roster does not know plays the plain style.
// Prints for every bot how it played and how many big blinds it won per
// hundred hands.
//
// A name may carry other numbers for its style, to try them out:
//   poher-sim 30000 1 Vasya:sticky=0.6,read=0.6 P1 P2 P3 P4 P5
//
// With HANDS written as "show:N" it plays N hands at one table and prints
// their record instead, with every hand's hole cards: for reading how the
// bots play.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "host/dealer.hpp"
#include "host/roster.hpp"
#include "ui/lang.hpp"

namespace {

using namespace poher;

struct Tally {
    bots::Notes notes;
    long long hands = 0;
    double won = 0;        // big blinds
    double tilt = 0;       // summed over sessions, for the average
    int sessions = 0;
    int rebuys = 0;
    int showdowns = 0;
    double at_showdown = 0;
    double without = 0;
};

// "Vasya:sticky=0.6,read=0.6": changes the style of that character and returns the bare name.
std::string with_changes(const std::string& text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) return text;
    const std::string name = text.substr(0, colon);
    bots::Character who = bots::character(name);
    static std::vector<std::string> kept;  // characters refer to their names
    kept.push_back(name);
    who.name = kept.back();
    static const std::map<std::string, double bots::Style::*> fields = {
        {"loose", &bots::Style::loose},   {"fancy", &bots::Style::fancy},   {"pre_raise", &bots::Style::pre_raise},
        {"aggr", &bots::Style::aggr},     {"bluff", &bots::Style::bluff},   {"sticky", &bots::Style::sticky},
        {"fear", &bots::Style::fear},     {"trap", &bots::Style::trap},     {"chase", &bots::Style::chase},
        {"tilt", &bots::Style::tilt},     {"guard", &bots::Style::guard},   {"read", &bots::Style::read},
        {"noise", &bots::Style::noise},   {"adapt", &bots::Style::adapt},   {"size", &bots::Style::size},
        {"honest", &bots::Style::honest},
    };
    std::size_t at = colon + 1;
    while (at < text.size()) {
        std::size_t end = text.find(',', at);
        if (end == std::string::npos) end = text.size();
        const std::string pair = text.substr(at, end - at);
        const std::size_t equals = pair.find('=');
        const auto field = fields.find(pair.substr(0, equals));
        if (equals == std::string::npos || field == fields.end()) {
            std::fprintf(stderr, "cannot use \"%s\"\n", pair.c_str());
            std::exit(2);
        }
        for (bots::Style& gear : who.gears) gear.*(field->second) = std::atof(pair.c_str() + equals + 1);
        at = end + 1;
    }
    bots::put_character(std::move(who));
    return name;
}

// Plays `hands` hands at one table and prints what happened in each.
int show(int hands, std::uint64_t seed, std::vector<std::string> names) {
    const game::Stakes stakes{1, 2, 40, 200};
    game::Random random(seed);
    for (std::size_t i = names.size(); i > 1; --i) {
        std::swap(names[i - 1], names[random.below(static_cast<std::uint32_t>(i))]);
    }
    names.resize(std::min<std::size_t>(6, names.size()));
    const int seats = static_cast<int>(names.size());
    host::Dealer dealer(stakes, seats, random.next(), host::Pace::none());
    for (int seat = 0; seat < seats; ++seat) {
        dealer.sit(seat, host::Player{names[static_cast<std::size_t>(seat)], "01", true}, 200);
    }
    for (std::int64_t now = 1; now <= hands; ++now) {
        dealer.tick(now);
        const host::TableView view = dealer.view(-1);
        std::size_t start = view.log.size();
        while (start > 0 && view.log[start - 1].kind != host::LogLine::Kind::NewHand) --start;
        std::printf("--- hand %d ---", view.hand_number);
        // The cards everybody was dealt, which no player sees: this is a tool, not the game.
        for (const game::HandPlayer& player : dealer.table().hand()->players()) {
            std::printf("  %s %s%s (%lld)", names[static_cast<std::size_t>(player.seat)].c_str(),
                        game::to_text(player.cards[0]).c_str(), game::to_text(player.cards[1]).c_str(),
                        static_cast<long long>(player.stack + player.total));
        }
        std::printf("\n");
        for (std::size_t i = start; i < view.log.size(); ++i) {
            const host::LogLine& line = view.log[i];
            std::printf("  %-6s %s\n", line.name.c_str(), ui::log_text(line, ui::Lang::En).c_str());
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]).rfind("show:", 0) == 0) {
        std::vector<std::string> names;
        for (int i = 3; i < argc; ++i) names.push_back(with_changes(argv[i]));
        if (names.empty()) {
            for (const host::Bot& bot : host::kBots) names.emplace_back(bot.name);
        }
        return show(std::atoi(argv[1] + 5), argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1, names);
    }
    const long long total = argc > 1 ? std::atoll(argv[1]) : 20000;
    const std::uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
    std::vector<std::string> names;
    for (int i = 3; i < argc; ++i) names.push_back(with_changes(argv[i]));
    if (names.empty()) {
        for (const host::Bot& bot : host::kBots) names.emplace_back(bot.name);
    }

    const game::Stakes stakes{1, 2, 40, 200};
    const int seats = static_cast<int>(std::min<std::size_t>(6, names.size()));
    if (seats < 2) {
        std::fputs("at least two bots are needed\n", stderr);
        return 2;
    }
    game::Random random(seed);
    std::map<std::string, Tally> tallies;
    long long played = 0;

    while (played < total) {
        // Who sits down this time.
        std::vector<std::string> line_up = names;
        for (std::size_t i = line_up.size(); i > 1; --i) {
            std::swap(line_up[i - 1], line_up[random.below(static_cast<std::uint32_t>(i))]);
        }
        line_up.resize(static_cast<std::size_t>(seats));

        host::Dealer dealer(stakes, seats, random.next(), host::Pace::none());
        std::vector<game::Chips> paid(static_cast<std::size_t>(seats), 0);
        for (int seat = 0; seat < seats; ++seat) {
            dealer.sit(seat, host::Player{line_up[static_cast<std::size_t>(seat)], "01", true}, 200);
            paid[static_cast<std::size_t>(seat)] = 200;
        }
        int hands = 0;
        std::int64_t now = 0;
        while (hands < 300) {
            dealer.tick(++now);
            for (const host::Notice& notice : dealer.take_notices()) {
                if (const auto* added = std::get_if<host::notice::ChipsAdded>(&notice)) {
                    paid[static_cast<std::size_t>(added->seat)] += added->amount;
                    ++tallies[line_up[static_cast<std::size_t>(added->seat)]].rebuys;
                }
                if (std::holds_alternative<host::notice::HandOver>(notice)) ++hands;
            }
        }
        dealer.finish_now();
        played += hands;
        for (int seat = 0; seat < seats; ++seat) {
            const std::string& name = line_up[static_cast<std::size_t>(seat)];
            Tally& tally = tallies[name];
            tally.won += static_cast<double>(dealer.table().seat(seat).stack - paid[static_cast<std::size_t>(seat)]) /
                         static_cast<double>(stakes.big_blind);
            if (const bots::Notes* notes = dealer.notebook().find(name)) {
                tally.hands += notes->hands;
                tally.notes.hands += notes->hands;
                tally.notes.entered += notes->entered;
                tally.notes.raised += notes->raised;
                tally.notes.bets += notes->bets;
                tally.notes.calls += notes->calls;
                tally.notes.faced += notes->faced;
                tally.notes.gave_up += notes->gave_up;
            }
            if (const bots::Brain* brain = dealer.brain(seat)) {
                tally.tilt += brain->tilt();
                tally.showdowns += brain->record().showdowns;
                tally.at_showdown += brain->record().at_showdown;
                tally.without += brain->record().without;
            }
            ++tally.sessions;
        }
    }

    std::printf("%lld hands\n\n", played);
    std::printf("%-8s %8s %6s %6s %6s %6s %8s %7s %7s %6s %7s %6s\n", "bot", "hands", "enter", "raise", "push", "folds",
                "bb/100", "shown", "quiet", "shows", "rebuys", "tilt");
    for (const std::string& name : names) {
        const Tally& tally = tallies[name];
        if (tally.hands == 0) continue;
        const double hundreds = static_cast<double>(tally.hands) / 100;
        std::printf("%-8s %8lld %5.0f%% %5.0f%% %6.2f %5.0f%% %+8.1f %+7.1f %+7.1f %5.0f%% %7d %6.2f\n", name.c_str(),
                    tally.hands, 100 * tally.notes.enters(), 100 * tally.notes.raises(), tally.notes.pushes(),
                    100 * tally.notes.gives_up(), tally.won / hundreds, tally.at_showdown / hundreds,
                    tally.without / hundreds, static_cast<double>(tally.showdowns) / hundreds, tally.rebuys,
                    tally.sessions > 0 ? tally.tilt / tally.sessions : 0.0);
    }
    std::puts("\nenter: put chips in before the flop by choice; raise: raised before the flop;\n"
              "push: bets and raises per call after the flop; folds: gave up when bet at after the flop;\n"
              "shown, quiet: the part of bb/100 from hands with and without a showdown; shows: how often it shows down.");
    return 0;
}
