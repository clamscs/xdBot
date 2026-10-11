#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace slc {

namespace util {

template <typename T, T Left, T Right>
    requires std::totally_ordered<T>
constexpr bool inRange(T value) {
    static_assert(Left <= Right, "Lhs must be lesser or equal to Rhs");
    return value >= Left && value <= Right;
}

template <typename T>
bool binReadInto(std::istream &s, T &out) {
    out = T{};
    return static_cast<bool>(s.read(reinterpret_cast<char *>(&out), sizeof(T)));
}

template <typename T>
T binRead(std::istream &s) {
    T temp{};
    (void)binReadInto(s, temp);
    return temp;
}

inline std::streamoff streamLength(std::istream &s) {
    const auto pos = s.tellg();
    if (pos == -1) {
        return -1;
    }
    if (!s.seekg(0, std::ios::end)) {
        return -1;
    }
    const auto end = s.tellg();
    if (!s.seekg(pos, std::ios::beg)) {
        return -1;
    }
    return end;
}

template <typename T>
void binWrite(std::ostream &s, const T &val) {
    s.write(reinterpret_cast<const char *>(&val), sizeof(T));
}

template <typename T>
    requires std::is_integral_v<T>
int exponentOfTwo(T n) {
    return n ? std::min(
                   static_cast<int>((sizeof(T) * 8 - 1 - std::countl_zero(n))),
                   15)
             : 0;
}

template <typename T>
    requires std::is_integral_v<T>
int largestPowerOfTwo(T n) {
    return n ? 1ULL << exponentOfTwo(n) : 0;
}

} // namespace util

namespace v2 {

enum class InputType : uint8_t {
    Skip = 0,
    Jump = 1,
    Left = 2,
    Right = 3,
    Restart = 4,
    RestartFull = 5,
    Death = 6,
    TPS = 7
};

class Input {
public:
    double m_tps = 0.0;
    uint64_t m_frame = 0;
    bool m_player2 = false;
    InputType m_button = InputType::Skip;
    bool m_holding = false;
    uint64_t m_delta = 0;

private:
    uint64_t m_state;

public:
    Input() : m_state(0) {}
    Input(uint64_t currentFrame, uint64_t delta, InputType type, bool p2, bool hold) {
        m_state = (delta << 5) | (static_cast<uint8_t>(type) << 2) | (p2 << 1) | hold;
        m_delta = delta;
        m_frame = currentFrame + delta;
        m_player2 = p2;
        m_button = type;
        m_holding = hold;
    }

    Input(uint64_t currentFrame, uint64_t delta, double tps) {
        m_state = (delta << 5) | (static_cast<uint8_t>(InputType::TPS) << 2);
        m_delta = delta;
        m_frame = currentFrame + delta;
        m_tps = tps;
        m_button = InputType::TPS;
    }

    uint8_t requiredBytes() const {
        if (m_button == InputType::TPS)
            return 8;

        if (m_state < 0x100) {
            return 1;
        } else if (m_state < 0x10000) {
            return 2;
        } else if (m_state < 0x100000000) {
            return 4;
        } else {
            return 8;
        }
    }

    void updateHelpers(uint64_t currentFrame) {
        m_delta = m_state >> 5;
        m_frame = currentFrame + (m_state >> 5);
        m_player2 = (m_state & 2) >> 1;
        m_button = static_cast<InputType>((m_state & 0b11100) >> 2);
        m_holding = m_state & 1;
    }

