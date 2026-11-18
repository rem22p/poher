#include "app/profile.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace poher::app {

namespace {

constexpr std::uintmax_t kHistoryLimit = 4 * 1024 * 1024;  // bytes; older hands go when the file grows past it
constexpr std::size_t kHistoryKept = 2000;                 // hands left after that

// Writes the whole file under another name first, so that a crash in the
// middle never leaves half a profile.
void write_file(const std::filesystem::path& file, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::filesystem::path temporary = file;
    temporary += ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) return;
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        // Some systems refuse to rename over an existing file.
        std::filesystem::remove(file, error);
        std::filesystem::rename(temporary, file, error);
    }
}

// Lines of "key rest of the line".
std::vector<std::pair<std::string, std::string>> read_pairs(const std::filesystem::path& file) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t space = line.find(' ');
        if (line.empty()) continue;
        if (space == std::string::npos) {
            pairs.emplace_back(line, "");
        } else {
            pairs.emplace_back(line.substr(0, space), line.substr(space + 1));
        }
    }
    return pairs;
}

long long number(const std::string& text, long long otherwise = 0) {
    try {
        return std::stoll(text);
    } catch (...) {
        return otherwise;
    }
}

// Keeps a name on one line and within reason whatever the file says.
std::string clean(std::string text, std::size_t most_bytes) {
    for (char& c : text) {
        if (static_cast<unsigned char>(c) < 0x20) c = ' ';
    }
    if (text.size() > most_bytes) text.resize(most_bytes);
    return text;
}

void write_notes(std::ostream& out, const bots::Notes& notes) {
    out << notes.hands << ' ' << notes.entered << ' ' << notes.raised << ' ' << notes.bets << ' ' << notes.calls << ' '
        << notes.faced << ' ' << notes.gave_up;
}

// Leaves the notes alone unless all the seven numbers are there and make sense.
void read_notes(std::istream& in, bots::Notes& notes) {
    bots::Notes read;
    in >> read.hands >> read.entered >> read.raised >> read.bets >> read.calls >> read.faced >> read.gave_up;
    if (!in) return;
    for (const int count : {read.hands, read.entered, read.raised, read.bets, read.calls, read.faced, read.gave_up}) {
        if (count < 0) return;
    }
    notes = read;
}

std::string hand_text(const PastHand& hand) {
    std::ostringstream out;
    out << "hand " << hand.number << ' ' << hand.when << ' ' << hand.level << ' ' << hand.result;
    if (hand.cards) out << ' ' << game::to_text((*hand.cards)[0]) << ' ' << game::to_text((*hand.cards)[1]);
    out << '\n';
    for (const host::LogLine& line : hand.lines) {
        out << static_cast<int>(line.kind) << ' ' << (line.bot ? 1 : 0) << ' ' << line.amount << ' ' << line.number
            << ' ';
        if (line.cards.empty()) {
            out << '-';
        } else {
            for (std::size_t i = 0; i < line.cards.size(); ++i) out << (i ? "," : "") << game::to_text(line.cards[i]);
        }
        out << ' ' << clean(line.name, 64) << '\n';
    }
    out << "end\n";
    return out.str();
}

std::vector<PastHand> read_history(const std::filesystem::path& file) {
    std::vector<PastHand> hands;
    std::ifstream in(file, std::ios::binary);
    std::string text;
    bool open = false;
    while (std::getline(in, text)) {
        if (!text.empty() && text.back() == '\r') text.pop_back();
        std::istringstream line(text);
        if (text.rfind("hand ", 0) == 0) {
            PastHand hand;
            std::string word, first, second;
            line >> word >> hand.number >> hand.when >> hand.level >> hand.result >> first >> second;
            game::Card a, b;
            if (game::parse_card(first, a) && game::parse_card(second, b)) hand.cards = {a, b};
            if (hand.level >= host::kLevels.size()) hand.level = 0;
            hands.push_back(std::move(hand));
            open = true;
        } else if (text == "end") {
            open = false;
        } else if (open) {
            host::LogLine entry;
            int kind = 0;
            int bot = 0;
            std::string cards;
            line >> kind >> bot >> entry.amount >> entry.number >> cards;
            if (!line || kind < 0 || kind > static_cast<int>(host::LogLine::Kind::Says)) continue;
            entry.kind = static_cast<host::LogLine::Kind>(kind);
            entry.bot = bot != 0;
            if (cards != "-") {
                for (char& c : cards) {
                    if (c == ',') c = ' ';
                }
                entry.cards = game::parse_cards(cards);
            }
            std::getline(line, entry.name);
            if (!entry.name.empty() && entry.name.front() == ' ') entry.name.erase(0, 1);
            hands.back().lines.push_back(std::move(entry));
        }
    }
    return hands;
}

}  // namespace

Profile::Profile() {
    for (std::size_t i = 0; i < bots.size(); ++i) bots[i].bankroll = host::start_bankroll(host::kBots[i].strength);
}

Storage::Storage(std::filesystem::path folder) : folder_(std::move(folder)) {}

