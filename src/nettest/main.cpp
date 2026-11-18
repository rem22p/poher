// POHER network check.
//
// Friends run this small program at the same time and exchange codes. It shows
// whether their computers find each other without a server, how long that
// takes, whether the link is direct or goes through a relay node, and how fast
// and stable it is. At the end it prints a report and saves it to a file.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "net/tox_network.hpp"

namespace {

using namespace poher;
using net::Link;
using net::PeerId;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

constexpr const char* kVersion = "0.1";

#if defined(_WIN32)
constexpr const char* kSystem = "Windows";
#elif defined(__APPLE__)
constexpr const char* kSystem = "macOS";
#elif defined(__linux__)
constexpr const char* kSystem = "Linux";
#else
constexpr const char* kSystem = "other";
#endif

constexpr const char* kProfileFile = "poher-nettest.profile";
constexpr const char* kReportFile = "poher-nettest-report.txt";

constexpr auto kReliableEvery = seconds(2);
constexpr auto kDatagramEvery = seconds(1);
constexpr auto kStatusEvery = seconds(30);
constexpr auto kDrain = seconds(2);          // how long to wait for the last answers before the report
constexpr auto kTick = milliseconds(5);      // the longest sleep between polls: keeps timings precise
constexpr std::size_t kReliableSize = 200;   // about the size of a game message
constexpr std::size_t kDatagramSize = 40;
constexpr int kGiveUpMinutes = 60;           // stop if nobody was found in this time

// First byte of every message this program sends.
enum : std::uint8_t { kPing = 1, kPong = 2, kInfo = 3, kBye = 4 };
constexpr std::size_t kPingHeader = 13;      // kind, number (4 bytes), send time in ms (8 bytes)

// ---- Text -------------------------------------------------------------------

bool g_russian = false;

const char* tr(const char* english, const char* russian) {
    return g_russian ? russian : english;
}

// Puts the values in place of each "{}" in the text, in order.
std::string fill(std::string text, std::initializer_list<std::string> values) {
    std::size_t at = 0;
    for (const std::string& value : values) {
        at = text.find("{}", at);
        if (at == std::string::npos) break;
        text.replace(at, 2, value);
        at += value.size();
    }
    return text;
}

std::string clock_text(Clock::duration time) {
    const long long total = std::chrono::duration_cast<seconds>(time).count();
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "%02lld:%02lld", total / 60, total % 60);
    return buffer;
}

std::string link_text(Link link) {
    switch (link) {
        case Link::Direct: return tr("direct (UDP)", "напрямую (UDP)");
        case Link::Relay: return tr("through a relay node (TCP)", "через промежуточный узел (TCP)");
        default: return tr("offline", "нет связи");
    }
}

// The same in the fixed wording of the report, which is always in English.
const char* link_word(Link link) {
    switch (link) {
        case Link::Direct: return "direct (UDP)";
        case Link::Relay: return "relay (TCP)";
        default: return "none";
    }
}

// True if the bytes are correctly encoded UTF-8 text.
bool is_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > text.size()) return false;
        for (std::size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += length;
    }
    return true;
}

