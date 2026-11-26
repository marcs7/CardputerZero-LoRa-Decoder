/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "frequency_plan.h"

#include "meshtastic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace decoder {
namespace {

std::vector<Channel> spaced(uint32_t first_hz, uint32_t step_hz, int count, uint32_t bw,
                            uint8_t sf_min, uint8_t sf_max) {
    std::vector<Channel> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out.push_back({first_hz + static_cast<uint32_t>(i) * step_hz, bw, sf_min, sf_max});
    }
    return out;
}

std::vector<Channel> concat(std::vector<Channel> a, const std::vector<Channel>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

} // namespace

const char* scan_profile_name(ScanProfile profile) {
    switch (profile) {
        case ScanProfile::All: return "All";
        case ScanProfile::LoRaWAN: return "LoRaWAN up";
        case ScanProfile::LoRaWANDownlink: return "LoRaWAN down";
        case ScanProfile::Meshtastic: return "Meshtastic";
        case ScanProfile::Private: return "Private 0x12";
        default: return "?";
    }
}

const std::vector<BandPlan>& band_plans() {
    static const std::vector<BandPlan> plans = {
        {"EU868", "EU_868", 863000000, 870000000,
         {{868100000, 125000, 7, 12}, {868300000, 125000, 7, 12}, {868500000, 125000, 7, 12},
          {867100000, 125000, 7, 12}, {867300000, 125000, 7, 12}, {867500000, 125000, 7, 12},
          {867700000, 125000, 7, 12}, {867900000, 125000, 7, 12}},
         {{869525000, 125000, 7, 12}, {868100000, 125000, 7, 12}, {868300000, 125000, 7, 12},
          {868500000, 125000, 7, 12}}},
        {"US915", "US", 902000000, 928000000,
         concat(spaced(902300000, 200000, 64, 125000, 7, 10),
                spaced(903000000, 1600000, 8, 500000, 8, 8)),
         spaced(923300000, 600000, 8, 500000, 7, 12)},
        {"AU915", "ANZ", 915000000, 928000000,
         concat(spaced(915200000, 200000, 64, 125000, 7, 12),
                spaced(915900000, 1600000, 8, 500000, 8, 8)),
         spaced(923300000, 600000, 8, 500000, 7, 12)},
        {"AS923", "JP", 915000000, 928000000,
         {{923200000, 125000, 7, 12}, {923400000, 125000, 7, 12}, {922200000, 125000, 7, 12},
          {922400000, 125000, 7, 12}, {922600000, 125000, 7, 12}, {922800000, 125000, 7, 12},
          {923000000, 125000, 7, 12}, {922000000, 125000, 7, 12}},
         {{923200000, 125000, 7, 12}}},
        {"KR920", "KR", 920000000, 923500000,
         {{922100000, 125000, 7, 12}, {922300000, 125000, 7, 12}, {922500000, 125000, 7, 12}},
         {{921900000, 125000, 7, 12}}},
        {"IN865", "IN", 865000000, 867000000,
         {{865062500, 125000, 7, 12}, {865402500, 125000, 7, 12}, {865985000, 125000, 7, 12}},
         {{866550000, 125000, 7, 12}}},
        {"EU433", "EU_433", 433050000, 434790000,
         {{433175000, 125000, 7, 12}, {433375000, 125000, 7, 12}, {433575000, 125000, 7, 12}},
         {{434665000, 125000, 7, 12}}},
    };
    return plans;
}

const BandPlan* find_band_plan(const std::string& name) {
    for (const auto& plan : band_plans()) {
        if (name == plan.name) {
            return &plan;
        }
    }
    return nullptr;
}

uint16_t sf_mask_range(uint8_t sf_min, uint8_t sf_max) {
    uint16_t mask = 0;
    for (uint8_t sf = sf_min; sf <= sf_max && sf <= 12; ++sf) {
        mask = static_cast<uint16_t>(mask | (1U << sf));
    }
    return mask;
}

