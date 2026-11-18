#include "net/loop_network.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <set>

namespace poher::net {

namespace {

struct Node {
    PeerId id{};
    std::string name;
    Events events;
    bool online = true;
    bool deaf = false;
    std::set<PeerId> friends;
    std::set<PeerId> asked;                    // who sent us a friend request
    std::deque<std::function<void()>> inbox;   // what poll() will report
};

}  // namespace

struct Hub::Shared {
    std::map<PeerId, std::shared_ptr<Node>> nodes;
    std::uint8_t next = 1;

    Node* find(const PeerId& id) {
        const auto found = nodes.find(id);
        return found == nodes.end() ? nullptr : found->second.get();
    }

    // Both sides learn that the link between them went up or down.
    void tell_link(Node& one, Node& other) {
        const Link link = one.online && other.online ? Link::Direct : Link::None;
        const auto tell = [link](Node& to, const Node& about) {
            const PeerId id = about.id;
            const std::string name = about.name;
            to.inbox.push_back([&to, id, name, link] {
                if (link != Link::None && to.events.friend_name) to.events.friend_name(id, name);
                if (to.events.friend_link) to.events.friend_link(id, link);
            });
        };
        tell(one, other);
        tell(other, one);
    }
};

namespace {

class LoopNetwork final : public Network {
public:
    LoopNetwork(std::shared_ptr<Hub::Shared> shared, std::shared_ptr<Node> node)
        : shared_(std::move(shared)), node_(std::move(node)) {}

    ~LoopNetwork() override {
        // Gone for good: friends see the link drop.
        node_->online = false;
        for (const PeerId& id : node_->friends) {
            if (Node* other = shared_->find(id)) shared_->tell_link(*node_, *other);
        }
        node_->events = Events{};
        node_->inbox.clear();
    }

    PeerId id() const override { return node_->id; }
    std::string code() const override { return to_hex(Bytes(node_->id)); }
    void set_name(std::string_view name) override {
        node_->name = std::string(name);
        for (const PeerId& id : node_->friends) {
            Node* other = shared_->find(id);
            if (other == nullptr || !other->online || !node_->online) continue;
            const PeerId me = node_->id;
            const std::string now = node_->name;
            other->inbox.push_back([other, me, now] {
                if (other->events.friend_name) other->events.friend_name(me, now);
            });
        }
    }
    std::string name() const override { return node_->name; }
    Link link() const override { return node_->online ? Link::Direct : Link::None; }

    AddResult add_friend(std::string_view code, std::string_view hello) override {
        if (code.size() != 64) return AddResult::BadCode;
        PeerId id{};
        for (std::size_t i = 0; i < 32; ++i) {
            const auto digit = [&](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int high = digit(code[i * 2]);
            const int low = digit(code[i * 2 + 1]);
            if (high < 0 || low < 0) return AddResult::BadCode;
            id[i] = static_cast<std::uint8_t>(high * 16 + low);
        }
        if (id == node_->id) return AddResult::OwnCode;
        if (node_->friends.contains(id)) return AddResult::AlreadyAdded;
        Node* other = shared_->find(id);
        if (other == nullptr) return AddResult::Failed;
        other->asked.insert(node_->id);
        const PeerId me = node_->id;
        const std::string text(hello);
        other->inbox.push_back([other, me, text] {
            if (other->events.friend_request) other->events.friend_request(me, text);
        });
        return AddResult::Ok;
    }

    bool accept_friend(const PeerId& id) override {
        Node* other = shared_->find(id);
        if (other == nullptr || !node_->asked.contains(id)) return false;
        node_->asked.erase(id);
        node_->friends.insert(id);
        other->friends.insert(node_->id);
        shared_->tell_link(*node_, *other);
        return true;
    }

    std::vector<Friend> friends() const override {
        std::vector<Friend> list;
        for (const PeerId& id : node_->friends) {
            const Node* other = shared_->find(id);
            if (other == nullptr) continue;
            list.push_back(Friend{id, other->name, node_->online && other->online ? Link::Direct : Link::None});
        }
        return list;
    }

    bool send(const PeerId& to, Bytes data) override { return post(to, data, false); }
    bool send_datagram(const PeerId& to, Bytes data) override { return post(to, data, true); }

    std::chrono::milliseconds poll() override {
        // What arrives while we report is reported by the next call.
        std::deque<std::function<void()>> now = std::move(node_->inbox);
        node_->inbox.clear();
        for (const std::function<void()>& report : now) report();
        return std::chrono::milliseconds(10);
    }

    std::vector<std::uint8_t> save() const override { return std::vector<std::uint8_t>(node_->id.begin(), node_->id.end()); }

private:
    bool post(const PeerId& to, Bytes data, bool datagram) {
        Node* other = shared_->find(to);
        if (other == nullptr || data.size() > kMaxMessage || !node_->friends.contains(to)) return false;
        if (!node_->online || !other->online) return false;
        if (other->deaf) return true;
        const PeerId me = node_->id;
        std::vector<std::uint8_t> copy(data.begin(), data.end());
        other->inbox.push_back([other, me, copy = std::move(copy), datagram] {
            const auto& report = datagram ? other->events.datagram : other->events.message;
            if (report) report(me, Bytes(copy));
        });
        return true;
    }

    std::shared_ptr<Hub::Shared> shared_;
    std::shared_ptr<Node> node_;
};

}  // namespace

Hub::Hub() : shared_(std::make_shared<Shared>()) {}
Hub::~Hub() = default;

std::unique_ptr<Network> Hub::join(std::string name, Events events) {
    auto node = std::make_shared<Node>();
    node->id.fill(shared_->next++);
    node->name = std::move(name);
    node->events = std::move(events);
    shared_->nodes[node->id] = node;
    Node* raw = node.get();
    raw->inbox.push_back([raw] {
        if (raw->events.self_link) raw->events.self_link(Link::Direct);
    });
    return std::make_unique<LoopNetwork>(shared_, std::move(node));
}

void Hub::befriend(const PeerId& one, const PeerId& other) {
    Node* a = shared_->find(one);
    Node* b = shared_->find(other);
    if (a == nullptr || b == nullptr || a == b) return;
    a->friends.insert(b->id);
    b->friends.insert(a->id);
    shared_->tell_link(*a, *b);
}

void Hub::set_online(const PeerId& who, bool online) {
    Node* node = shared_->find(who);
    if (node == nullptr || node->online == online) return;
    node->online = online;
    node->inbox.push_back([node, online] {
        if (node->events.self_link) node->events.self_link(online ? Link::Direct : Link::None);
    });
    for (const PeerId& id : node->friends) {
        if (Node* other = shared_->find(id)) shared_->tell_link(*node, *other);
    }
}

void Hub::set_deaf(const PeerId& who, bool deaf) {
    if (Node* node = shared_->find(who)) node->deaf = deaf;
}

}  // namespace poher::net