// Text that came from the keyboard or from another computer: no control
// characters (they could repaint the terminal), no outer spaces, limited length.
std::string clean(std::string_view text, std::size_t limit) {
    // A console in an old encoding gives letters we cannot show: keep the plain ones.
    const bool plain_only = !is_utf8(text);
    std::string out;
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte != 0x7F && !(plain_only && byte >= 0x80)) out += c;
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    if (out.size() > limit) {
        out.resize(limit);
        // Do not leave half of a multi-byte letter at the end.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80) != 0) out.pop_back();
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void print(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

bool system_is_russian() {
#ifdef _WIN32
    return (GetUserDefaultUILanguage() & 0x3FF) == LANG_RUSSIAN;
#else
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(variable);
        if (value != nullptr && *value != '\0') return std::strncmp(value, "ru", 2) == 0;
    }
    return false;
#endif
}

// ---- Measurements -----------------------------------------------------------

// Round-trip times in milliseconds.
struct Series {
    std::vector<int> values;

    int count() const { return static_cast<int>(values.size()); }
    int min() const { return values.empty() ? 0 : *std::min_element(values.begin(), values.end()); }
    int max() const { return values.empty() ? 0 : *std::max_element(values.begin(), values.end()); }

    int average() const {
        if (values.empty()) return 0;
        long long sum = 0;
        for (int value : values) sum += value;
        return static_cast<int>(sum / count());
    }

    // The value that `percent` of the round trips did not exceed.
    int percentile(int percent) const {
        if (values.empty()) return 0;
        std::vector<int> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const auto rank = static_cast<std::size_t>(std::ceil(percent / 100.0 * count()));
        return sorted[std::max<std::size_t>(rank, 1) - 1];
    }

    std::string text() const {
        return std::to_string(min()) + " / " + std::to_string(average()) + " / " + std::to_string(percentile(95)) +
               " / " + std::to_string(max());
    }
};

struct FriendState {
    std::string name;
    std::string system;       // what the friend's program reported about itself
    std::string their_link;   // how the friend is connected to the network

    Link link = Link::None;
    Clock::time_point since;          // start of the search, later the moment of the last link change
    std::optional<int> found_after;   // seconds from the start of the search
    milliseconds direct{0}, relay{0}, offline{0};  // time in each state after the friend was found
    int drops = 0;
    bool left = false;                // the friend's program said it has finished

    std::uint32_t pings = 0;
    int reliable_sent = 0;
    int datagram_sent = 0;
    Series reliable;
    Series datagram;
    Series recent;                    // reliable round trips since the last status line
    Clock::time_point next_reliable;
    Clock::time_point next_datagram;
};

struct App {
    std::unique_ptr<net::Network> network;
    std::filesystem::path profile_path;
    std::filesystem::path report_path;
    bool relay_only = false;
    int minutes = 15;

    Clock::time_point start = Clock::now();
    std::string started_utc;
    Link link = Link::None;
    std::optional<int> online_after;  // seconds from the start
    int drops = 0;
    std::map<PeerId, FriendState> friends;
    std::optional<Clock::time_point> first_found;
};

void say(const App& app, const std::string& text) {
    print("[" + clock_text(Clock::now() - app.start) + "] " + text + "\n");
}

std::string label(const PeerId& id, const FriendState& state) {
    return state.name.empty() ? net::short_id(id) : state.name;
}

long long now_ms(const App& app) {
    return std::chrono::duration_cast<milliseconds>(Clock::now() - app.start).count();
}

int seconds_between(Clock::time_point from, Clock::time_point to) {
    return static_cast<int>(std::chrono::duration_cast<seconds>(to - from).count());
}

// Adds the time since the last link change to the counter of the state the friend was in.
void account(FriendState& state, Clock::time_point now) {
    const auto span = std::chrono::duration_cast<milliseconds>(now - state.since);
    switch (state.link) {
        case Link::Direct: state.direct += span; break;
        case Link::Relay: state.relay += span; break;
        default:
            // A friend who finished their check is not a lost connection.
            if (!state.left) state.offline += span;
            break;
    }
    state.since = now;
}

// ---- Files ------------------------------------------------------------------

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Writes next to the target first, so that a crash never leaves half a file.
bool write_file(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::filesystem::path draft = path;
    draft += ".new";
    {
        std::ofstream file(draft, std::ios::binary | std::ios::trunc);
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!file) return false;
    }
    std::error_code error;
    std::filesystem::rename(draft, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        std::filesystem::rename(draft, path, error);
    }
    return !error;
}

// A path as UTF-8 text. On Windows path.string() would use the old system
// encoding and break on folders with, say, Russian letters.
std::string path_text(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

void save_profile(const App& app) {
    const std::vector<std::uint8_t> data = app.network->save();
    write_file(app.profile_path, data.data(), data.size());
}

// ---- Messages between two copies of this program ----------------------------

void put_number(std::uint8_t* at, std::uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) at[i] = static_cast<std::uint8_t>(value >> (8 * i));
}

std::uint64_t get_number(const std::uint8_t* at, int bytes) {
    std::uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) value |= static_cast<std::uint64_t>(at[i]) << (8 * i);
    return value;
}

std::vector<std::uint8_t> make_ping(const App& app, FriendState& state, std::size_t size) {
    std::vector<std::uint8_t> ping(size, 0);
    ping[0] = kPing;
    put_number(&ping[1], ++state.pings, 4);
    put_number(&ping[5], static_cast<std::uint64_t>(now_ms(app)), 8);
    return ping;
}

