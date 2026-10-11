#pragma once

/*
 * TCM format support for xdBot.
 *
 * Implements both TCM v1 and v2 format encoding and decoding.
 * Based on the tcm-rs Rust reference implementation.
 *
 * -- File layout --
 *   offset 0x00:   header (16 bytes, fixed magic)
 *   offset 0x10:   metadata (64 bytes, version-dependent)
 *   offset 0x50:   input blob (variable length)
 *
 * -- v1 input encoding --
 *   count varint
 *   per input: frame varint, input byte
 *   EOM marker: 0xCC
 *
 *   input byte: bits 0-2=button, bit 6=player2, bit 7=push
 *   button: 1-3=jump/left/right(vanilla), 3=restart, 4=restartfull, 5=death
 *
 * -- v2 input encoding --
 *   first_frame varint
 *   stream of byte descriptors until EOF
 *
 *   descriptor byte:
 *   bits 0-1: button (vanilla 1-3) or custom_type low bits
 *   bit 2: push / custom_type
 *   bit 3: player2 / custom_type
 *   bit 4: extra/swift flag
 *   bits 5-7: delta blob info (size<<1 | magic)
 */

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tcm {

inline constexpr uint8_t TCBOT_HEADER[16] = {
    0x9f, 0x88, 0x89, 0x84, 0x9f, 0x3b, 0x1d, 0xd8,
    0xcc, 0xa1, 0x86, 0x8a, 0x88, 0x99, 0x84, 0x00
};

inline constexpr size_t META_SIZE = 64;
inline constexpr uint8_t V1_EOM = 0xCC;

enum class InputType : uint8_t {
    Jump = 1,
    Left = 2,
    Right = 3,
    Restart = 4,
    RestartFull = 5,
    Death = 6,
    TPS = 7,
    Bugpoint = 8
};

struct Input {
    uint64_t frame = 0;
    uint8_t button = 1;
    bool player2 = false;
    bool down = false;  // push (true) or release (false)
    InputType type = InputType::Jump;
    float tps = 0.0f;
    uint64_t seed = 0;
    bool swift = false;
};

struct Replay {
    float tps = 240.0f;
    uint64_t seed = 0;
    std::vector<Input> inputs;
};

// --- Metadata ---

struct MetaV1 {
    static constexpr uint8_t VERSION = 1;
    float tps = 240.0f;
    uint8_t append_counter = 0;

    static MetaV1 from_bytes(std::span<const uint8_t> bytes) {
        MetaV1 result;
        result.tps = *reinterpret_cast<const float*>(&bytes[4]);
        result.append_counter = bytes[1];
        return result;
    }

    void to_bytes(uint8_t* out) const {
        memset(out, 0, META_SIZE);
        out[0] = VERSION;
        out[1] = append_counter;
        *reinterpret_cast<float*>(&out[4]) = tps;
    }
};

struct MetaV2 {
    static constexpr uint8_t VERSION = 2;
    float tps = 240.0f;
    uint8_t append_counter = 0;
    uint64_t seed = 0;
    bool use_dt = false;

    static MetaV2 from_bytes(std::span<const uint8_t> bytes) {
        MetaV2 result;
        result.append_counter = bytes[1];
        uint8_t flags = bytes[2];
        float tps_or_dt = *reinterpret_cast<const float*>(&bytes[4]);
        uint64_t stored_seed = *reinterpret_cast<const uint64_t*>(&bytes[8]);
        result.seed = stored_seed;
        result.use_dt = (flags & (1 << 1)) != 0;
        if (result.use_dt) {
            result.tps = 1.0f / tps_or_dt;
        } else {
            result.tps = tps_or_dt;
        }
        return result;
    }

    void to_bytes(uint8_t* out) const {
        memset(out, 0, META_SIZE);
        out[0] = VERSION;
        out[1] = append_counter;
        uint8_t flags = 0;
        if (seed != 0) flags |= (1 << 0);
        if (use_dt) flags |= (1 << 1);
        out[2] = flags;
        float value = use_dt ? (1.0f / tps) : tps;
        *reinterpret_cast<float*>(&out[4]) = value;
        *reinterpret_cast<uint64_t*>(&out[8]) = seed;
    }
};

// --- Writer/Reader ---

class Writer {
public:
    void raw(std::span<const uint8_t> data) {
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    void varint(uint64_t value) {
        do {
            uint8_t byte = static_cast<uint8_t>(value & 0x7F);
            value >>= 7;
            if (value) byte |= 0x80;
            bytes.push_back(byte);
        } while (value);
    }
    void raw_bytes(const void* data, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
    }
    template <typename T>
    void le(T value) {
        uint8_t buf[sizeof(T)];
        std::memcpy(buf, &value, sizeof(T));
        bytes.insert(bytes.end(), buf, buf + sizeof(T));
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
            if (shift == 63 && (byte & 0x7E)) return false;
            out |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80)) return true;
        }
        return false;
    }

    template <typename T>
    bool le(T& out) {
        std::span<const uint8_t> raw;
        if (!take(sizeof(T), raw)) return false;
        std::memcpy(&out, raw.data(), sizeof(T));
        return true;
    }

