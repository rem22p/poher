#include "online/online.hpp"

#include <algorithm>
#include <utility>

namespace poher::online {

namespace {

// Each piece starts with two bytes: whether more pieces follow, and the
// number of the message, so that the pieces of a message that never arrived
// whole are not glued to the next one.
constexpr std::size_t kHeader = 2;
constexpr std::size_t kPiece = net::kMaxMessage - kHeader;
constexpr std::size_t kMostMessage = 256 * 1024;

}  // namespace

Online::Online(const Factory& factory) {
    net::Events events;
    events.friend_request = [this](const PeerId& id, std::string_view hello) {
        if (std::none_of(requests_.begin(), requests_.end(), [&](const Request& r) { return r.id == id; })) {
            requests_.push_back(Request{id, std::string(hello.substr(0, 200))});
        }
    };
    events.friend_link = [this](const PeerId& id, net::Link link) {
        const bool was = links_[id] != net::Link::None;
        links_[id] = link;
        const bool now = link != net::Link::None;
        incoming_.erase(id);
        if (now && !was) tell_status(id);
        if (!now) {
            statuses_.erase(id);
            std::erase_if(invitations_, [&](const Invitation& i) { return i.from == id; });
        }
        if (was != now && on_link) on_link(id, now);
    };
    events.friend_name = [this](const PeerId&, std::string_view) { changed_ = true; };
    events.message = [this](const PeerId& from, net::Bytes bytes) { received(from, bytes); };
    if (factory) network_ = factory(std::move(events), error_);
    if (network_) changed_ = true;
}

Online::~Online() = default;

void Online::poll() {
    if (network_) network_->poll();
}

std::string Online::code() const {
    return network_ ? network_->code() : std::string();
}

net::Link Online::link() const {
    return network_ ? network_->link() : net::Link::None;
}

PeerId Online::id() const {
    return network_ ? network_->id() : PeerId{};
}

void Online::set_name(const std::string& name) {
    if (!network_ || network_->name() == name) return;
    network_->set_name(name);
    changed_ = true;
}

net::AddResult Online::add(const std::string& code) {
    if (!network_) return net::AddResult::Failed;
    // Spaces and line breaks from copying do not matter.
    std::string clean;
    for (const char c : code) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') clean += c;
    }
    const net::AddResult result = network_->add_friend(clean, "POHER");
    if (result == net::AddResult::Ok) changed_ = true;
    return result;
}

void Online::accept(const PeerId& id) {
    if (network_ && network_->accept_friend(id)) changed_ = true;
    std::erase_if(requests_, [&](const Request& r) { return r.id == id; });
}

void Online::decline(const PeerId& id) {
    std::erase_if(requests_, [&](const Request& r) { return r.id == id; });
}

std::vector<Mate> Online::mates() const {
    std::vector<Mate> list;
    if (!network_) return list;
    for (const net::Friend& person : network_->friends()) {
        Mate mate{person.id, person.name, person.link, std::nullopt};
        if (const auto found = statuses_.find(person.id); found != statuses_.end()) mate.status = found->second;
        list.push_back(std::move(mate));
    }
    // Those online first, then by name.
    std::stable_sort(list.begin(), list.end(), [](const Mate& a, const Mate& b) {
        const bool a_on = a.link != net::Link::None;
        const bool b_on = b.link != net::Link::None;
        if (a_on != b_on) return a_on;
        return a.name < b.name;
    });
    return list;
}

std::optional<Mate> Online::mate(const PeerId& id) const {
    for (Mate& mate : mates()) {
        if (mate.id == id) return std::move(mate);
    }
    return std::nullopt;
}

void Online::set_status(const Status& status) {
    net::Writer a;
    net::Writer b;
    write(a, status);
    if (status_) write(b, *status_);
    if (status_ && a.bytes() == b.bytes()) return;
    status_ = status;
    for (const auto& [id, link] : links_) {
        if (link != net::Link::None) tell_status(id);
    }
}

void Online::tell_status(const PeerId& to) {
    if (!status_) return;
    net::Writer body;
    write(body, *status_);
    send(to, Kind::Status, body);
}

void Online::invite(const PeerId& to, const TableInfo& table) {
    net::Writer body;
    write(body, table);
    send(to, Kind::Invite, body);
}

std::optional<Invitation> Online::invitation() const {
    if (invitations_.empty()) return std::nullopt;
    return invitations_.back();
}

void Online::drop_invitation() {
    if (!invitations_.empty()) invitations_.pop_back();
}

bool Online::send(const PeerId& to, Kind kind, const net::Writer& body) {
    if (!network_) return false;
    const std::vector<std::uint8_t> whole = message(kind, body);
    const std::uint8_t number = ++numbers_[to];
    for (std::size_t at = 0; at < whole.size() || at == 0; at += kPiece) {
        const std::size_t size = std::min(kPiece, whole.size() - at);
        std::vector<std::uint8_t> piece;
        piece.reserve(size + kHeader);
        piece.push_back(at + size < whole.size() ? 1 : 0);
        piece.push_back(number);
        piece.insert(piece.end(), whole.begin() + static_cast<std::ptrdiff_t>(at),
                     whole.begin() + static_cast<std::ptrdiff_t>(at + size));
        if (!network_->send(to, net::Bytes(piece))) return false;
        if (whole.empty()) break;
    }
    return true;
}

void Online::received(const PeerId& from, net::Bytes bytes) {
    if (bytes.size() < kHeader || bytes[0] > 1) return;
    const bool more = bytes[0] == 1;
    const std::uint8_t number = bytes[1];
    const net::Bytes rest = bytes.subspan(kHeader);
    Incoming& incoming = incoming_[from];
    if (incoming.open && incoming.number != number) incoming.bytes.clear();  // the end of the last one never came
    if (!incoming.open && !more) {
        handle(from, rest);
        return;
    }
    incoming.open = true;
    incoming.number = number;
    if (incoming.bytes.size() + rest.size() > kMostMessage) {
        incoming = Incoming{};
        return;
    }
    incoming.bytes.insert(incoming.bytes.end(), rest.begin(), rest.end());
    if (more) return;
    const std::vector<std::uint8_t> whole = std::move(incoming.bytes);
    incoming = Incoming{};
    handle(from, net::Bytes(whole));
}

void Online::handle(const PeerId& from, net::Bytes message) {
    if (message.empty()) return;
    const auto kind = static_cast<Kind>(message[0]);
    net::Reader body(message.subspan(1));
    switch (kind) {
        case Kind::Status: {
            Status status;
            if (read(body, status)) statuses_[from] = std::move(status);
            break;
        }
        case Kind::Invite: {
            Invitation invitation;
            invitation.from = from;
            if (!read(body, invitation.table) || !body.done()) break;
            for (const net::Friend& person : network_->friends()) {
                if (person.id == from) invitation.name = person.name;
            }
            std::erase_if(invitations_, [&](const Invitation& i) { return i.from == from; });
            invitations_.push_back(std::move(invitation));
            break;
        }
        default:
            if (on_table) on_table(from, kind, body);
            break;
    }
}

std::vector<std::uint8_t> Online::save() const {
    return network_ ? network_->save() : std::vector<std::uint8_t>{};
}

}  // namespace poher::online