    uint64_t state() const { return m_state; }
};

struct Replay {
    double m_tps = 240.0;
    std::vector<Input> m_inputs;
};

inline std::vector<uint8_t> encode(const Replay &replay) {
    std::vector<uint8_t> bytes;

    // Header: "SILL"
    const char header[] = {'S', 'I', 'L', 'L'};
    bytes.insert(bytes.end(), header, header + 4);

    // TPS as double (8 bytes)
    uint64_t tpsBits;
    std::memcpy(&tpsBits, &replay.m_tps, sizeof(tpsBits));
    bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&tpsBits), reinterpret_cast<uint8_t*>(&tpsBits) + 8);

    // Meta size = 0 (void meta)
    uint64_t metaSize = 0;
    bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&metaSize), reinterpret_cast<uint8_t*>(&metaSize) + 8);

    // Input count as uint64_t
    uint64_t count = replay.m_inputs.size();
    bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&count), reinterpret_cast<uint8_t*>(&count) + 8);

    // Build blob groups: merge consecutive inputs with the same required byte size
    // (matching Silicate v2 behavior where each blob has a uniform byte size)
    struct BlobInfo {
        uint8_t byteSize;
        uint64_t start;
        uint64_t length;
    };
    std::vector<BlobInfo> blobs;

    for (size_t i = 0; i < replay.m_inputs.size(); i++) {
        uint8_t reqBytes = replay.m_inputs[i].requiredBytes();
        if (!blobs.empty() && blobs.back().byteSize == reqBytes) {
            blobs.back().length++;
        } else {
            blobs.push_back({reqBytes, i, 1});
        }
    }

    // Blob count
    uint64_t blobCount = blobs.size();
    bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&blobCount), reinterpret_cast<uint8_t*>(&blobCount) + 8);

    // Blob metadata (byteSize, start, length) for each blob
    for (const auto& blob : blobs) {
        uint64_t byteSize = blob.byteSize;
        uint64_t start = blob.start;
        uint64_t length = blob.length;
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&byteSize), reinterpret_cast<uint8_t*>(&byteSize) + 8);
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&start), reinterpret_cast<uint8_t*>(&start) + 8);
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&length), reinterpret_cast<uint8_t*>(&length) + 8);
    }

    // Blob input data
    for (const auto& blob : blobs) {
        for (uint64_t i = 0; i < blob.length; i++) {
            const auto& input = replay.m_inputs[blob.start + i];
            uint64_t state = input.state();

            // Write state bytes (little-endian based on blob byte size)
            for (uint8_t b = 0; b < blob.byteSize; b++) {
                bytes.push_back(static_cast<uint8_t>((state >> (b * 8)) & 0xFF));
            }

            // For TPS inputs, write the additional TPS double
            if (input.m_button == InputType::TPS) {
                uint64_t tpsBits;
                std::memcpy(&tpsBits, &input.m_tps, sizeof(tpsBits));
                bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&tpsBits), reinterpret_cast<uint8_t*>(&tpsBits) + 8);
            }
        }
    }

    // Footer: "EOM"
    const char footer[] = {'E', 'O', 'M'};
    bytes.insert(bytes.end(), footer, footer + 3);

    return bytes;
}

