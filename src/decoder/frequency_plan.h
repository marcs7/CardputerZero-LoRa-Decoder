/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Band plans (LoRaWAN regional channels + Meshtastic default slots) and the
 * list of "scan targets" the radio hops through while hunting for packets.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace decoder {

constexpr uint8_t kSyncLoRaWANPublic = 0x34;
constexpr uint8_t kSyncPrivate = 0x12;

struct LoraModem {
    uint32_t freq_hz{868100000};
    uint32_t bandwidth_hz{125000};
    uint8_t spreading_factor{7};
    uint8_t coding_rate{5};  // 4/5
    uint8_t sync_word{kSyncLoRaWANPublic};
    uint16_t preamble{8};
    bool iq_inverted{false};
    bool crc_on{true};
};

// One hop of the hunt sequence. Several SFs share the same RF settings and
// are probed one after another with Channel Activity Detection.
struct ScanTarget {
    LoraModem modem;
    uint16_t sf_mask{0};  // bit n set => SF n is probed (n in 5..12)
    std::string label;    // e.g. "LW 868.1" or "MT LongFast"
};

enum class ScanProfile : uint8_t {
    All = 0,           // Meshtastic + MeshCore presets + LoRaWAN uplinks
    LoRaWAN,           // LoRaWAN uplink channels, public sync word
    LoRaWANDownlink,   // RX1/RX2 downlinks (inverted IQ)
    Meshtastic,        // Meshtastic preset slots (sync 0x2B)
    Private,           // LoRaWAN channels with private sync word 0x12
    MeshCore,          // MeshCore presets of the band (sync 0x12)
    Count,
};

const char* scan_profile_name(ScanProfile profile);

struct Channel {
    uint32_t freq_hz;
    uint32_t bandwidth_hz;
    uint8_t sf_min;
    uint8_t sf_max;
};

struct BandPlan {
    const char* name;               // "EU868"
    const char* meshtastic_region;  // "EU_868"
    uint32_t sweep_start_hz;        // spectrum sweep bounds
    uint32_t sweep_end_hz;
    std::vector<Channel> uplinks;
    std::vector<Channel> downlinks;
};

const std::vector<BandPlan>& band_plans();
const BandPlan* find_band_plan(const std::string& name);

uint16_t sf_mask_range(uint8_t sf_min, uint8_t sf_max);

std::vector<ScanTarget> build_scan_targets(const BandPlan& plan, ScanProfile profile);

// Human readable "868.100" (MHz, 3 decimals).
std::string format_mhz(uint32_t freq_hz);
std::string format_bw(uint32_t bandwidth_hz);  // "125k", "62.5k"

// Time-on-air in milliseconds for a LoRa packet (Semtech AN1200.13).
double time_on_air_ms(const LoraModem& modem, uint32_t payload_len);

} // namespace decoder