std::vector<ScanTarget> build_scan_targets(const BandPlan& plan, ScanProfile profile) {
    std::vector<ScanTarget> targets;

    const auto add_lorawan = [&](const std::vector<Channel>& channels, uint8_t sync, bool iq_inv,
                                 const char* tag) {
        for (const auto& ch : channels) {
            ScanTarget t;
            t.modem.freq_hz = ch.freq_hz;
            t.modem.bandwidth_hz = ch.bandwidth_hz;
            t.modem.spreading_factor = ch.sf_min;
            t.modem.coding_rate = 5;
            t.modem.sync_word = sync;
            t.modem.preamble = 8;
            t.modem.iq_inverted = iq_inv;
            // LoRaWAN downlinks are sent without payload CRC.
            t.modem.crc_on = !iq_inv;
            t.sf_mask = sf_mask_range(ch.sf_min, ch.sf_max);
            t.label = std::string(tag) + " " + format_mhz(ch.freq_hz);
            targets.push_back(std::move(t));
        }
    };

    const auto add_meshtastic = [&]() {
        const auto* region = meshtastic::find_region(plan.meshtastic_region);
        if (!region) {
            return;
        }
        for (const auto& preset : meshtastic::modem_presets()) {
            const uint32_t freq = meshtastic::default_frequency_hz(*region, preset, preset.name);
            if (freq == 0) {
                continue;
            }
            ScanTarget t;
            t.modem.freq_hz = freq;
            t.modem.bandwidth_hz = preset.bandwidth_hz;
            t.modem.spreading_factor = preset.spreading_factor;
            t.modem.coding_rate = preset.coding_rate;
            t.modem.sync_word = meshtastic::kSyncWord;
            t.modem.preamble = meshtastic::kPreambleLength;
            t.modem.iq_inverted = false;
            t.modem.crc_on = true;
            t.sf_mask = static_cast<uint16_t>(1U << preset.spreading_factor);
            t.label = std::string("MT ") + preset.name;
            targets.push_back(std::move(t));
        }
    };

    switch (profile) {
        case ScanProfile::All:
            add_meshtastic();
            add_lorawan(plan.uplinks, kSyncLoRaWANPublic, false, "LW");
            break;
        case ScanProfile::LoRaWAN:
            add_lorawan(plan.uplinks, kSyncLoRaWANPublic, false, "LW");
            break;
        case ScanProfile::LoRaWANDownlink:
            add_lorawan(plan.downlinks, kSyncLoRaWANPublic, true, "LWdn");
            break;
        case ScanProfile::Meshtastic:
            add_meshtastic();
            break;
        case ScanProfile::Private:
            add_lorawan(plan.uplinks, kSyncPrivate, false, "PRV");
            break;
        default:
            break;
    }
    return targets;
}

std::string format_mhz(uint32_t freq_hz) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%03u", freq_hz / 1000000U, (freq_hz / 1000U) % 1000U);
    return buf;
}

std::string format_bw(uint32_t bandwidth_hz) {
    char buf[16];
    if (bandwidth_hz % 1000 == 0) {
        std::snprintf(buf, sizeof(buf), "%uk", bandwidth_hz / 1000U);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1fk", bandwidth_hz / 1000.0);
    }
    return buf;
}

double time_on_air_ms(const LoraModem& modem, uint32_t payload_len) {
    const double sf = modem.spreading_factor;
    const double bw = modem.bandwidth_hz;
    const double t_sym = std::pow(2.0, sf) / bw * 1000.0;
    const bool low_dr = t_sym > 16.0;
    const double de = low_dr ? 1.0 : 0.0;
    const double h = 0.0;  // explicit header
    const double crc = modem.crc_on ? 1.0 : 0.0;
    const double cr = modem.coding_rate - 4;
    const double num = 8.0 * payload_len - 4.0 * sf + 28.0 + 16.0 * crc - 20.0 * h;
    const double payload_symbols =
        8.0 + std::max(std::ceil(num / (4.0 * (sf - 2.0 * de))) * (cr + 4.0), 0.0);
    const double preamble_ms = (modem.preamble + 4.25) * t_sym;
    return preamble_ms + payload_symbols * t_sym;
}

} // namespace decoder