inline std::optional<Replay> decode(std::span<const uint8_t> data) {
    if (data.size() < 4 + 8 + 8 + 8 + 8 + 3) return std::nullopt; // Header + TPS + metaSize + count + blobCount + footer minimum
    if (data[0] != 'S' || data[1] != 'I' || data[2] != 'L' || data[3] != 'L')
        return std::nullopt;

    Replay replay;
    size_t pos = 4;

    // Read TPS
    uint64_t tpsBits;
    std::memcpy(&tpsBits, data.data() + pos, 8);
    std::memcpy(&replay.m_tps, &tpsBits, 8);
    pos += 8;

    // Read meta size (skip, should be 0 for void meta)
    uint64_t metaSize;
    std::memcpy(&metaSize, data.data() + pos, 8);
    pos += 8;
    if (metaSize != 0) return std::nullopt; // We don't support non-void meta

    // Read input count
    uint64_t count;
    std::memcpy(&count, data.data() + pos, 8);
    pos += 8;

    // Read blob count
    uint64_t blobCount;
    std::memcpy(&blobCount, data.data() + pos, 8);
    pos += 8;

    // Read blob metadata
    struct BlobInfo {
        uint8_t byteSize;
        uint64_t start;
        uint64_t length;
    };
    std::vector<BlobInfo> blobs;
    for (uint64_t i = 0; i < blobCount; i++) {
        if (pos + 24 > data.size()) return std::nullopt;
        BlobInfo b;
        std::memcpy(&b.byteSize, data.data() + pos, 8);
        pos += 8;
        std::memcpy(&b.start, data.data() + pos, 8);
        pos += 8;
        std::memcpy(&b.length, data.data() + pos, 8);
        pos += 8;
        blobs.push_back(b);
    }

    // Allocate inputs
    replay.m_inputs.resize(count);

    // Read blob input data
    uint64_t frame = 0;
    for (const auto& blob : blobs) {
        if (blob.length <= 0) continue;

        for (uint64_t i = 0; i < blob.length; i++) {
            if (pos + blob.byteSize > data.size()) return std::nullopt;

            Input& input = replay.m_inputs[blob.start + i];

            // Read state bytes
            uint64_t state = 0;
            for (uint64_t b = 0; b < blob.byteSize; b++) {
                state |= static_cast<uint64_t>(data[pos + b]) << (b * 8);
            }
            pos += blob.byteSize;

            input.updateHelpers(frame);
            frame = input.m_frame;

            // For TPS inputs, read the additional TPS double
            if (input.m_button == InputType::TPS) {
                if (pos + 8 > data.size()) return std::nullopt;
                uint64_t tpsBits;
                std::memcpy(&tpsBits, data.data() + pos, 8);
                std::memcpy(&input.m_tps, &tpsBits, 8);
                pos += 8;
            }
        }
    }

    // Footer
    if (pos + 3 > data.size()) return std::nullopt;
    if (data[pos] != 'E' || data[pos+1] != 'O' || data[pos+2] != 'M')
        return std::nullopt;

    return replay;
}

} // namespace v2

// ===== Silicate v3 Format =====
// v3 uses an atom-based structure with advanced compression
namespace v3 {

enum class ActionType : uint8_t {
    Reserved = 0,
    Jump = 1,
    Left = 2,
    Right = 3,
    Restart = 4,
    RestartFull = 5,
    Death = 6,
    TPS = 7,
    Bugpoint = 8
};

enum class AtomId : uint32_t {
    Null = 0,
    Action = 1,
    Marker = 2
};

class Action {
public:
    uint64_t m_frame = 0;
    ActionType m_type;
    bool m_holding = false;
    bool m_player2 = false;
    uint64_t m_seed = 0;
    double m_tps = 240.0;
    bool m_swift = false;

private:
    uint64_t m_delta = 0;

public:
    Action() = default;

    Action(uint64_t currentFrame, uint64_t delta, ActionType button, bool holding, bool p2) {
        m_frame = currentFrame + delta;
        m_type = button;
        m_holding = holding;
        m_player2 = p2;
        m_delta = delta;
    }

    Action(uint64_t currentFrame, uint64_t delta, ActionType button, uint64_t seed) {
        m_frame = currentFrame + delta;
        m_delta = delta;
        m_type = button;
        m_seed = seed;
    }

    Action(uint64_t currentFrame, uint64_t delta, double tps) {
        m_frame = currentFrame + delta;
        m_delta = delta;
        m_type = ActionType::TPS;
        m_tps = tps;
    }

    Action(uint64_t currentFrame, uint64_t delta, ActionType button) {
        m_frame = currentFrame + delta;
        m_delta = delta;
        m_type = button;
    }

    constexpr bool isPlayer() const { return static_cast<uint8_t>(m_type) <= 3; }
    constexpr uint64_t delta() const { return m_delta; }
    constexpr bool swift() const { return m_swift; }

    void recalculateDelta(uint64_t previousFrame) {
        m_delta = m_frame - previousFrame;
    }