private:
    std::span<const uint8_t> data;
    size_t pos = 0;
};

// --- V1 ---

namespace v1 {
    inline constexpr uint8_t INPUT_MASK = 0b111;
    inline constexpr uint8_t PUSH_OFFSET = 7;
    inline constexpr uint8_t PUSH_MASK = 1 << PUSH_OFFSET;
    inline constexpr uint8_t PLAYER2_OFFSET = 6;
    inline constexpr uint8_t PLAYER2_MASK = 1 << PLAYER2_OFFSET;

    inline uint8_t serialize_input(const Input& input) {
        if (input.type == InputType::Restart) return 3;
        if (input.type == InputType::RestartFull) return 4;
        if (input.type == InputType::Death) return 5;

        uint8_t button_val = (input.button - 1) & INPUT_MASK;
        return (button_val & INPUT_MASK)
             | ((input.down ? 1u : 0u) << PUSH_OFFSET)
             | ((input.player2 ? 1u : 0u) << PLAYER2_OFFSET);
    }

    inline Input deserialize_input(uint8_t data) {
        Input input;
        uint8_t val = data & INPUT_MASK;

        if (val == 3) {
            input.type = InputType::Restart;
        } else if (val == 4) {
            input.type = InputType::RestartFull;
        } else if (val == 5) {
            input.type = InputType::Death;
        } else {
            input.button = val + 1;
            switch (input.button) {
                case 1: input.type = InputType::Jump; break;
                case 2: input.type = InputType::Left; break;
                case 3: input.type = InputType::Right; break;
                default: input.type = InputType::Jump; break;
            }
        }
        input.down = (data & PUSH_MASK) != 0;
        input.player2 = (data & PLAYER2_MASK) != 0;
        return input;
    }

    inline std::vector<uint8_t> encode(const Replay& replay) {
        Writer w;
        w.raw(std::span<const uint8_t>(TCBOT_HEADER, 16));

        MetaV1 meta;
        meta.tps = replay.tps;
        uint8_t meta_bytes[META_SIZE];
        meta.to_bytes(meta_bytes);
        w.raw(std::span<const uint8_t>(meta_bytes, META_SIZE));

        std::vector<Input> sorted_inputs = replay.inputs;
        std::stable_sort(sorted_inputs.begin(), sorted_inputs.end(),
            [](const Input& a, const Input& b) { return a.frame < b.frame; });

        w.varint(sorted_inputs.size());

        for (const auto& input : sorted_inputs) {
            w.varint(input.frame);
            uint8_t byte = serialize_input(input);
            w.raw_bytes(&byte, 1);
        }

        w.raw_bytes(&V1_EOM, 1);
        return std::move(w.bytes);
    }

