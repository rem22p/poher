// POHER: the game in a terminal. This file connects the game to the terminal
// through the FTXUI library: keys come in, the picture goes out.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>

#include "app/game.hpp"
#include "app/paths.hpp"
#include "net/tox_network.hpp"

namespace {

using poher::app::Game;
using poher::app::Key;

using poher::app::kVersion;

// Shows the picture of the game in the whole window.
class Picture : public ftxui::Node {
public:
    explicit Picture(Game& game) : game_(game) {}

    void ComputeRequirement() override {
        requirement_.min_x = 1;
        requirement_.min_y = 1;
        requirement_.flex_grow_x = 1;
        requirement_.flex_grow_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        const int width = box_.x_max - box_.x_min + 1;
        const int height = box_.y_max - box_.y_min + 1;
        poher::ui::Canvas canvas(width, height);
        game_.draw(canvas);
        std::string glyph;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const poher::ui::Cell& cell = canvas.cell(x, y);
                ftxui::Cell& out = screen.CellAt(box_.x_min + x, box_.y_min + y);
                glyph.clear();
                // The second half of a wide glyph stays empty: the glyph before it covers it.
                if (cell.glyph != U'\0') poher::ui::append_utf8(glyph, cell.glyph);
                out.character = glyph;
                out.foreground_color = ftxui::Color::RGB(cell.fg.r, cell.fg.g, cell.fg.b);
                out.background_color = ftxui::Color::RGB(cell.bg.r, cell.bg.g, cell.bg.b);
                out.bold = cell.bold;
            }
        }
    }

private:
    Game& game_;
};

// The keys of an event. Pasted text comes as one event with many letters.
std::vector<Key> to_keys(const ftxui::Event& event) {
    using ftxui::Event;
    if (event == Event::Return) return {Key{Key::Kind::Enter}};
    if (event == Event::Escape) return {Key{Key::Kind::Escape}};
    if (event == Event::Tab) return {Key{Key::Kind::Tab}};
    if (event == Event::Backspace) return {Key{Key::Kind::Backspace}};
    if (event == Event::ArrowLeft) return {Key{Key::Kind::Left}};
    if (event == Event::ArrowRight) return {Key{Key::Kind::Right}};
    if (event == Event::ArrowUp) return {Key{Key::Kind::Up}};
    if (event == Event::ArrowDown) return {Key{Key::Kind::Down}};
    std::vector<Key> keys;
    if (event.is_character()) {
        for (const char32_t glyph : poher::ui::decode_utf8(event.character())) {
            if (glyph >= 0x20) keys.push_back(Key{Key::Kind::Letter, glyph});
        }
    }
    return keys;
}

// Puts text on the clipboard through the terminal (OSC 52). Terminals that
// do not know the sequence ignore it.
void copy_to_clipboard(const std::string& text) {
    static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string coded;
    for (std::size_t i = 0; i < text.size(); i += 3) {
        const std::uint32_t chunk = static_cast<std::uint8_t>(text[i]) << 16 |
                                    (i + 1 < text.size() ? static_cast<std::uint8_t>(text[i + 1]) << 8 : 0) |
                                    (i + 2 < text.size() ? static_cast<std::uint8_t>(text[i + 2]) : 0);
        coded += kDigits[chunk >> 18 & 63];
        coded += kDigits[chunk >> 12 & 63];
        coded += i + 1 < text.size() ? kDigits[chunk >> 6 & 63] : '=';
        coded += i + 2 < text.size() ? kDigits[chunk & 63] : '=';
    }
    std::cout << "\x1b]52;c;" << coded << "\x07" << std::flush;
}