    constexpr uint8_t getMinimumSize() const {
        uint64_t offset = 4ull;
        const uint64_t delta = m_delta;
        if (static_cast<int>(m_type) >= 4) {
            offset = 8;
        }

        const uint64_t ONE_BYTE_THRESHOLD = 1ull << (offset);
        const uint64_t TWO_BYTES_THRESHOLD = 1ull << (offset + 8ull);
        const uint64_t FOUR_BYTES_THRESHOLD = 1ull << (offset + 24ull);

        if (delta < ONE_BYTE_THRESHOLD) {
            return 0;
        } else if (delta < TWO_BYTES_THRESHOLD) {
            return 1;
        } else if (delta < FOUR_BYTES_THRESHOLD) {
            return 2;
        } else {
            return 3;
        }
    }
};

struct Metadata {
    double m_tps = 240.0;
    uint64_t m_seed = 0;
    uint32_t m_version = 2;
    uint32_t m_build = 0;
    uint32_t m_randomnessAlgorithm = 0;
    char __padding[36] = {0};
};

static_assert(sizeof(Metadata) == 64, "Metadata should be 64 bytes");

class PlayerInput {
public:
    enum class Button : uint8_t {
        Swift = 0,
        Jump = 1,
        Left = 2,
        Right = 3
    };

    uint64_t m_frame;
    uint64_t m_delta;
    Button m_button;
    bool m_holding;
    bool m_player2;
    bool m_difference;

    static PlayerInput fromAction(const Action &action) {
        PlayerInput p;
        p.m_button = static_cast<Button>(action.m_type);
        if (action.m_swift) {
            p.m_button = Button::Swift;
        }
        p.m_frame = action.m_frame;
        p.m_delta = action.delta();
        p.m_holding = action.m_holding;
        p.m_player2 = action.m_player2;
        return p;
    }

    static PlayerInput fromState(uint64_t prevFrame, uint64_t state) {
        PlayerInput p;
        p.m_delta = state >> 4;
        p.m_frame = prevFrame + p.m_delta;
        uint8_t button = (state >> 2) & 0b11;
        p.m_button = static_cast<Button>(button);
        p.m_holding = (state & 0b1) == 0b1;
        p.m_player2 = (state & 0b10) == 0b10;
        return p;
    }

    uint64_t prepareState(uint8_t byteSize) const {
        uint64_t byteMask = byteSize == 8 ? ~((uint64_t)0) : (1ull << (byteSize * 8ull)) - 1ull;
        return byteMask & ((m_delta << 4) | (static_cast<uint8_t>(m_button) << 2) |
                           (m_player2 << 1) | m_holding);
    }

    bool weakEq(const PlayerInput &other) {
        return m_delta == other.m_delta && m_holding == other.m_holding &&
               m_player2 == other.m_player2 && m_button == other.m_button;
    }
};

enum class SectionIdentifier : uint8_t {
    Input = 0,
    Repeat = 1,
    Special = 2
};

enum class SpecialType : uint8_t {
    Restart = 0,
    RestartFull = 1,
    Death = 2,
    TPS = 3,
    Bugpoint = 4
};

struct Section {
    SectionIdentifier m_id;
    uint16_t m_deltaSize = 0;
    uint16_t m_countExp = 0;
    uint16_t m_repeatsExp = 0;
    std::vector<PlayerInput> m_playerInputs;
    SpecialType m_specialType;
    uint64_t m_seed = 0;
    double m_tps = 240.0;
    Action m_special;
    bool m_markedForRemoval = false;

    uint64_t getRealDeltaSize() const {
        return 1ull << (uint64_t)m_deltaSize;
    }

    uint64_t getInputCount() const { return 1ull << (uint64_t)m_countExp; }
    uint64_t getRepeatCount() { return 1ull << (uint64_t)m_repeatsExp; }
    bool isSpecial() const { return m_id == SectionIdentifier::Special; }

