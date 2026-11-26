/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Meshtastic over-the-air packet decoding: 16-byte cleartext header,
 * AES-CTR payload decryption (default "AQ==" channel key or user PSKs),
 * and decoding of the most common Data portnums.
 */

#pragma once

#include "decoded_packet.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace decoder::meshtastic {

constexpr uint8_t kSyncWord = 0x2B;
constexpr uint16_t kPreambleLength = 16;
constexpr std::size_t kHeaderSize = 16;

struct Header {
    uint32_t to{0};
    uint32_t from{0};
    uint32_t id{0};
    uint8_t flags{0};
    uint8_t channel_hash{0};
    uint8_t next_hop{0};
    uint8_t relay_node{0};

    uint8_t hop_limit() const { return flags & 0x07; }
    bool want_ack() const { return (flags & 0x08) != 0; }
    bool via_mqtt() const { return (flags & 0x10) != 0; }
    uint8_t hop_start() const { return (flags >> 5) & 0x07; }
};

bool parse_header(const uint8_t* data, std::size_t size, Header& header);

struct ChannelKey {
    std::string name;          // channel name used for the hash, e.g. "LongFast"
    std::vector<uint8_t> key;  // 0 (no crypto), 16 or 32 bytes
    uint8_t hash() const;
};

// Expands a Meshtastic PSK as stored in channel settings: 1-byte PSKs index
// the well-known default key, 16/32-byte PSKs are used directly.
std::vector<uint8_t> expand_psk(const std::vector<uint8_t>& psk);
const std::vector<uint8_t>& default_key();
uint8_t xor_hash(const uint8_t* data, std::size_t size);

// Default key set: every modem preset name with the default PSK (AQ==).
std::vector<ChannelKey> default_channel_keys();

bool decrypt(const Header& header,
             const std::vector<uint8_t>& key,
             const uint8_t* cipher,
             std::size_t size,
             std::vector<uint8_t>& plain);

// Full decode attempt. Returns false only if the header is unusable.
// When no key matches the packet is still reported (encrypted) with header fields.
bool decode(const uint8_t* data,
            std::size_t size,
            const std::vector<ChannelKey>& keys,
            DecodedPacket& out,
            bool* decrypted = nullptr);

const char* portnum_name(uint32_t portnum);

// ---- Modem presets / frequency slots ----

struct ModemPreset {
    const char* name;  // display name, also the default channel name
    uint32_t bandwidth_hz;
    uint8_t spreading_factor;
    uint8_t coding_rate;  // 5..8 => 4/5..4/8
};

const std::vector<ModemPreset>& modem_presets();

struct Region {
    const char* name;
    double freq_start_mhz;
    double freq_end_mhz;
    double spacing_mhz;
};

const std::vector<Region>& regions();
const Region* find_region(const std::string& name);

uint32_t djb2_hash(const std::string& text);

// Default frequency slot for a channel name in a region (firmware algorithm).
// Returns 0 if the preset bandwidth doesn't fit the region.
uint32_t default_frequency_hz(const Region& region, const ModemPreset& preset,
                              const std::string& channel_name);

} // namespace decoder::meshtastic