int play(Game& game) {
    ftxui::App screen = ftxui::App::FullscreenAlternateScreen();
    // The mouse is not used; leaving it alone keeps the terminal's own text selection working.
    screen.TrackMouse(false);

    ftxui::Component root = ftxui::Renderer([&] { return std::make_shared<Picture>(game); });
    root = ftxui::CatchEvent(root, [&](ftxui::Event event) {
        const std::vector<Key> keys = to_keys(event);
        if (keys.empty()) return false;
        for (const Key& key : keys) game.key(key);
        return true;
    });

    ftxui::Loop loop(&screen, root);
    const auto start = std::chrono::steady_clock::now();
    while (!loop.HasQuitted()) {
        const auto passed = std::chrono::steady_clock::now() - start;
        const bool changed = game.tick(std::chrono::duration_cast<std::chrono::milliseconds>(passed).count());
        if (game.quit()) {
            screen.Exit();
        } else if (changed) {
            // Any event makes the library draw a new frame.
            screen.PostEvent(ftxui::Event::Custom);
        }
        if (game.take_bell()) {
            std::cout << '\a' << std::flush;
        }
        if (const std::string text = game.take_clipboard(); !text.empty()) copy_to_clipboard(text);
        loop.RunOnce();
        // The game waits for people most of the time: a short nap keeps the processor idle.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

const char* const kHelp =
    "POHER - poker in the terminal\n"
    "\n"
    "  poher                 start the game\n"
    "  --lang en|ru          language (taken from the system otherwise)\n"
    "  --avatars FOLDER      where the avatar pictures are (the folder \"avatars\" next to the game)\n"
    "  --data FOLDER         where the profile, the settings and the hand history are kept\n"
    "  --offline             do not go online: no friends, bots only\n"
    "  --where               print the folder the profile is kept in\n"
    "  --code                print the code friends add you by\n"
    "  --version             print the version\n";

}  // namespace

int main(int argc, char** argv) {
    const char* program = argc > 0 ? argv[0] : "";
    std::filesystem::path avatars = poher::app::default_avatar_folder(program);
    std::filesystem::path data = poher::app::default_data_folder(program);
    std::optional<poher::ui::Lang> lang;
    bool where = false;
    bool offline = false;
    bool code = false;

    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        const bool has_value = i + 1 < argc;
        if (option == "--lang" && has_value) {
            const std::string value = argv[++i];
            if (value != "en" && value != "ru") {
                std::fprintf(stderr, "Unknown language: %s (en or ru)\n", value.c_str());
                return 2;
            }
            lang = value == "ru" ? poher::ui::Lang::Ru : poher::ui::Lang::En;
        } else if (option == "--avatars" && has_value) {
            avatars = argv[++i];
        } else if (option == "--data" && has_value) {
            data = argv[++i];
        } else if (option == "--where") {
            where = true;
        } else if (option == "--offline") {
            offline = true;
        } else if (option == "--code") {
            code = true;
        } else if (option == "--version") {
            std::printf("POHER %s\n", kVersion);
            return 0;
        } else if (option == "--help" || option == "-h") {
            std::fputs(kHelp, stdout);
            return 0;
        } else {
            std::fprintf(stderr, "Cannot use the option: %s\n\n", option.c_str());
            std::fputs(kHelp, stderr);
            return 2;
        }
    }

    if (where) {
        std::printf("%s\n", data.string().c_str());
        return 0;
    }

    // The language of the system until the player chooses one; the option wins over both.
    poher::app::Settings settings;
    settings.lang = poher::ui::system_lang();
    poher::app::Storage(data).load(settings);
    if (lang) settings.lang = *lang;

    std::random_device device;
    const std::uint64_t seed = (static_cast<std::uint64_t>(device()) << 32) ^ device() ^
                               static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (code) {
        // The identity is made on the first start and kept from then on.
        const poher::app::Storage storage(data);
        poher::net::ToxConfig config;
        config.saved = storage.load_bytes("network.dat");
        std::string error;
        const auto network = poher::net::make_tox_network(config, poher::net::Events{}, error);
        if (!network) {
            std::fprintf(stderr, "The network did not start: %s\n", error.c_str());
            return 1;
        }
        if (config.saved.empty()) storage.save_bytes("network.dat", network->save());
        std::printf("%s\n", network->code().c_str());
        return 0;
    }

    // The network keeps the player's identity and friends in a file of the data folder.
    poher::online::Online::Factory network;
    if (!offline) {
        network = [data](poher::net::Events events, std::string& error) {
            poher::net::ToxConfig config;
            config.saved = poher::app::Storage(data).load_bytes("network.dat");
            return poher::net::make_tox_network(config, std::move(events), error);
        };
    }
    Game game(settings, avatars, seed, data, network);
    return play(game);
}
