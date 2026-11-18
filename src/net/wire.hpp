// Packing values into the bytes of a network message and reading them back.
// What comes from the network is never trusted: a Reader that runs out of
// bytes or meets nonsense turns "not ok" and from then on returns zeros, so
// the caller checks ok() once at the end instead of after every value.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace poher::net {

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void flag(bool value) { u8(value ? 1 : 0); }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value >> 8));
        u8(static_cast<std::uint8_t>(value));
    }
    // A whole number of any sign, small ones in fewer bytes.
    void number(std::int64_t value) {
        // The sign goes to the lowest bit, then seven bits per byte.
        std::uint64_t left = (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
        while (left >= 0x80) {
            u8(static_cast<std::uint8_t>(left) | 0x80);
            left >>= 7;
        }
        u8(static_cast<std::uint8_t>(left));
    }
    // Text of up to 255 bytes; anything longer is cut.
    void text(std::string_view value) {
        const std::size_t size = value.size() < 255 ? value.size() : 255;
        u8(static_cast<std::uint8_t>(size));
        bytes_.insert(bytes_.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(size));
    }
    void raw(std::span<const std::uint8_t> value) { bytes_.insert(bytes_.end(), value.begin(), value.end()); }

    std::size_t size() const { return bytes_.size(); }
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint8_t u8() {
        if (at_ >= data_.size()) {
            ok_ = false;
            return 0;
        }
        return data_[at_++];
    }
    bool flag() {
        const std::uint8_t value = u8();
        if (value > 1) ok_ = false;
        return value == 1;
    }
    std::uint16_t u16() {
        const std::uint16_t high = u8();
        return static_cast<std::uint16_t>(high << 8 | u8());
    }
    std::int64_t number() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const std::uint8_t byte = u8();
            value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return static_cast<std::int64_t>(value >> 1) ^ -static_cast<std::int64_t>(value & 1);
            }
        }
        ok_ = false;
        return 0;
    }
    // A number that must lie between `least` and `most`.
    std::int64_t number(std::int64_t least, std::int64_t most) {
        const std::int64_t value = number();
        if (value < least || value > most) {
            ok_ = false;
            return least;
        }
        return value;
    }
    std::string text() {
        const std::size_t size = u8();
        if (data_.size() - at_ < size) {
            ok_ = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(data_.data()) + at_, size);
        at_ += size;
        // No control characters: the text goes straight to the screen.
        for (char& c : value) {
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = ' ';
        }
        return value;
    }
    void raw(std::span<std::uint8_t> into) {
        if (data_.size() - at_ < into.size()) {
            ok_ = false;
            return;
        }
        for (std::uint8_t& byte : into) byte = data_[at_++];
    }

    // Everything was there and made sense so far.
    bool ok() const { return ok_; }
    // The same, and nothing is left over.
    bool done() const { return ok_ && at_ == data_.size(); }
    void fail() { ok_ = false; }
    std::size_t left() const { return data_.size() - at_; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

}  // namespace poher::net