    void write(std::vector<uint8_t>& bytes) const {
        if (m_markedForRemoval) return;

        switch (m_id) {
        case SectionIdentifier::Input: {
            uint16_t header = (static_cast<uint16_t>(m_countExp) << 8) | (static_cast<uint16_t>(m_deltaSize) << 12);
            bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&header), reinterpret_cast<uint8_t*>(&header) + 2);

            uint64_t byteSize = getRealDeltaSize();
            for (const auto &input : m_playerInputs) {
                uint64_t state = input.prepareState(static_cast<uint8_t>(byteSize));
                for (uint8_t b = 0; b < byteSize; b++) {
                    bytes.push_back(static_cast<uint8_t>((state >> (b * 8)) & 0xFF));
                }
            }
            break;
        }
        case SectionIdentifier::Repeat: {
            uint16_t header = (static_cast<uint16_t>(SectionIdentifier::Repeat) << 14) |
                              (static_cast<uint16_t>(m_deltaSize) << 12) |
                              (static_cast<uint16_t>(m_countExp) << 8) |
                              (static_cast<uint16_t>(m_repeatsExp) << 3);
            bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&header), reinterpret_cast<uint8_t*>(&header) + 2);

            uint64_t byteSize = getRealDeltaSize();
            for (const auto &input : m_playerInputs) {
                uint64_t state = input.prepareState(static_cast<uint8_t>(byteSize));
                for (uint8_t b = 0; b < byteSize; b++) {
                    bytes.push_back(static_cast<uint8_t>((state >> (b * 8)) & 0xFF));
                }
            }
            break;
        }
        case SectionIdentifier::Special: {
            uint16_t header = (static_cast<uint16_t>(SectionIdentifier::Special) << 14) |
                              (static_cast<uint16_t>(m_specialType) << 10) |
                              (static_cast<uint16_t>(m_deltaSize) << 8);
            bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&header), reinterpret_cast<uint8_t*>(&header) + 2);

            uint64_t delta = m_special.delta();
            uint64_t byteSize = getRealDeltaSize();
            for (uint8_t b = 0; b < byteSize; b++) {
                bytes.push_back(static_cast<uint8_t>((delta >> (b * 8)) & 0xFF));
            }

            switch (m_specialType) {
            case SpecialType::Restart:
            case SpecialType::RestartFull:
            case SpecialType::Death:
                bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&m_seed), reinterpret_cast<uint8_t*>(&m_seed) + 8);
                break;
            case SpecialType::TPS:
                bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&m_tps), reinterpret_cast<uint8_t*>(&m_tps) + 8);
                break;
            case SpecialType::Bugpoint:
                break;
            }
            break;
        }
        }
    }
};

inline bool swiftCompatible(std::vector<Action> &actions, size_t i) {
    assert(i < actions.size());
    return actions[i].delta() == 0 && !actions[i].m_holding &&
           actions[i - 1].m_holding != actions[i].m_holding &&
           actions[i - 1].m_player2 == actions[i].m_player2 &&
           actions[i - 1].m_type == actions[i].m_type &&
           actions[i].m_type == Action::ActionType::Jump;
}

inline bool canJoin(std::vector<Action> &actions, size_t count, size_t i) {
    static constexpr size_t MAX_SECTION_ACTIONS = 1 << 16;
    return i < (actions.size() - 1) && count < MAX_SECTION_ACTIONS &&
           actions[i + 1].isPlayer() &&
           actions[i + 1].getMinimumSize() == actions[i].getMinimumSize();
}

