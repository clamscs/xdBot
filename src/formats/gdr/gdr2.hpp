#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gdr2 {

inline constexpr uint64_t FormatVersion = 2;

struct Input {
    uint64_t frame = 0;
    uint8_t button = 1;
    bool player2 = false;
    bool down = false;
};

struct Replay {
    std::string author;
    std::string description;
    float duration = 0.f;
    int gameVersion = 0;
    double framerate = 240.0;
    uint64_t seed = 0;
    int coins = 0;
    bool ldm = false;
    bool platformer = false;
    std::string botName;
    int botVersion = 1;
    uint32_t levelId = 0;
    std::string levelName;
    std::vector<uint8_t> extension;
    std::vector<uint64_t> deaths;
    std::vector<Input> inputs;
};

namespace detail {

class Writer {
public:
    void raw(std::span<const uint8_t> data) {
        bytes.insert(bytes.end(), data.begin(), data.end());
    }

    void varint(uint64_t value) {
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value) byte |= 0x80;
            bytes.push_back(byte);
        } while (value);
    }

    void string(std::string const& s) {
        varint(s.size());
        bytes.insert(bytes.end(), s.begin(), s.end());
    }

    void boolean(bool b) { bytes.push_back(b ? 1 : 0); }

    template <typename T>
    void be(T value) {
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        if constexpr (std::endian::native == std::endian::little) std::reverse(raw, raw + sizeof(T));
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
    }

    std::vector<uint8_t> bytes;
};

class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data(data) {}

    size_t remaining() const { return data.size() - pos; }

    bool take(size_t n, std::span<const uint8_t>& out) {
        if (n > remaining()) return false;
        out = data.subspan(pos, n);
        pos += n;
        return true;
    }

    bool varint(uint64_t& out) {
        out = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            if (pos >= data.size()) return false;
            uint8_t byte = data[pos++];
            out |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80)) return true;
        }
        return false;
    }

    bool varintSize(size_t& out) {
        uint64_t value;
        if (!varint(value) || value > remaining()) return false;
        out = static_cast<size_t>(value);
        return true;
    }

    bool string(std::string& out) {
        size_t length;
        std::span<const uint8_t> bytes;
        if (!varintSize(length) || !take(length, bytes)) return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool boolean(bool& out) {
        std::span<const uint8_t> bytes;
        if (!take(1, bytes)) return false;
        out = bytes[0] != 0;
        return true;
    }

    template <typename T>
    bool be(T& out) {
        std::span<const uint8_t> bytes;
        if (!take(sizeof(T), bytes)) return false;
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, bytes.data(), sizeof(T));
        if constexpr (std::endian::native == std::endian::little) std::reverse(raw, raw + sizeof(T));
        std::memcpy(&out, raw, sizeof(T));
        return true;
    }

private:
    std::span<const uint8_t> data;
    size_t pos = 0;
};

}

inline std::vector<uint8_t> encode(Replay const& replay) {
    detail::Writer w;
    const bool platformer = replay.platformer;

    std::vector<Input> p1;
    std::vector<Input> p2;
    for (Input const& input : replay.inputs)
        (input.player2 ? p2 : p1).push_back(input);

    auto byFrame = [](Input const& a, Input const& b) { return a.frame < b.frame; };
    std::stable_sort(p1.begin(), p1.end(), byFrame);
    std::stable_sort(p2.begin(), p2.end(), byFrame);

    std::vector<uint64_t> deaths = replay.deaths;
    std::sort(deaths.begin(), deaths.end());

    const uint8_t magic[] = {'G', 'D', 'R'};
    w.raw(magic);
    w.varint(FormatVersion);
    w.string("");

    w.string(replay.author);
    w.string(replay.description);
    w.be(replay.duration);
    w.varint(static_cast<uint64_t>(static_cast<int64_t>(replay.gameVersion)));
    w.be(replay.framerate);
    w.varint(replay.seed);
    w.varint(static_cast<uint64_t>(static_cast<int64_t>(replay.coins)));
    w.boolean(replay.ldm);
    w.boolean(platformer);

    w.string(replay.botName);
    w.varint(static_cast<uint64_t>(static_cast<int64_t>(replay.botVersion)));
    w.varint(replay.levelId);
    w.string(replay.levelName);

    w.varint(replay.extension.size());
    w.raw(replay.extension);

    w.varint(deaths.size());
    uint64_t prev = 0;
    for (uint64_t death : deaths) {
        w.varint(death - prev);
        prev = death;
    }

    w.varint(p1.size() + p2.size());
    w.varint(p1.size());

    for (auto const* group : {&p1, &p2}) {
        prev = 0;
        for (Input const& input : *group) {
            const uint64_t delta = input.frame - prev;
            const uint64_t chunk = platformer
                ? (delta << 3) | (uint64_t(input.button & 3) << 1) | uint64_t(input.down)
                : (delta << 1) | uint64_t(input.down);
            w.varint(chunk);
            prev = input.frame;
        }
    }

    return std::move(w.bytes);
}