    inline std::optional<Replay> decode(std::span<const uint8_t> data) {
        if (data.size() < 16 + META_SIZE + 1 + 5) return std::nullopt;
        if (std::memcmp(data.data(), TCBOT_HEADER, 16) != 0) return std::nullopt;

        auto meta = MetaV1::from_bytes(data.subspan(16, META_SIZE));
        Replay replay;
        replay.tps = meta.tps;

        Reader r(data.subspan(16 + META_SIZE));
        uint64_t count;
        if (!r.varint(count) || count > r.remaining()) return std::nullopt;

        replay.inputs.reserve(count);
        for (uint64_t i = 0; i < count; i++) {
            uint64_t frame;
            std::span<const uint8_t> input_byte;
            if (!r.varint(frame) || !r.take(1, input_byte)) return std::nullopt;
            Input input = deserialize_input(input_byte[0]);
            input.frame = frame;
            replay.inputs.push_back(input);
        }

        std::span<const uint8_t> eom;
        if (!r.take(1, eom) || eom[0] != V1_EOM) return std::nullopt;
        if (r.remaining() != 0) return std::nullopt;
        return replay;
    }
}

// --- V2 ---

namespace v2 {
    // Bit layout constants (matching Rust tcm-rs)
    inline constexpr uint8_t BUTTON_MASK = 0b11;      // bits 0-1
    inline constexpr uint8_t PUSH_OFFSET = 2;
    inline constexpr uint8_t PUSH_MASK = 1 << PUSH_OFFSET;
    inline constexpr uint8_t PLAYER2_OFFSET = 3;
    inline constexpr uint8_t PLAYER2_MASK = 1 << PLAYER2_OFFSET;
    inline constexpr uint8_t CUSTOM_OFFSET = 2;
    inline constexpr uint8_t EXTRA_OFFSET = 4;
    inline constexpr uint8_t DELTA_OFFSET = 5;

    inline constexpr uint8_t CUSTOM_MASK = 0b11 << CUSTOM_OFFSET;
    inline constexpr uint8_t EXTRA_MASK = 1 << EXTRA_OFFSET;
    inline constexpr uint8_t DELTA_DATA_MASK = 0b111 << DELTA_OFFSET;

    // Returns max frame value for blob size code (1=1byte, 2=2bytes, 3=4bytes)
    inline uint64_t blob_max_val(uint64_t b) {
        return (b == 0) ? 0 : ((b == 1) ? UINT8_MAX : (b == 2 ? UINT16_MAX : UINT32_MAX));
    }

    // Serialize an input to v2 byte format
    inline uint8_t serialize_input(const Input& input, bool swift) {
        if (input.type >= InputType::Jump && input.type <= InputType::Right) {
            // Vanilla: button (bits 0-1), push (bit 2), player2 (bit 3), swift (bit 4)
            uint8_t button_val = (static_cast<uint8_t>(input.type) - 1) & BUTTON_MASK;
            return button_val |
                ((input.down ? 1u : 0u) << PUSH_OFFSET) |
                ((input.player2 ? 1u : 0u) << PLAYER2_OFFSET) |
                ((swift ? 1u : 0u) << EXTRA_OFFSET);
        }

        // Non-vanilla
        uint8_t t;
        bool extra = false;

        switch (input.type) {
            case InputType::Restart: t = 0; extra = (input.seed != 0); break;
            case InputType::RestartFull: t = 1; extra = (input.seed != 0); break;
            case InputType::Death: t = 2; extra = (input.seed != 0); break;
            case InputType::TPS: t = 3; break;
            case InputType::Bugpoint: t = 3; extra = true; break;
            default: t = 0; break;
        }

        return (t << CUSTOM_OFFSET) | ((extra ? 1u : 0u) << EXTRA_OFFSET);
    }