// Tells a friend what we are running on and how we are connected.
void send_info(App& app, const PeerId& to) {
    const std::string text = std::string(kSystem) + "|" + kVersion + "|" + link_word(app.link);
    std::vector<std::uint8_t> message{kInfo};
    message.insert(message.end(), text.begin(), text.end());
    app.network->send(to, message);
}

// A ping is sent straight back; a returning pong gives one round-trip time.
// Returns that time, or nothing if the message was not an answer to our ping.
std::optional<int> handle_ping(App& app, const PeerId& from, net::Bytes data, bool reliable) {
    if (data.size() < kPingHeader) return std::nullopt;
    if (data[0] == kPing) {
        std::vector<std::uint8_t> pong(data.begin(), data.end());
        pong[0] = kPong;
        if (reliable) {
            app.network->send(from, pong);
        } else {
            app.network->send_datagram(from, pong);
        }
        return std::nullopt;
    }
    if (data[0] != kPong) return std::nullopt;
    const long long trip = now_ms(app) - static_cast<long long>(get_number(&data[5], 8));
    if (trip < 0 || trip > 600'000) return std::nullopt;
    return static_cast<int>(trip);
}

// ---- Report -----------------------------------------------------------------

const char* verdict(const FriendState& state, milliseconds online, milliseconds offline) {
    if (state.reliable.count() == 0) return "POOR";
    const double total = static_cast<double>((online + offline).count());
    const double share = total > 0 ? static_cast<double>(online.count()) / total : 0;
    const double answered = static_cast<double>(state.reliable.count()) / std::max(state.reliable_sent, 1);
    const int slow = state.reliable.percentile(95);
    if (share >= 0.98 && answered >= 0.95 && slow < 1000) return "GOOD";
    if (share >= 0.90 && answered >= 0.80 && slow < 3000) return "PLAYABLE";
    return "POOR";
}

std::string report(const App& app) {
    const Clock::time_point now = Clock::now();
    std::string out = "===== POHER network check =====\n";
    out += std::string("version    ") + kVersion + ", tox " + net::tox_library_version() + "\n";
    out += std::string("system     ") + kSystem + "\n";
    out += "name       " + app.network->name() + " [" + net::short_id(app.network->id()) + "]\n";
    out += "started    " + app.started_utc + " UTC, ran " + clock_text(now - app.start) + "\n";
    if (app.relay_only) out += "mode       relay only (--tcp)\n";
    if (app.online_after) {
        out += "network    online after " + std::to_string(*app.online_after) + " s, now " + link_word(app.link) +
               ", lost " + std::to_string(app.drops) + " times\n";
    } else {
        out += "network    NEVER GOT ONLINE\n";
    }
    if (app.friends.empty()) out += "\nno friends were added\n";

    for (const auto& [id, saved] : app.friends) {
        FriendState state = saved;
        out += "\nfriend     " + label(id, state) + " [" + net::short_id(id) + "]";
        if (!state.system.empty()) out += ", " + state.system;
        out += "\n";
        if (!state.found_after) {
            out += "  result   NOT FOUND in " + clock_text(now - state.since) + "\n";
            continue;
        }
        account(state, now);
        out += "  found    after " + std::to_string(*state.found_after) + " s\n";
        out += "  link     direct " + clock_text(state.direct) + ", relay " + clock_text(state.relay) + ", offline " +
               clock_text(state.offline) + ", lost " + std::to_string(state.drops) + " times\n";
        if (!state.their_link.empty()) out += "  theirs   " + state.their_link + "\n";
        if (state.left) out += "  note     finished the check before us\n";
        out += "  reliable sent " + std::to_string(state.reliable_sent) + ", answered " +
               std::to_string(state.reliable.count()) + ", ms min / avg / p95 / max: " + state.reliable.text() + "\n";
        const int lost = std::max(state.datagram_sent - state.datagram.count(), 0);
        char percent[16];
        std::snprintf(percent, sizeof percent, "%.1f", 100.0 * lost / std::max(state.datagram_sent, 1));
        out += "  datagram sent " + std::to_string(state.datagram_sent) + ", lost " + std::to_string(lost) + " (" +
               percent + "%), ms min / avg / p95 / max: " + state.datagram.text() + "\n";
        out += std::string("  result   ") + verdict(state, state.direct + state.relay, state.offline) + "\n";
    }
    out += "===============================\n";
    return out;
}

void save_report(const App& app) {
    const std::string text = report(app);
    write_file(app.report_path, text.data(), text.size());
}

// ---- Events from the network ------------------------------------------------

// Starts keeping records for a friend, or returns the ones we already have.
FriendState& track(App& app, const PeerId& id) {
    const auto [place, added] = app.friends.try_emplace(id);
    if (added) place->second.since = Clock::now();
    return place->second;
}

// Picks up friends the network knows about but we have no records for:
// saved in the profile by an earlier run, or just added by code.
void track_all(App& app) {
    for (const net::Friend& known : app.network->friends()) {
        FriendState& state = track(app, known.id);
        if (state.name.empty()) state.name = clean(known.name, 32);
    }
}

void on_self_link(App& app, Link link) {
    if (link != Link::None && !app.online_after) app.online_after = seconds_between(app.start, Clock::now());
    if (link == Link::None && app.link != Link::None) ++app.drops;
    app.link = link;
    if (link == Link::None) {
        say(app, tr("Lost the network", "Сеть пропала"));
    } else {
        say(app, fill(tr("In the network: {}", "В сети: {}"), {link_text(link)}));
    }
    for (const auto& [id, state] : app.friends) {
        if (state.link != Link::None) send_info(app, id);
    }
}

void on_friend_request(App& app, const PeerId& id) {
    if (!app.network->accept_friend(id)) return;
    track(app, id);
    save_profile(app);
    say(app, fill(tr("Request from {} accepted, searching...", "Заявка от {} принята, ищу..."), {net::short_id(id)}));
}

void on_friend_link(App& app, const PeerId& id, Link link) {
    FriendState& state = track(app, id);
    const Clock::time_point now = Clock::now();
    const std::string who = label(id, state);
    const Link before = state.link;

    if (!state.found_after) {
        if (link == Link::None) return;
        state.found_after = seconds_between(state.since, now);
        state.since = now;
        if (!app.first_found) app.first_found = now;
        say(app, fill(tr("{} found after {} s: {}", "{} найден за {} с: {}"),
                      {who, std::to_string(*state.found_after), link_text(link)}));
    } else {
        account(state, now);
        if (link == Link::None) {
            if (!state.left) {
                ++state.drops;
                say(app, fill(tr("{}: connection lost", "{}: связь пропала"), {who}));
            }
        } else if (before == Link::None) {
            state.left = false;
            say(app, fill(tr("{} is back: {}", "{} снова на связи: {}"), {who, link_text(link)}));
        } else {
            say(app, fill(tr("{}: now {}", "{}: теперь {}"), {who, link_text(link)}));
        }
    }

    state.link = link;
    if (link != Link::None && before == Link::None) {
        state.next_reliable = now;
        state.next_datagram = now;
        send_info(app, id);
    }
}

void on_message(App& app, const PeerId& id, net::Bytes data) {
    if (data.empty()) return;
    FriendState& state = track(app, id);
    if (data[0] == kBye) {
        state.left = true;
        say(app, fill(tr("{} has finished the check", "{} закончил проверку"), {label(id, state)}));
        return;
    }
    if (data[0] == kInfo) {
        // "system|version|link"
        const std::string text = clean(std::string_view(reinterpret_cast<const char*>(data.data()) + 1,
                                                        data.size() - 1), 64);
        const std::size_t first = text.find('|');
        const std::size_t second = text.find('|', first == std::string::npos ? first : first + 1);
        if (second == std::string::npos) return;
        state.system = text.substr(0, first) + ", version " + text.substr(first + 1, second - first - 1);
        state.their_link = text.substr(second + 1);
        return;
    }
    if (const auto trip = handle_ping(app, id, data, true)) {
        state.reliable.values.push_back(*trip);
        state.recent.values.push_back(*trip);
    }
}

void on_datagram(App& app, const PeerId& id, net::Bytes data) {
    FriendState& state = track(app, id);
    if (const auto trip = handle_ping(app, id, data, false)) state.datagram.values.push_back(*trip);
}

// ---- Regular work -----------------------------------------------------------

void send_pings(App& app) {
    const Clock::time_point now = Clock::now();
    for (auto& [id, state] : app.friends) {
        if (state.link == Link::None) continue;
        if (now >= state.next_reliable) {
            state.next_reliable = now + kReliableEvery;
            if (app.network->send(id, make_ping(app, state, kReliableSize))) ++state.reliable_sent;
        }
        if (now >= state.next_datagram) {
            state.next_datagram = now + kDatagramEvery;
            if (app.network->send_datagram(id, make_ping(app, state, kDatagramSize))) ++state.datagram_sent;
        }
    }
}

// True when at least one friend was found and every found friend has finished.
bool everyone_left(const App& app) {
    bool any = false;
    for (const auto& [id, state] : app.friends) {
        if (!state.found_after) continue;
        if (!state.left) return false;
        any = true;
    }
    return any;
}

// One line per friend every half a minute, so it is clear the program is alive.
void print_status(App& app) {
    const Clock::time_point now = Clock::now();
    if (app.link == Link::None) {
        say(app, tr("No network yet, still trying...", "Сети пока нет, пробую дальше..."));
    } else if (app.friends.empty()) {
        say(app, tr("Waiting for a friend's code. If your friend entered yours, they will appear here.",
                    "Жду код друга. Если друг ввёл твой код, он появится здесь сам."));
    }
    for (auto& [id, state] : app.friends) {
        const std::string who = label(id, state);
        if (!state.found_after) {
            say(app, fill(tr("Searching for {}: {}", "Ищу {}: {}"), {who, clock_text(now - state.since)}));
        } else if (state.left) {
            // Nothing to report: they are done.
        } else if (state.link == Link::None) {
            say(app, fill(tr("{}: offline for {}", "{}: нет связи уже {}"), {who, clock_text(now - state.since)}));
        } else if (state.recent.count() == 0) {
            say(app, fill(tr("{}: {}, no answers in the last 30 s", "{}: {}, нет ответов за последние 30 с"),
                          {who, link_text(state.link)}));
        } else {
            // The last few datagrams may still be on their way: do not count them as lost.
            const int lost = std::max(state.datagram_sent - state.datagram.count() - 3, 0);
            say(app, fill(tr("{}: {}, answer in {} ms, lost {} of {}", "{}: {}, ответ за {} мс, потеряно {} из {}"),
                          {who, link_text(state.link), std::to_string(state.recent.average()), std::to_string(lost),
                           std::to_string(state.datagram_sent)}));
        }
        state.recent.values.clear();
    }
    save_report(app);
}

// ---- Keyboard ---------------------------------------------------------------

// Lines typed by the user. A separate thread reads them, because reading the
// keyboard blocks and the network must be polled all the time.
struct Input {
    std::mutex mutex;
    std::deque<std::string> lines;
    bool closed = false;

    std::optional<std::string> take() {
        const std::lock_guard lock(mutex);
        if (lines.empty()) return std::nullopt;
        std::string line = std::move(lines.front());
        lines.pop_front();
        return line;
    }

    bool is_closed() {
        const std::lock_guard lock(mutex);
        return closed;
    }
};

Input g_input;

void read_keyboard() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const std::lock_guard lock(g_input.mutex);
        g_input.lines.push_back(line);
    }
    const std::lock_guard lock(g_input.mutex);
    g_input.closed = true;
}