inline bool prepareSections(std::vector<Action> &actions, std::vector<Section> &sections) {
    size_t i = 0;
    while (i < actions.size()) {
        if (!actions[i].isPlayer()) {
            Section s;
            s.m_id = SectionIdentifier::Special;
            s.m_special = actions[i];
            s.m_deltaSize = actions[i].getMinimumSize();

            using A = Action::ActionType;
            switch (actions[i].m_type) {
            case A::TPS:
                s.m_tps = actions[i].m_tps;
                s.m_specialType = SpecialType::TPS;
                break;
            case A::Death:
            case A::Restart:
            case A::RestartFull:
                s.m_seed = actions[i].m_seed;
                s.m_specialType = static_cast<SpecialType>(static_cast<int>(actions[i].m_type) - 4);
                break;
            case A::Bugpoint:
                s.m_specialType = SpecialType::Bugpoint;
                break;
            default:
                return false;
            }
            sections.push_back(std::move(s));
            i++;
            continue;
        }

        uint32_t count = 1;
        uint32_t pureCount = 1;
        uint32_t swifts = 0;
        uint32_t pureSwifts = 0;
        size_t start = i;
        uint8_t minSize = actions[i].getMinimumSize();

        while (canJoin(actions, pureCount, i)) {
            i++;
            count++;
            if (swiftCompatible(actions, i)) {
                actions[i - 1].m_swift = true;
                actions[i].m_swift = true;
                swifts++;
            } else {
                pureCount++;
            }
            if ((uint32_t)util::largestPowerOfTwo(pureCount) == pureCount) {
                pureSwifts = swifts;
            }
        }

        count--;
        count = util::largestPowerOfTwo(pureCount);
        i = start + count + pureSwifts;

        Section s;
        s.m_id = SectionIdentifier::Input;
        for (size_t j = start; j < i; j++) {
            if (actions[j].m_holding || !actions[j].swift()) {
                s.m_playerInputs.push_back(PlayerInput::fromAction(actions[j]));
            }
        }
        s.m_countExp = util::exponentOfTwo(s.m_playerInputs.size());
        s.m_deltaSize = minSize;

        // For simplicity, skip run-length encoding (Repeat sections)
        // Just distribute as Input sections
        // This is a simplified version that still produces valid v3 files
        size_t idx = 0;
        while (idx < s.m_playerInputs.size()) {
            uint64_t subCount = util::largestPowerOfTwo(s.m_playerInputs.size() - idx);
            Section subSection;
            subSection.m_id = SectionIdentifier::Input;
            subSection.m_deltaSize = minSize;
            subSection.m_countExp = util::exponentOfTwo(subCount);
            subSection.m_playerInputs = std::vector<PlayerInput>(s.m_playerInputs.begin() + idx, s.m_playerInputs.begin() + idx + subCount);
            sections.push_back(std::move(subSection));
            idx += subCount;
        }
    }
    return true;
}

struct ActionAtom {
    std::vector<Action> m_actions;

    bool write(std::vector<uint8_t>& bytes) {
        uint64_t count = m_actions.size();
        bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&count), reinterpret_cast<uint8_t*>(&count) + 8);

        std::vector<Section> sections;
        if (!prepareSections(m_actions, sections)) return false;
        for (auto &section : sections) {
            section.write(bytes);
        }
        return true;
    }
};

struct Replay {
    Metadata m_meta;
    std::vector<Action> m_actions;
};

inline std::vector<uint8_t> encode(const Replay &replay) {
    std::vector<uint8_t> bytes;

    // Header: "SLC3RPLY"
    const char header[] = {'S', 'L', 'C', '3', 'R', 'P', 'L', 'Y'};
    bytes.insert(bytes.end(), header, header + 8);

    // Meta size as uint16_t
    uint16_t metaSize = sizeof(Metadata);
    bytes.insert(bytes.end(), reinterpret_cast<uint8_t*>(&metaSize), reinterpret_cast<uint8_t*>(&metaSize) + 2);

    // Metadata
    bytes.insert(bytes.end(), reinterpret_cast<const uint8_t*>(&replay.m_meta), reinterpret_cast<const uint8_t*>(&replay.m_meta) + sizeof(Metadata));

    // Action atom
    ActionAtom atom;
    atom.m_actions = replay.m_actions;
    atom.write(bytes);

    // Footer: 0xCC
    bytes.push_back(0xCC);

    return bytes;
}

