/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MeshCore over-the-air packet decoder (payload version 1, firmware v1.12+).
 * Reference: meshcore-dev/MeshCore docs/packet_format.md and docs/payloads.md.
 *
 *   [header][transport_codes?][path_len][path][payload]
 *   header = 0bVVPPPPRR (version, payload type, route type)
 *
 * Group text on known channels (AES-128-ECB + 2-byte HMAC-SHA256) is decrypted;
 * adverts are parsed (signature not verified); direct messages stay encrypted.
 */

#pragma once

#include "decoded_packet.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace decoder::meshcore {

constexpr uint8_t kSyncWord = 0x12;  // RadioLib SX126x "private" sync word

enum class RouteType : uint8_t { TransportFlood = 0, Flood = 1, Direct = 2, TransportDirect = 3 };

enum class PayloadType : uint8_t {
    Req = 0x00,
    Response = 0x01,
    TxtMsg = 0x02,
    Ack = 0x03,
    Advert = 0x04,
    GrpTxt = 0x05,
    GrpData = 0x06,
    AnonReq = 0x07,
    Path = 0x08,
    Trace = 0x09,
    Multipart = 0x0A,
    Control = 0x0B,
    RawCustom = 0x0F,
};

const char* payload_type_name(PayloadType type);
const char* route_type_name(RouteType type);

struct Packet {
    RouteType route{RouteType::Flood};
    PayloadType type{PayloadType::RawCustom};
    uint8_t version{0};
    bool has_transport{false};
    uint16_t transport_codes[2]{};
    uint8_t path_hash_size{1};
    uint8_t path_hops{0};
    const uint8_t* path{nullptr};
    std::size_t path_bytes{0};
    const uint8_t* payload{nullptr};
    std::size_t payload_size{0};
};

// Structural parse; false if the bytes can't be a v1 MeshCore packet.
bool parse(const uint8_t* data, std::size_t size, Packet& out);

struct Channel {
    std::string name;
    std::array<uint8_t, 16> secret{};
    uint8_t hash() const;  // first byte of SHA-256(secret)
};

// The built-in "Public" channel (well-known key 8b3387e9c5cdea6ac9e5edbaa115cd72).
const Channel& public_channel();
std::vector<Channel> default_channels();

// Parses a 16-byte channel secret given as base64 or 32 hex digits.
bool parse_secret(const std::string& text, std::array<uint8_t, 16>& out);

struct Preset {
    const char* name;
    uint32_t freq_hz;
    uint32_t bandwidth_hz;
    uint8_t spreading_factor;
    uint8_t coding_rate;  // 5..8
};

// MeshCore presets usable on a band plan (EU868: "EU Narrow" used in Sardinia).
std::vector<Preset> presets_for_plan(const std::string& plan_name);
uint16_t preamble_for_sf(uint8_t sf);  // firmware: 32 for SF<=8, else 16

enum class Confidence { None, Structural, Strong };

// Decodes a MeshCore packet. Strong = decrypted group text or a well-formed advert.
Confidence decode(const uint8_t* data, std::size_t size, const std::vector<Channel>& channels, DecodedPacket& out);

// ---- encoders (simulator and tests) ----
std::vector<uint8_t> encode_group_text(const Channel& channel, uint32_t timestamp, const std::string& sender,
                                       const std::string& text, uint8_t path_hops = 0);
std::vector<uint8_t> encode_advert(const uint8_t pub_key[32], uint32_t timestamp, uint8_t node_type,
                                   bool has_location, double lat, double lon, const std::string& name,
                                   uint8_t path_hops = 0);

} // namespace decoder::meshcore