volatile std::sig_atomic_t g_interrupted = 0;

void on_interrupt(int) {
    g_interrupted = 1;
}

void add_friend(App& app, const std::string& code) {
    switch (app.network->add_friend(code, "POHER network check")) {
        case net::AddResult::Ok:
            track_all(app);
            save_profile(app);
            say(app, tr("Friend added, searching...", "Друг добавлен, ищу..."));
            break;
        case net::AddResult::AlreadyAdded:
            say(app, tr("This friend is already added.", "Этот друг уже добавлен."));
            break;
        case net::AddResult::OwnCode:
            say(app, tr("This is your own code. Paste your friend's one.", "Это твой собственный код. Нужен код друга."));
            break;
        case net::AddResult::BadCode:
            say(app, tr("This does not look like a code: it is 76 letters and digits. Copy it whole.",
                        "Это не похоже на код: в нём 76 букв и цифр. Скопируй его целиком."));
            break;
        default:
            say(app, tr("Could not add this friend.", "Не получилось добавить друга."));
            break;
    }
}

// Returns false when the user asked to finish.
bool handle_line(App& app, const std::string& raw) {
    const std::string line = clean(raw, 200);
    if (line.empty()) return true;
    for (const char* word : {"q", "Q", "quit", "exit", "й", "Й"}) {
        if (line == word) return false;
    }
    add_friend(app, line);
    return true;
}