inline std::optional<Replay> decode(std::span<const uint8_t> data) {
    if (data.size() < 8 + 2 + sizeof(Metadata) + 8 + 1) return std::nullopt;

    // Check header
    const char v3Header[] = {'S', 'L', 'C', '3', 'R', 'P', 'L', 'Y'};
    if (std::memcmp(data.data(), v3Header, 8) != 0) return std::nullopt;

    Replay replay;
    size_t pos = 8;

    // Read meta size
    uint16_t metaSize;
    std::memcpy(&metaSize, data.data() + pos, 2);
    pos += 2;
    if (metaSize != sizeof(Metadata)) return std::nullopt;

    // Read metadata
    if (pos + sizeof(Metadata) > data.size()) return std::nullopt;
    std::memcpy(&replay.m_meta, data.data() + pos, sizeof(Metadata));
    pos += sizeof(Metadata);

    // Read action atom count
    if (pos + 8 > data.size()) return std::nullopt;
    uint64_t actionCount;
    std::memcpy(&actionCount, data.data() + pos, 8);
    pos += 8;

    replay.m_actions.reserve(actionCount);

    // Read sections
    while (pos < data.size() && data[pos] != 0xCC) {
        if (pos + 2 > data.size()) return std::nullopt;
        uint16_t header;
        std::memcpy(&header, data.data() + pos, 2);
        pos += 2;

        uint16_t id = header >> 14;
        if (id == 0) { // Input section
            uint16_t deltaSize = (header >> 12) & 0b11;
            uint16_t countExp = (header >> 8) & 0b1111;

            uint64_t byteSize = 1ull << deltaSize;
            uint64_t count = 1ull << countExp;

            uint64_t prevFrame = replay.m_actions.empty() ? 0 : replay.m_actions.back().m_frame;

            for (uint64_t i = 0; i < count; i++) {
                if (pos + byteSize > data.size()) return std::nullopt;
                uint64_t state = 0;
                for (uint64_t b = 0; b < byteSize; b++) {
                    state |= static_cast<uint64_t>(data[pos + b]) << (b * 8);
                }
                pos += byteSize;

                uint64_t delta = state >> 4;
                uint8_t button = (state >> 2) & 0b11;
                bool holding = state & 0b1;
                bool player2 = (state >> 1) & 0b1;

                uint64_t currentFrame = prevFrame + delta;
                prevFrame = currentFrame;

                if (button == 0) { // Swift
                    replay.m_actions.emplace_back(currentFrame - delta, delta, Action::ActionType::Jump, true, player2);
                    replay.m_actions.back().m_swift = true;
                    replay.m_actions.emplace_back(currentFrame, 0, Action::ActionType::Jump, false, player2);
                    replay.m_actions.back().m_swift = true;
                } else {
                    replay.m_actions.emplace_back(prevFrame, delta, static_cast<Action::ActionType>(button), holding, player2);
                }
            }
        } else if (id == 1) { // Repeat section
            uint16_t deltaSize = (header >> 12) & 0b11;
            uint16_t countExp = (header >> 8) & 0b1111;
            uint16_t repeatsExp = (header >> 3) & 0b11111;

            uint64_t byteSize = 1ull << deltaSize;
            uint64_t count = 1ull << countExp;
            uint64_t repeats = 1ull << repeatsExp;

            // Read the base cluster
            uint64_t prevFrame = replay.m_actions.empty() ? 0 : replay.m_actions.back().m_frame;
            std::vector<PlayerInput> cluster;
            for (uint64_t i = 0; i < count; i++) {
                if (pos + byteSize > data.size()) return std::nullopt;
                uint64_t state = 0;
                for (uint64_t b = 0; b < byteSize; b++) {
                    state |= static_cast<uint64_t>(data[pos + b]) << (b * 8);
                }
                pos += byteSize;
                cluster.push_back(PlayerInput::fromState(prevFrame, state));
                prevFrame = cluster.back().m_frame;
            }

            // Expand repeats
            for (uint64_t r = 0; r < repeats; r++) {
                for (size_t j = 0; j < cluster.size(); j++) {
                    auto &p = cluster[j];
                    prevFrame = replay.m_actions.empty() ? 0 : replay.m_actions.back().m_frame;
                    if (p.m_button == PlayerInput::Button::Swift) {
                        replay.m_actions.emplace_back(prevFrame, p.m_delta, Action::ActionType::Jump, true, p.m_player2);
                        replay.m_actions.back().m_swift = true;
                        replay.m_actions.emplace_back(prevFrame + p.m_delta, 0, Action::ActionType::Jump, false, p.m_player2);
                        replay.m_actions.back().m_swift = true;
                    } else {
                        replay.m_actions.emplace_back(prevFrame, p.m_delta, static_cast<Action::ActionType>(p.m_button), p.m_holding, p.m_player2);
                    }
                }
            }
        } else if (id == 2) { // Special section
            uint16_t deltaSize = (header >> 8) & 0b11;
            uint16_t specialType = (header >> 10) & 0b1111;

            uint64_t byteSize = 1ull << deltaSize;
            if (pos + byteSize > data.size()) return std::nullopt;

            uint64_t delta = 0;
            for (uint64_t b = 0; b < byteSize; b++) {
                delta |= static_cast<uint64_t>(data[pos + b]) << (b * 8);
            }
            pos += byteSize;

            uint64_t prevFrame = replay.m_actions.empty() ? 0 : replay.m_actions.back().m_frame;

            switch (static_cast<SpecialType>(specialType)) {
            case SpecialType::TPS: {
                if (pos + 8 > data.size()) return std::nullopt;
                double tps;
                std::memcpy(&tps, data.data() + pos, 8);
                pos += 8;
                replay.m_actions.emplace_back(prevFrame, delta, tps);
                break;
            }
            case SpecialType::Restart:
            case SpecialType::RestartFull:
            case SpecialType::Death: {
                if (pos + 8 > data.size()) return std::nullopt;
                uint64_t seed;
                std::memcpy(&seed, data.data() + pos, 8);
                pos += 8;
                replay.m_actions.emplace_back(prevFrame, delta, static_cast<Action::ActionType>(static_cast<int>(specialType) + 4), seed);
                break;
            }
            case SpecialType::Bugpoint:
                replay.m_actions.emplace_back(prevFrame, delta, Action::ActionType::Bugpoint);
                break;
            }
        }
    }

    // Check footer
    if (pos >= data.size() || data[pos] != 0xCC) return std::nullopt;

    return replay;
}

} // namespace v3

