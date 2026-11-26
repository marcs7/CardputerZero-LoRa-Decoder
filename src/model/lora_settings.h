/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * User-tunable scanner settings (persisted in the [lora] config section).
 */

#pragma once

#include "frequency_plan.h"

#include <cstdint>
#include <string>
#include <vector>

namespace model {

struct LoraSettings {
    std::string band{"EU868"};
    decoder::ScanProfile profile{decoder::ScanProfile::All};
    uint32_t hunt_dwell_ms{0};       // 0 = CAD hopping
    uint32_t spectrum_step_hz{50000};
    uint32_t listen_step_hz{25000};
    decoder::LoraModem listen{default_listen_modem()};

    static decoder::LoraModem default_listen_modem() {
        decoder::LoraModem m;  // Meshtastic EU868 LongFast
        m.freq_hz = 869525000;
        m.bandwidth_hz = 250000;
        m.spreading_factor = 11;
        m.coding_rate = 5;
        m.sync_word = 0x2B;
        m.preamble = 16;
        m.iq_inverted = false;
        m.crc_on = true;
        return m;
    }
};

// Option tables used by the settings UI and config parser.
const std::vector<uint32_t>& hunt_dwell_options();
const std::vector<uint32_t>& spectrum_step_options();
const std::vector<uint32_t>& listen_step_options();
const std::vector<uint32_t>& bandwidth_options();
const std::vector<uint8_t>& sync_word_options();

const char* profile_key(decoder::ScanProfile profile);  // config token
bool parse_profile(const std::string& token, decoder::ScanProfile& profile);

} // namespace model