// ---- Start ------------------------------------------------------------------

struct Arguments {
    std::string name;
    std::string language;
    std::filesystem::path folder = ".";
    std::vector<std::string> codes;
    int minutes = 15;
    bool relay_only = false;
    bool lan = true;
    bool help = false;
    std::string unknown;  // an option we do not have, or one that lacks its value
};

Arguments parse(int argc, char** argv) {
    Arguments arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        const bool has_value = i + 1 < argc;
        if (key == "--name" && has_value) {
            arguments.name = argv[++i];
        } else if (key == "--lang" && has_value) {
            arguments.language = argv[++i];
        } else if (key == "--dir" && has_value) {
            arguments.folder = argv[++i];
        } else if (key == "--add" && has_value) {
            arguments.codes.emplace_back(argv[++i]);
        } else if (key == "--minutes" && has_value) {
            arguments.minutes = std::max(std::atoi(argv[++i]), 1);
        } else if (key == "--tcp") {
            arguments.relay_only = true;
        } else if (key == "--no-lan") {
            arguments.lan = false;
        } else {
            arguments.help = true;
            if (key != "--help" && key != "-h") arguments.unknown = key;
        }
    }
    return arguments;
}

const char* kHelp =
    "POHER network check\n"
    "\n"
    "Run it without options, send your code to a friend, paste theirs.\n"
    "\n"
    "  --name NAME    the name friends see (asked on the first run otherwise)\n"
    "  --add CODE     add a friend right away; can be repeated\n"
    "  --minutes N    how long to measure after the first friend is found (15)\n"
    "  --lang en|ru   language (taken from the system otherwise)\n"
    "  --dir FOLDER   where to keep the profile and the report (current folder)\n"
    "  --tcp          use only relay nodes, as if direct connections were blocked\n"
    "  --no-lan       do not look for friends on the local network\n";

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", std::gmtime(&now));
    return buffer;
}