// Common Replay type that auto-detects format
struct Replay {
    double m_tps = 240.0;
    std::vector<Input> m_inputs; // v2 style
    std::vector<v3::Action> m_actions; // v3 style
    bool isV3 = false;
};

inline std::optional<Replay> decodeAuto(std::span<const uint8_t> data) {
    // Try v3 first (header "SLC3RPLY")
    if (data.size() >= 8) {
        const char v3Header[] = {'S', 'L', 'C', '3', 'R', 'P', 'L', 'Y'};
        if (std::memcmp(data.data(), v3Header, 8) == 0) {
            auto v3Replay = v3::decode(data);
            if (v3Replay) {
                Replay result;
                result.isV3 = true;
                result.m_tps = v3Replay->m_meta.m_tps;
                result.m_actions = std::move(v3Replay->m_actions);
                return result;
            }
            return std::nullopt;
        }
    }

    // Try v2 (header "SILL")
    if (data.size() >= 4) {
        if (data[0] == 'S' && data[1] == 'I' && data[2] == 'L' && data[3] == 'L') {
            auto v2Replay = v2::decode(data);
            if (v2Replay) {
                Replay result;
                result.m_tps = v2Replay->m_tps;
                for (const auto& input : v2Replay->m_inputs) {
                    result.m_inputs.push_back(input);
                }
                return result;
            }
            return std::nullopt;
        }
    }

    return std::nullopt;
}

} // namespace slc
