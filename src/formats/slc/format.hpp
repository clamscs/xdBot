#pragma once

#include "formats/slc/slc.hpp"

#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace formats::slc {

struct PlayerAction {
    uint64_t frame = 0;
    uint8_t button = 1;
    bool player2 = false;
    bool down = false;
};

struct Replay {
    double tps = 240.0;
    uint64_t seed = 0;
    std::vector<PlayerAction> actions;
    uint8_t version = 0;
};

inline std::optional<Replay> decode(std::span<const uint8_t> bytes) {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "SILL", 4) == 0) {
        std::string raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream input(raw, std::ios::binary);
        auto parsed = ::slc::v2::Replay<>::read(input);
        if (!parsed) return std::nullopt;

        Replay result;
        result.version = 2;
        result.tps = parsed->m_tps;
        for (const auto& action : parsed->getInputs()) {
            if (action.m_button == ::slc::v2::Input::InputType::Jump ||
                action.m_button == ::slc::v2::Input::InputType::Left ||
                action.m_button == ::slc::v2::Input::InputType::Right) {
                result.actions.push_back({action.m_frame,
                    static_cast<uint8_t>(action.m_button), action.m_player2,
                    action.m_holding});
            }
        }
        return result;
    }

    if (bytes.size() >= 8 && std::memcmp(bytes.data(), "SLC3RPLY", 8) == 0) {
        std::string raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream input(raw, std::ios::binary);
        auto parsed = ::slc::v3::Replay<>::read(input);
        if (!parsed) return std::nullopt;

        Replay result;
        result.version = 3;
        result.tps = parsed->m_meta.m_tps;
        result.seed = parsed->m_meta.m_seed;
        for (const auto& atom : parsed->m_atoms.m_atoms) {
            if (const auto* actions = std::get_if<::slc::v3::ActionAtom>(&atom)) {
                for (const auto& action : actions->m_actions) {
                    if (action.isPlayer()) {
                        result.actions.push_back({action.m_frame,
                            static_cast<uint8_t>(action.m_type), action.m_player2,
                            action.m_holding});
                    }
                }
            }
        }
        return result;
    }

    return std::nullopt;
}

inline std::vector<uint8_t> encode(const Replay& replay) {
    ::slc::v3::Replay<> output;
    output.m_meta.m_tps = replay.tps;
    output.m_meta.m_seed = replay.seed;
    ::slc::v3::ActionAtom atom;
    for (const auto& action : replay.actions) {
        auto previous = atom.m_actions.empty() ? 0 : atom.m_actions.back().m_frame;
        atom.m_actions.emplace_back(previous, action.frame - previous,
            static_cast<::slc::v3::Action::ActionType>(action.button),
            action.down, action.player2);
    }
    output.m_atoms.add(std::move(atom));

    std::ostringstream stream(std::ios::binary);
    auto written = output.write(stream);
    if (!written) return {};
    const auto value = stream.str();
    return std::vector<uint8_t>(value.begin(), value.end());
}

} // namespace formats::slc