inline std::optional<Replay> decode(std::span<const uint8_t> data) {
    detail::Reader r(data);
    Replay replay;

    std::span<const uint8_t> magic;
    if (!r.take(3, magic) || magic[0] != 'G' || magic[1] != 'D' || magic[2] != 'R')
        return std::nullopt;

    uint64_t version;
    if (!r.varint(version) || version != FormatVersion) return std::nullopt;

    std::string inputTag;
    if (!r.string(inputTag)) return std::nullopt;
    const bool hasInputExtension = !inputTag.empty();

    bool platformer = false;
    uint64_t gameVersion, coins, botVersion, levelId;
    if (!r.string(replay.author) || !r.string(replay.description)
        || !r.be(replay.duration) || !r.varint(gameVersion)
        || !r.be(replay.framerate) || !r.varint(replay.seed)
        || !r.varint(coins) || !r.boolean(replay.ldm) || !r.boolean(platformer)
        || !r.string(replay.botName) || !r.varint(botVersion)
        || !r.varint(levelId) || !r.string(replay.levelName))
        return std::nullopt;

    replay.platformer = platformer;
    replay.gameVersion = static_cast<int>(static_cast<int64_t>(gameVersion));
    replay.coins = static_cast<int>(static_cast<int64_t>(coins));
    replay.botVersion = static_cast<int>(static_cast<int64_t>(botVersion));
    replay.levelId = static_cast<uint32_t>(levelId);

    size_t extensionSize;
    std::span<const uint8_t> extension;
    if (!r.varintSize(extensionSize) || !r.take(extensionSize, extension)) return std::nullopt;
    replay.extension.assign(extension.begin(), extension.end());

    size_t deathCount;
    if (!r.varintSize(deathCount)) return std::nullopt;
    replay.deaths.reserve(deathCount);
    uint64_t prev = 0;
    for (size_t i = 0; i < deathCount; i++) {
        uint64_t delta;
        if (!r.varint(delta)) return std::nullopt;
        prev += delta;
        replay.deaths.push_back(prev);
    }

    size_t total;
    size_t p1Count;
    if (!r.varintSize(total) || !r.varintSize(p1Count) || p1Count > total) return std::nullopt;

    replay.inputs.reserve(total);
    for (size_t group = 0; group < 2; group++) {
        const size_t count = group == 0 ? p1Count : total - p1Count;
        prev = 0;

        for (size_t i = 0; i < count; i++) {
            uint64_t chunk;
            if (!r.varint(chunk)) return std::nullopt;

            Input input;
            if (platformer) {
                input.frame = prev + (chunk >> 3);
                input.button = static_cast<uint8_t>((chunk >> 1) & 3);
                input.down = chunk & 1;
            } else {
                input.frame = prev + (chunk >> 1);
                input.button = 1;
                input.down = chunk & 1;
            }
            input.player2 = group == 1;
            prev = input.frame;

            if (hasInputExtension) {
                size_t inputExtensionSize;
                std::span<const uint8_t> skipped;
                if (!r.varintSize(inputExtensionSize) || !r.take(inputExtensionSize, skipped))
                    return std::nullopt;
            }

            replay.inputs.push_back(input);
        }
    }

    if (r.remaining() != 0) return std::nullopt;

    std::stable_sort(replay.inputs.begin(), replay.inputs.end(),
        [](Input const& a, Input const& b) { return a.frame < b.frame; });

    return replay;
}

}