int run(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    const Arguments arguments = parse(argc, argv);
    if (arguments.help) {
        if (!arguments.unknown.empty()) print("Cannot use the option: " + arguments.unknown + "\n\n");
        print(kHelp);
        return arguments.unknown.empty() ? 0 : 2;
    }
    g_russian = arguments.language.empty() ? system_is_russian() : arguments.language == "ru";

    App app;
    app.started_utc = utc_now();
    app.minutes = arguments.minutes;
    app.relay_only = arguments.relay_only;
    std::error_code ignored;
    std::filesystem::create_directories(arguments.folder, ignored);
    app.profile_path = std::filesystem::absolute(arguments.folder / kProfileFile);
    app.report_path = std::filesystem::absolute(arguments.folder / kReportFile);

    print(std::string(tr("POHER network check ", "POHER: проверка сети ")) + kVersion + "\n\n");

    net::Events events;
    events.self_link = [&app](Link link) { on_self_link(app, link); };
    events.friend_request = [&app](const PeerId& id, std::string_view) { on_friend_request(app, id); };
    events.friend_link = [&app](const PeerId& id, Link link) { on_friend_link(app, id, link); };
    events.friend_name = [&app](const PeerId& id, std::string_view name) { track(app, id).name = clean(name, 32); };
    events.message = [&app](const PeerId& id, net::Bytes data) { on_message(app, id, data); };
    events.datagram = [&app](const PeerId& id, net::Bytes data) { on_datagram(app, id, data); };

    net::ToxConfig config;
    config.udp = !arguments.relay_only;
    config.lan = arguments.lan;
    config.saved = read_file(app.profile_path);
    const bool first_run = config.saved.empty();

    std::string error;
    app.network = net::make_tox_network(config, events, error);
    if (!app.network && !first_run) {
        print(fill(tr("The saved profile could not be read ({}), starting a new one.\n",
                      "Сохранённый профиль не читается ({}), начинаю новый.\n"), {error}));
        config.saved.clear();
        app.network = net::make_tox_network(config, events, error);
    }
    if (!app.network) {
        print(fill(tr("Could not start the network: {}\n", "Не получилось запустить сеть: {}\n"), {error}));
        return 1;
    }

    std::string name = clean(arguments.name, 32);
    if (name.empty() && app.network->name().empty()) {
        print(tr("Your name (friends will see it), then Enter: ", "Твоё имя (его увидят друзья), потом Enter: "));
        std::string typed;
        if (std::getline(std::cin, typed)) name = clean(typed, 32);
        if (name.empty()) name = "Player-" + net::short_id(app.network->id());
    }
    if (!name.empty()) app.network->set_name(name);
    save_profile(app);
    track_all(app);

    print(std::string("\n") + tr("Your code. Send it to your friend:", "Твой код. Отправь его другу:") + "\n\n  " +
          app.network->code() + "\n\n" +
          tr("Paste your friend's code here and press Enter.\n"
             "If your friend has already entered yours, just wait.\n"
             "To finish early and see the report, type q and press Enter.\n",
             "Вставь сюда код друга и нажми Enter.\n"
             "Если друг уже ввёл твой код, просто подожди.\n"
             "Чтобы закончить раньше и увидеть отчёт, набери q и нажми Enter.\n") +
          "\n");
    say(app, tr("Connecting to the network...", "Подключаюсь к сети..."));

    for (const std::string& code : arguments.codes) add_friend(app, code);

    std::signal(SIGINT, on_interrupt);
    std::thread(read_keyboard).detach();

    Clock::time_point next_status = Clock::now() + kStatusEvery;
    bool running = true;
    while (running) {
        const milliseconds pause = app.network->poll();
        send_pings(app);

        while (const auto line = g_input.take()) {
            if (!handle_line(app, *line)) running = false;
        }
        if (g_interrupted) running = false;

        const Clock::time_point now = Clock::now();
        if (now >= next_status) {
            next_status = now + kStatusEvery;
            print_status(app);
        }
        if (app.first_found && now - *app.first_found >= std::chrono::minutes(app.minutes)) {
            say(app, fill(tr("Time is up ({} min), finishing.", "Время вышло ({} мин), заканчиваю."),
                          {std::to_string(app.minutes)}));
            running = false;
        }
        if (everyone_left(app)) {
            say(app, tr("All friends have finished, finishing too.", "Все друзья закончили, заканчиваю тоже."));
            running = false;
        }
        if (!app.first_found && now - app.start >= std::chrono::minutes(kGiveUpMinutes)) {
            say(app, tr("Nobody was found in an hour, finishing.", "За час никто не нашёлся, заканчиваю."));
            running = false;
        }
        std::this_thread::sleep_for(std::min(pause, kTick));
    }

    // Tell the friends we are leaving on purpose, so they do not count it as a lost connection.
    for (const auto& [id, state] : app.friends) {
        if (state.link != Link::None) app.network->send(id, std::vector<std::uint8_t>{kBye});
    }
    // No new pings; give the answers that are on their way a moment to arrive.
    const Clock::time_point drained = Clock::now() + kDrain;
    while (Clock::now() < drained) {
        app.network->poll();
        std::this_thread::sleep_for(kTick);
    }

    save_profile(app);
    const std::string text = report(app);
    const bool saved = write_file(app.report_path, text.data(), text.size());
    print("\n" + text + "\n");
    if (saved) {
        print(std::string(tr("The report is also saved here:", "Отчёт также сохранён здесь:")) + "\n  " +
              path_text(app.report_path) + "\n");
    }
    print(std::string(tr("Send it to whoever asked you to run this check.",
                         "Отправь его тому, кто попросил запустить проверку.")) + "\n");

#ifdef _WIN32
    // A window opened by a double click closes together with the program.
    if (!g_interrupted && !g_input.is_closed()) {
        print(std::string("\n") + tr("Press Enter to close.", "Нажми Enter, чтобы закрыть.") + "\n");
        while (!g_input.take() && !g_input.is_closed()) std::this_thread::sleep_for(milliseconds(50));
    }
#endif
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const int code = run(argc, argv);
    std::fflush(stdout);
    // The keyboard thread may still be waiting for a line: leave without waiting for it.
    std::_Exit(code);
}