    inline std::vector<uint8_t> encode(const Replay& replay) {
        Writer w;
        w.raw(std::span<const uint8_t>(TCBOT_HEADER, 16));

        MetaV2 meta;
        meta.tps = replay.tps;
        meta.seed = replay.seed;
        uint8_t meta_bytes[META_SIZE];
        meta.to_bytes(meta_bytes);
        w.raw(std::span<const uint8_t>(meta_bytes, META_SIZE));

        if (replay.inputs.empty()) return std::move(w.bytes);

        std::vector<Input> sorted_inputs = replay.inputs;
        std::stable_sort(sorted_inputs.begin(), sorted_inputs.end(),
            [](const Input& a, const Input& b) { return a.frame < b.frame; });

        w.varint(sorted_inputs[0].frame);

        uint64_t last_delta = 0;

        size_t i = 0;
        while (i < sorted_inputs.size()) {
            const Input& input = sorted_inputs[i];

            // Check for swift
            bool is_swift = false;
            if (i + 1 < sorted_inputs.size()) {
                const Input& next = sorted_inputs[i + 1];
                if (next.frame == input.frame &&
                    next.type == input.type &&
                    next.button == input.button &&
                    next.player2 == input.player2 &&
                    next.down != input.down) {
                    is_swift = true;
                }
            }

            // Compute delta to next significant input
            uint64_t next_idx = i + (is_swift ? 2 : 1);
            uint64_t next_frame = (next_idx < sorted_inputs.size())
                ? sorted_inputs[next_idx].frame : input.frame;
            uint64_t delta = next_frame - input.frame;

            // Compute delta blob info
            // Following Rust logic: try non-magic first, then magic, for each blob size from smallest
            uint64_t delta_value = delta;
            bool use_magic = false;
            uint8_t blob_size_code = 0;

            if (delta > 0) {
                // Try each blob size starting from smallest (One, Two, Four)
                for (int b = 1; b <= 3; b++) {
                    if (delta <= blob_max_val(b)) {
                        // delta fits without magic
                        blob_size_code = static_cast<uint8_t>(b);
                        delta_value = delta;
                        use_magic = false;
                        goto delta_done;
                    }
                    // Try magic: delta - last_delta
                    if (last_delta != 0 && last_delta <= delta) {
                        uint64_t magic_delta = delta - last_delta;
                        if (magic_delta > 0 && magic_delta <= blob_max_val(b)) {
                            blob_size_code = static_cast<uint8_t>(b);
                            delta_value = magic_delta;
                            use_magic = true;
                            goto delta_done;
                        }
                    }
                }
                // If nothing fit, delta is too big for any blob
                // In practice this shouldn't happen with sane delta values
                blob_size_code = 3;
                delta_value = delta;
                use_magic = false;
            }
            delta_done:;

            uint8_t delta_byte = (blob_size_code << 1) | (use_magic ? 1 : 0);

            // Serialize input byte (lower 5 bits)
            uint8_t input_byte = serialize_input(input, is_swift);

            // Combine
            uint8_t combined = (input_byte & 0x1F) | (delta_byte << DELTA_OFFSET);
            w.raw_bytes(&combined, 1);

            // Extra data for non-vanilla inputs
            if (input.type == InputType::TPS) {
                w.le(input.tps);
            } else if (input.type == InputType::Bugpoint) {
                w.le(input.seed);
            } else if (input.type >= InputType::Restart && input.type <= InputType::Death) {
                // Restart types: write seed if extra flag is set
                uint8_t base = serialize_input(input, false);
                if (base & EXTRA_MASK) {
                    w.le(input.seed);
                }
            }

            // Write delta data if needed
            if (delta > 0) {
                uint8_t bytes_to_write = blob_size_code;  // 0=no bytes, 1=1byte, 2=2bytes, 3=4bytes
                if (bytes_to_write == 3) bytes_to_write = 4;
                w.raw_bytes(&delta_value, bytes_to_write);
                last_delta = delta;
            }

            i += (is_swift ? 2 : 1);
        }

        return std::move(w.bytes);
    }

