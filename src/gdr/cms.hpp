#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cms {

inline constexpr uint64_t FormatVersion = 1;

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
    std::vector<uint64_t> deaths;
    std::vector<Input> inputs;
};

namespace detail {

class Writer {
public:
    void raw(std::span<const uint8_t> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }

    void varint(uint64_t value) {
        do {
            uint8_t byte = static_cast<uint8_t>(value & 0x7F);
            value >>= 7;
            if (value) byte |= 0x80;
            bytes.push_back(byte);
        } while (value);
    }

    void string(std::string const& value) {
        varint(value.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void boolean(bool value) { bytes.push_back(value ? 1 : 0); }

    template <typename T>
    void be(T value) {
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        if constexpr (std::endian::native == std::endian::little)
            std::reverse(raw, raw + sizeof(T));
        bytes.insert(bytes.end(), raw, raw + sizeof(T));
    }

    std::vector<uint8_t> bytes;
};

class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data(data) {}

    size_t remaining() const { return data.size() - position; }

    bool take(size_t size, std::span<const uint8_t>& out) {
        if (size > remaining()) return false;
        out = data.subspan(position, size);
        position += size;
        return true;
    }

    bool varint(uint64_t& out) {
        out = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            if (position >= data.size()) return false;
            uint8_t byte = data[position++];
            if (shift == 63 && (byte & 0x7E)) return false;
            out |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80)) return true;
        }
        return false;
    }

    bool string(std::string& out) {
        uint64_t length;
        std::span<const uint8_t> bytes;
        if (!varint(length) || length > remaining() || !take(static_cast<size_t>(length), bytes)) return false;
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool boolean(bool& out) {
        std::span<const uint8_t> byte;
        if (!take(1, byte) || byte[0] > 1) return false;
        out = byte[0] != 0;
        return true;
    }

    template <typename T>
    bool be(T& out) {
        std::span<const uint8_t> bytes;
        if (!take(sizeof(T), bytes)) return false;
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, bytes.data(), sizeof(T));
        if constexpr (std::endian::native == std::endian::little)
            std::reverse(raw, raw + sizeof(T));
        std::memcpy(&out, raw, sizeof(T));
        return true;
    }

private:
    std::span<const uint8_t> data;
    size_t position = 0;
};

}

inline std::vector<uint8_t> encode(Replay const& replay) {
    detail::Writer writer;
    std::vector<Input> inputs = replay.inputs;
    std::stable_sort(inputs.begin(), inputs.end(), [](Input const& a, Input const& b) {
        return a.frame < b.frame;
    });

    std::vector<uint64_t> deaths = replay.deaths;
    std::sort(deaths.begin(), deaths.end());

    const uint8_t magic[] = {'C', 'M', 'S'};
    writer.raw(magic);
    writer.varint(FormatVersion);
    writer.boolean(replay.platformer);
    writer.string(replay.author);
    writer.string(replay.description);
    writer.be(replay.duration);
    writer.varint(static_cast<uint64_t>(static_cast<int64_t>(replay.gameVersion)));
    writer.be(replay.framerate);
    writer.varint(replay.seed);
    writer.varint(static_cast<uint64_t>(static_cast<int64_t>(replay.coins)));
    writer.boolean(replay.ldm);
    writer.string(replay.botName);
    writer.varint(static_cast<uint64_t>(static_cast<int64_t>(replay.botVersion)));
    writer.varint(replay.levelId);
    writer.string(replay.levelName);

    writer.varint(deaths.size());
    uint64_t previous = 0;
    for (uint64_t death : deaths) {
        if (death < previous) continue;
        writer.varint(death - previous);
        previous = death;
    }

    writer.varint(inputs.size());
    previous = 0;
    for (Input const& input : inputs) {
        if (input.frame < previous) continue;
        const uint64_t delta = input.frame - previous;
        const uint64_t packed = replay.platformer
            ? (delta << 4) | (uint64_t(input.button & 3) << 2) | (uint64_t(input.player2) << 1) | uint64_t(input.down)
            : (delta << 2) | (uint64_t(input.player2) << 1) | uint64_t(input.down);
        writer.varint(packed);
        previous = input.frame;
    }

    return std::move(writer.bytes);
}

inline std::optional<Replay> decode(std::span<const uint8_t> data) {
    detail::Reader reader(data);
    Replay replay;
    std::span<const uint8_t> magic;
    uint64_t version;
    if (!reader.take(3, magic) || magic[0] != 'C' || magic[1] != 'M' || magic[2] != 'S'
        || !reader.varint(version) || version != FormatVersion
        || !reader.boolean(replay.platformer)
        || !reader.string(replay.author) || !reader.string(replay.description)
        || !reader.be(replay.duration))
        return std::nullopt;

    uint64_t gameVersion, coins, botVersion, levelId;
    if (!reader.varint(gameVersion) || !reader.be(replay.framerate)
        || !reader.varint(replay.seed) || !reader.varint(coins)
        || !reader.boolean(replay.ldm)
        || !reader.string(replay.botName) || !reader.varint(botVersion)
        || !reader.varint(levelId) || !reader.string(replay.levelName))
        return std::nullopt;

    replay.gameVersion = static_cast<int>(static_cast<int64_t>(gameVersion));
    replay.coins = static_cast<int>(static_cast<int64_t>(coins));
    replay.botVersion = static_cast<int>(static_cast<int64_t>(botVersion));
    replay.levelId = static_cast<uint32_t>(levelId);

    uint64_t count;
    if (!reader.varint(count) || count > reader.remaining()) return std::nullopt;
    replay.deaths.reserve(static_cast<size_t>(count));
    uint64_t previous = 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t delta;
        if (!reader.varint(delta) || delta > UINT64_MAX - previous) return std::nullopt;
        previous += delta;
        replay.deaths.push_back(previous);
    }

    if (!reader.varint(count) || count > reader.remaining() * 10) return std::nullopt;
    replay.inputs.reserve(static_cast<size_t>(count));
    previous = 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t packed;
        if (!reader.varint(packed)) return std::nullopt;
        const uint64_t delta = replay.platformer ? packed >> 4 : packed >> 2;
        if (delta > UINT64_MAX - previous) return std::nullopt;
        Input input;
        input.frame = previous + delta;
        input.player2 = replay.platformer ? ((packed >> 1) & 1) : ((packed >> 1) & 1);
        input.down = packed & 1;
        input.button = replay.platformer ? static_cast<uint8_t>((packed >> 2) & 3) : 1;
        replay.inputs.push_back(input);
        previous = input.frame;
    }

    if (reader.remaining() != 0) return std::nullopt;
    return replay;
}

}
