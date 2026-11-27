/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace decoder {

enum class Protocol : uint8_t {
    Raw = 0,
    LoRaWAN,
    Meshtastic,
    MeshCore,
};

const char* protocol_name(Protocol protocol);

struct DecodedField {
    std::string key;
    std::string value;
};

struct DecodedPacket {
    Protocol protocol{Protocol::Raw};
    // One short line for list views (fits ~40 chars).
    std::string summary;
    // Ordered key/value pairs for the detail view.
    std::vector<DecodedField> fields;

    void add(std::string key, std::string value) {
        fields.push_back({std::move(key), std::move(value)});
    }
};

std::string to_hex(const uint8_t* data, std::size_t size, char separator = ' ');
// Printable ASCII with '.' substituted for non-printables.
std::string to_printable(const uint8_t* data, std::size_t size);
// True if most bytes are printable text (UTF-8 multibyte tolerated).
bool mostly_printable(const uint8_t* data, std::size_t size);
std::string format_node_id(uint32_t node);
std::string format_eui(const uint8_t* le_bytes);  // 8 bytes, little-endian on air

bool base64_decode(const std::string& in, std::vector<uint8_t>& out);

} // namespace decoder