void Storage::load(Settings& settings) const {
    if (!enabled()) return;
    for (const auto& [key, value] : read_pairs(folder_ / "settings.txt")) {
        if (key == "lang") settings.lang = value == "ru" ? ui::Lang::Ru : ui::Lang::En;
        if (key == "hint") settings.hint = static_cast<ui::Hint>(std::clamp<long long>(number(value, 1), 0, 2));
        if (key == "sound") settings.sound = number(value, 1) != 0;
        if (key == "talk") settings.talk = number(value, 1) != 0;
    }
}

void Storage::save(const Settings& settings) const {
    if (!enabled()) return;
    std::ostringstream out;
    out << "lang " << (settings.lang == ui::Lang::Ru ? "ru" : "en") << '\n'
        << "hint " << static_cast<int>(settings.hint) << '\n'
        << "sound " << (settings.sound ? 1 : 0) << '\n'
        << "talk " << (settings.talk ? 1 : 0) << '\n';
    write_file(folder_ / "settings.txt", out.str());
}

void Storage::load(Profile& profile) const {
    if (!enabled()) return;
    Stats& stats = profile.stats;
    for (const auto& [key, value] : read_pairs(folder_ / "profile.txt")) {
        std::istringstream words(value);
        if (key == "nick") profile.nick = clean(value, kNickLength * 4);
        if (key == "avatar" && !value.empty()) profile.avatar = clean(value, 32);
        if (key == "bankroll") profile.bankroll = std::max<Chips>(number(value, profile.bankroll), 0);
        if (key == "refills") profile.refills = static_cast<int>(std::max<long long>(number(value), 0));
        if (key == "hands_won") stats.hands_won = number(value);
        if (key == "biggest_pot") stats.biggest_pot = number(value);
        if (key == "from_bots") stats.from_bots = number(value);
        if (key == "from_people") stats.from_people = number(value);
        if (key == "showdowns") stats.showdowns = number(value);
        if (key == "showdowns_won") stats.showdowns_won = number(value);
        if (key == "play") read_notes(words, stats.play);
        if (key == "seen") read_notes(words, profile.seen);
        if (key == "person") {
            std::string id;
            words >> id;
            bots::Notes notes;
            read_notes(words, notes);
            if (id.size() == 64 && notes.hands > 0) profile.people[id] = notes;
        }
        if (key == "bot") {
            std::string name;
            BotAccount account;
            words >> name >> account.bankroll >> account.refills >> account.hands >> account.won;
            if (!words || account.bankroll < 0) continue;
            read_notes(words, account.seen);
            for (std::size_t i = 0; i < host::kBots.size(); ++i) {
                if (host::kBots[i].name == name) profile.bots[i] = account;
            }
        }
    }
}

void Storage::save(const Profile& profile) const {
    if (!enabled()) return;
    const Stats& stats = profile.stats;
    std::ostringstream out;
    out << "nick " << clean(profile.nick, kNickLength * 4) << '\n'
        << "avatar " << profile.avatar << '\n'
        << "bankroll " << profile.bankroll << '\n'
        << "refills " << profile.refills << '\n'
        << "hands_won " << stats.hands_won << '\n'
        << "biggest_pot " << stats.biggest_pot << '\n'
        << "from_bots " << stats.from_bots << '\n'
        << "from_people " << stats.from_people << '\n'
        << "showdowns " << stats.showdowns << '\n'
        << "showdowns_won " << stats.showdowns_won << '\n';
    out << "play ";
    write_notes(out, stats.play);
    out << "\nseen ";
    write_notes(out, profile.seen);
    out << '\n';
    for (std::size_t i = 0; i < host::kBots.size(); ++i) {
        const BotAccount& account = profile.bots[i];
        out << "bot " << host::kBots[i].name << ' ' << account.bankroll << ' ' << account.refills << ' '
            << account.hands << ' ' << account.won << ' ';
        write_notes(out, account.seen);
        out << '\n';
    }
    for (const auto& [id, notes] : profile.people) {
        out << "person " << id << ' ';
        write_notes(out, notes);
        out << '\n';
    }
    write_file(folder_ / "profile.txt", out.str());
}

void Storage::add(const PastHand& hand) const {
    if (!enabled()) return;
    const std::filesystem::path file = folder_ / "history.txt";
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    // Grown too big: keep the latest hands only.
    if (std::filesystem::file_size(file, error) > kHistoryLimit && !error) {
        const std::vector<PastHand> all = read_history(file);
        std::string text;
        for (std::size_t i = all.size() > kHistoryKept ? all.size() - kHistoryKept : 0; i < all.size(); ++i) {
            text += hand_text(all[i]);
        }
        write_file(file, text);
    }
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out << hand_text(hand);
}

void Storage::save_bytes(const std::string& name, const std::vector<std::uint8_t>& bytes) const {
    if (!enabled()) return;
    write_file(folder_ / name, std::string(bytes.begin(), bytes.end()));
}

std::vector<std::uint8_t> Storage::load_bytes(const std::string& name) const {
    if (!enabled()) return {};
    std::ifstream in(folder_ / name, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<PastHand> Storage::history(std::size_t most) const {
    if (!enabled()) return {};
    std::vector<PastHand> all = read_history(folder_ / "history.txt");
    std::vector<PastHand> latest;
    for (std::size_t i = all.size(); i > 0 && latest.size() < most; --i) latest.push_back(std::move(all[i - 1]));
    return latest;
}

}  // namespace poher::app
