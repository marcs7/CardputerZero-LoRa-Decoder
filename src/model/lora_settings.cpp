/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lora_settings.h"

namespace model {

const std::vector<uint32_t>& hunt_dwell_options() {
    static const std::vector<uint32_t> v = {0, 500, 1000, 2000, 5000};
    return v;
}

const std::vector<uint32_t>& spectrum_step_options() {
    static const std::vector<uint32_t> v = {12500, 25000, 50000, 100000, 200000};
    return v;
}

const std::vector<uint32_t>& listen_step_options() {
    static const std::vector<uint32_t> v = {1000, 5000, 12500, 25000, 100000, 200000, 1000000};
    return v;
}

const std::vector<uint32_t>& bandwidth_options() {
    static const std::vector<uint32_t> v = {62500, 125000, 250000, 500000};
    return v;
}

const std::vector<uint8_t>& sync_word_options() {
    static const std::vector<uint8_t> v = {0x34, 0x12, 0x2B};
    return v;
}

const char* profile_key(decoder::ScanProfile profile) {
    switch (profile) {
        case decoder::ScanProfile::All: return "all";
        case decoder::ScanProfile::LoRaWAN: return "lorawan";
        case decoder::ScanProfile::LoRaWANDownlink: return "lorawan_down";
        case decoder::ScanProfile::Meshtastic: return "meshtastic";
        case decoder::ScanProfile::Private: return "private";
        default: return "all";
    }
}

bool parse_profile(const std::string& token, decoder::ScanProfile& profile) {
    for (int i = 0; i < static_cast<int>(decoder::ScanProfile::Count); ++i) {
        const auto p = static_cast<decoder::ScanProfile>(i);
        if (token == profile_key(p)) {
            profile = p;
            return true;
        }
    }
    return false;
}

} // namespace model