    inline std::optional<Replay> decode(std::span<const uint8_t> data) {
        if (data.size() < 16 + META_SIZE + 1) return std::nullopt;
        if (std::memcmp(data.data(), TCBOT_HEADER, 16) != 0) return std::nullopt;

        auto meta = MetaV2::from_bytes(data.subspan(16, META_SIZE));
        Replay replay;
        replay.tps = meta.tps;
        replay.seed = meta.seed;

        Reader r(data.subspan(16 + META_SIZE));
        uint64_t current_frame;
        if (!r.varint(current_frame)) return std::nullopt;

        uint64_t last_delta = 0;

        while (r.remaining() > 0) {
            uint8_t byte;
            if (!r.le(byte)) return std::nullopt;

            uint8_t delta_data = (byte & DELTA_DATA_MASK) >> DELTA_OFFSET;
            uint8_t input_data = byte & 0x1F;

            uint8_t blob_code = (delta_data >> 1) & 0b11;
            bool use_magic = (delta_data & 1) != 0;

            // Parse delta
            uint64_t delta = 0;
            if (blob_code == 0) {
                delta = use_magic ? last_delta : 0;
            } else {
                uint64_t value = 0;
                if (blob_code == 1) {
                    uint8_t v;
                    if (!r.le(v)) return std::nullopt;
                    value = v;
                } else if (blob_code == 2) {
                    uint16_t v;
                    if (!r.le(v)) return std::nullopt;
                    value = v;
                } else if (blob_code == 3) {
                    uint32_t v;
                    if (!r.le(v)) return std::nullopt;
                    value = v;
                }
                uint64_t base = use_magic ? last_delta : 0;
                delta = base + value;
                if (delta != 0) last_delta = delta;
            }

            // Parse input
            uint8_t button = input_data & BUTTON_MASK;
            if (button > 0) {
                // Vanilla input
                Input input;
                input.frame = current_frame;
                input.button = button;
                input.down = (input_data & PUSH_MASK) != 0;
                input.player2 = (input_data & PLAYER2_MASK) != 0;
                bool swift = (input_data & EXTRA_MASK) != 0;

                switch (button) {
                    case 1: input.type = InputType::Jump; break;
                    case 2: input.type = InputType::Left; break;
                    case 3: input.type = InputType::Right; break;
                    default: input.type = InputType::Jump; break;
                }

                if (swift) {
                    replay.inputs.push_back(input);
                    Input complement = input;
                    complement.down = !input.down;
                    complement.frame = input.frame;
                    replay.inputs.push_back(complement);
                } else {
                    replay.inputs.push_back(input);
                }
            } else {
                // Non-vanilla
                uint8_t custom_type = (input_data & CUSTOM_MASK) >> CUSTOM_OFFSET;
                bool extra = (input_data & EXTRA_MASK) != 0;

                if (custom_type == 3) {
                    if (extra) {
                        // Bugpoint
                        Input input;
                        input.frame = current_frame;
                        input.type = InputType::Bugpoint;
                        replay.inputs.push_back(input);
                    } else {
                        // TPS
                        float tps_val;
                        if (!r.le(tps_val)) return std::nullopt;
                        Input input;
                        input.frame = current_frame;
                        input.type = InputType::TPS;
                        input.tps = tps_val;
                        replay.inputs.push_back(input);
                    }
                } else {
                    // Restart types
                    InputType restart_type;
                    switch (custom_type) {
                        case 0: restart_type = InputType::Restart; break;
                        case 1: restart_type = InputType::RestartFull; break;
                        case 2: restart_type = InputType::Death; break;
                        default: return std::nullopt;
                    }

                    Input input;
                    input.type = restart_type;

                    if (extra) {
                        uint64_t seed_val;
                        if (!r.le(seed_val)) return std::nullopt;
                        input.seed = seed_val;
                        input.frame = current_frame;
                        replay.inputs.push_back(input);
                        current_frame = 0;
                    } else {
                        input.frame = current_frame;
                        replay.inputs.push_back(input);
                        current_frame = 0;
                    }
                }
            }

            current_frame += delta;
        }

        return replay;
    }
}

// --- Main API ---

inline bool isValidHeader(std::span<const uint8_t> data) {
    return data.size() >= 16 && std::memcmp(data.data(), TCBOT_HEADER, 16) == 0;
}

inline uint8_t getVersion(std::span<const uint8_t> data) {
    if (data.size() < 16 + 1) return 0;
    return data[16];
}

inline std::optional<Replay> decode(std::span<const uint8_t> data) {
    if (!isValidHeader(data)) return std::nullopt;
    uint8_t version = getVersion(data);
    if (version == 1) return v1::decode(data);
    if (version == 2) return v2::decode(data);
    return std::nullopt;
}

inline std::vector<uint8_t> encode(const Replay& replay, uint8_t version = 2) {
    if (version == 1) return v1::encode(replay);
    if (version == 2) return v2::encode(replay);
    return {};
}

} // namespace tcm
