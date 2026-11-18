#include "net/tox_network.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

#include <toxcore/tox.h>

namespace poher::net {
namespace {

using Clock = std::chrono::steady_clock;

// A public node through which a client enters the Tox network.
struct Node {
    const char* host;
    std::uint16_t port;
    const char* key;
    std::uint16_t tcp_ports[3];  // unused slots are 0
};

constexpr Node kNodes[] = {
#include "net/tox_nodes.inc"
};

// Tox reserves the first byte of every custom packet for its kind.
constexpr std::uint8_t kReliablePacket = 160;  // allowed: 160-191
constexpr std::uint8_t kDatagramPacket = 200;  // allowed: 192-254

// How often to knock on the entry nodes again while we are offline.
constexpr auto kRetryEvery = std::chrono::seconds(10);
constexpr std::size_t kRetryNodes = 4;

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Fills `out` from hex text of exactly twice its length.
bool from_hex(std::string_view text, std::span<std::uint8_t> out) {
    if (text.size() != out.size() * 2) return false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int high = hex_value(text[2 * i]);
        const int low = hex_value(text[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

Link to_link(Tox_Connection connection) {
    switch (connection) {
        case TOX_CONNECTION_UDP: return Link::Direct;
        case TOX_CONNECTION_TCP: return Link::Relay;
        default: return Link::None;
    }
}

class ToxNetwork final : public Network {
public:
    ToxNetwork(Tox* tox, Events events) : tox_(tox), events_(std::move(events)) {
        tox_callback_self_connection_status(tox_, on_self_link);
        tox_callback_friend_request(tox_, on_friend_request);
        tox_callback_friend_connection_status(tox_, on_friend_link);
        tox_callback_friend_name(tox_, on_friend_name);
        tox_callback_friend_lossless_packet(tox_, on_reliable);
        tox_callback_friend_lossy_packet(tox_, on_datagram);
        knock(std::size(kNodes));
    }

    ~ToxNetwork() override { tox_kill(tox_); }

    ToxNetwork(const ToxNetwork&) = delete;
    ToxNetwork& operator=(const ToxNetwork&) = delete;

    PeerId id() const override {
        PeerId id;
        tox_self_get_public_key(tox_, id.data());
        return id;
    }

    std::string code() const override {
        std::uint8_t address[TOX_ADDRESS_SIZE];
        tox_self_get_address(tox_, address);
        return to_hex(address);
    }

    void set_name(std::string_view name) override {
        name = name.substr(0, TOX_MAX_NAME_LENGTH);
        tox_self_set_name(tox_, reinterpret_cast<const std::uint8_t*>(name.data()), name.size(), nullptr);
    }

    std::string name() const override {
        std::string name(tox_self_get_name_size(tox_), '\0');
        tox_self_get_name(tox_, reinterpret_cast<std::uint8_t*>(name.data()));
        return name;
    }

    Link link() const override { return to_link(tox_self_get_connection_status(tox_)); }

    AddResult add_friend(std::string_view code, std::string_view hello) override {
        std::string digits;
        for (char c : code) {
            if (!std::isspace(static_cast<unsigned char>(c))) digits += c;
        }
        std::uint8_t address[TOX_ADDRESS_SIZE];
        if (!from_hex(digits, address)) return AddResult::BadCode;

        if (hello.empty()) hello = "POHER";
        hello = hello.substr(0, TOX_MAX_FRIEND_REQUEST_LENGTH);
        Tox_Err_Friend_Add error;
        tox_friend_add(tox_, address, reinterpret_cast<const std::uint8_t*>(hello.data()), hello.size(), &error);
        switch (error) {
            case TOX_ERR_FRIEND_ADD_OK: return AddResult::Ok;
            case TOX_ERR_FRIEND_ADD_OWN_KEY: return AddResult::OwnCode;
            case TOX_ERR_FRIEND_ADD_BAD_CHECKSUM: return AddResult::BadCode;
            case TOX_ERR_FRIEND_ADD_ALREADY_SENT:
            case TOX_ERR_FRIEND_ADD_SET_NEW_NOSPAM: return AddResult::AlreadyAdded;
            default: return AddResult::Failed;
        }
    }

    bool accept_friend(const PeerId& id) override {
        Tox_Err_Friend_Add error;
        tox_friend_add_norequest(tox_, id.data(), &error);
        return error == TOX_ERR_FRIEND_ADD_OK || error == TOX_ERR_FRIEND_ADD_ALREADY_SENT;
    }

    std::vector<Friend> friends() const override {
        std::vector<Tox_Friend_Number> numbers(tox_self_get_friend_list_size(tox_));
        tox_self_get_friend_list(tox_, numbers.data());

        std::vector<Friend> out;
        for (Tox_Friend_Number number : numbers) {
            Friend f;
            if (!friend_id(number, f.id)) continue;
            f.name.resize(tox_friend_get_name_size(tox_, number, nullptr));
            tox_friend_get_name(tox_, number, reinterpret_cast<std::uint8_t*>(f.name.data()), nullptr);
            f.link = to_link(tox_friend_get_connection_status(tox_, number, nullptr));
            out.push_back(std::move(f));
        }
        return out;
    }

    bool send(const PeerId& to, Bytes data) override { return send_packet(to, data, kReliablePacket); }

    bool send_datagram(const PeerId& to, Bytes data) override { return send_packet(to, data, kDatagramPacket); }

    std::chrono::milliseconds poll() override {
        tox_iterate(tox_, this);
        if (link() == Link::None && Clock::now() - last_knock_ > kRetryEvery) knock(kRetryNodes);
        return std::chrono::milliseconds(tox_iteration_interval(tox_));
    }

    std::vector<std::uint8_t> save() const override {
        std::vector<std::uint8_t> data(tox_get_savedata_size(tox_));
        tox_get_savedata(tox_, data.data());
        return data;
    }

private:
    // Asks the next `count` entry nodes to let us into the network. The list
    // holds addresses, not host names, so this never waits for DNS.
    void knock(std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const Node& node = kNodes[next_node_];
            next_node_ = (next_node_ + 1) % std::size(kNodes);

            std::uint8_t key[TOX_PUBLIC_KEY_SIZE];
            if (!from_hex(node.key, key)) continue;
            tox_bootstrap(tox_, node.host, node.port, key, nullptr);
            for (std::uint16_t port : node.tcp_ports) {
                if (port != 0) tox_add_tcp_relay(tox_, node.host, port, key, nullptr);
            }
        }
        last_knock_ = Clock::now();
    }

    bool friend_id(Tox_Friend_Number number, PeerId& id) const {
        return tox_friend_get_public_key(tox_, number, id.data(), nullptr);
    }

    std::optional<Tox_Friend_Number> friend_number(const PeerId& id) const {
        Tox_Err_Friend_By_Public_Key error;
        const Tox_Friend_Number number = tox_friend_by_public_key(tox_, id.data(), &error);
        if (error != TOX_ERR_FRIEND_BY_PUBLIC_KEY_OK) return std::nullopt;
        return number;
    }

    bool send_packet(const PeerId& to, Bytes data, std::uint8_t kind) {
        const auto number = friend_number(to);
        if (!number || data.size() > kMaxMessage) return false;

        std::vector<std::uint8_t> packet;
        packet.reserve(data.size() + 1);
        packet.push_back(kind);
        packet.insert(packet.end(), data.begin(), data.end());
        if (kind == kReliablePacket) {
            return tox_friend_send_lossless_packet(tox_, *number, packet.data(), packet.size(), nullptr);
        }
        return tox_friend_send_lossy_packet(tox_, *number, packet.data(), packet.size(), nullptr);
    }

    // Tox calls these from inside tox_iterate(); `user` is the ToxNetwork.

    static void on_self_link(Tox*, Tox_Connection connection, void* user) {
        auto& self = *static_cast<ToxNetwork*>(user);
        if (self.events_.self_link) self.events_.self_link(to_link(connection));
    }

    static void on_friend_request(Tox*, const std::uint8_t* key, const std::uint8_t* text, std::size_t length,
                                  void* user) {
        auto& self = *static_cast<ToxNetwork*>(user);
        PeerId id;
        std::copy_n(key, id.size(), id.begin());
        const std::string_view hello(reinterpret_cast<const char*>(text), length);
        if (self.events_.friend_request) self.events_.friend_request(id, hello);
    }

    static void on_friend_link(Tox*, Tox_Friend_Number number, Tox_Connection connection, void* user) {
        auto& self = *static_cast<ToxNetwork*>(user);
        PeerId id;
        if (self.friend_id(number, id) && self.events_.friend_link) self.events_.friend_link(id, to_link(connection));
    }

    static void on_friend_name(Tox*, Tox_Friend_Number number, const std::uint8_t* text, std::size_t length,
                               void* user) {
        auto& self = *static_cast<ToxNetwork*>(user);
        PeerId id;
        const std::string_view name(reinterpret_cast<const char*>(text), length);
        if (self.friend_id(number, id) && self.events_.friend_name) self.events_.friend_name(id, name);
    }

    static void on_reliable(Tox*, Tox_Friend_Number number, const std::uint8_t* data, std::size_t length,
                            void* user) {
        auto& self = *static_cast<ToxNetwork*>(user);
        PeerId id;
        if (length < 1 || data[0] != kReliablePacket || !self.friend_id(number, id)) return;
        if (self.events_.message) self.events_.message(id, Bytes(data + 1, length - 1));
    }

    static void on_datagram(Tox*, Tox_Friend_Number number, const std::uint8_t* data, std::size_t length,
                            void* user) {
        auto& self = *static_cast<ToxNetwork*>(user);
        PeerId id;
        if (length < 1 || data[0] != kDatagramPacket || !self.friend_id(number, id)) return;
        if (self.events_.datagram) self.events_.datagram(id, Bytes(data + 1, length - 1));
    }

    Tox* tox_;
    Events events_;
    std::size_t next_node_ = 0;
    Clock::time_point last_knock_;
};

}  // namespace

std::unique_ptr<Network> make_tox_network(const ToxConfig& config, Events events, std::string& error) {
    Tox_Options* options = tox_options_new(nullptr);
    if (options == nullptr) {
        error = "out of memory";
        return nullptr;
    }
    tox_options_set_udp_enabled(options, config.udp);
    tox_options_set_local_discovery_enabled(options, config.lan);
    // A DNS lookup can block for seconds; we only ever pass addresses.
    tox_options_set_experimental_disable_dns(options, true);
    if (!config.saved.empty()) {
        tox_options_set_savedata_type(options, TOX_SAVEDATA_TYPE_TOX_SAVE);
        tox_options_set_savedata_data(options, config.saved.data(), config.saved.size());
    }

    Tox_Err_New status;
    Tox* tox = tox_new(options, &status);
    if (tox == nullptr && status == TOX_ERR_NEW_PORT_ALLOC) {
        // A computer with IPv6 switched off cannot open the default socket: IPv4 alone is enough.
        tox_options_set_ipv6_enabled(options, false);
        tox = tox_new(options, &status);
    }
    tox_options_free(options);
    if (tox == nullptr) {
        switch (status) {
            case TOX_ERR_NEW_PORT_ALLOC:
                error = "could not open a network port; a firewall or an antivirus may be blocking the program";
                break;
            case TOX_ERR_NEW_LOAD_BAD_FORMAT:
            case TOX_ERR_NEW_LOAD_ENCRYPTED:
                error = "the saved profile is damaged";
                break;
            case TOX_ERR_NEW_MALLOC:
                error = "out of memory";
                break;
            default:
                error = tox_err_new_to_string(status);
                break;
        }
        return nullptr;
    }
    return std::make_unique<ToxNetwork>(tox, std::move(events));
}

std::string tox_library_version() {
    return std::to_string(tox_version_major()) + "." + std::to_string(tox_version_minor()) + "." +
           std::to_string(tox_version_patch());
}

}  // namespace poher::net
