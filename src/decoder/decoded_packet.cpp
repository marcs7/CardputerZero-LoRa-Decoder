/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "decoded_packet.h"

#include <cstdio>

namespace decoder {

const char* protocol_name(Protocol protocol) {
    switch (protocol) {
        case Protocol::LoRaWAN:
            return "LoRaWAN";
        case Protocol::Meshtastic:
            return "Meshtastic";
        case Protocol::Raw:
        default:
            return "Raw";
    }
}

std::string to_hex(const uint8_t* data, std::size_t size, char separator) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(size * 3);
    for (std::size_t i = 0; i < size; ++i) {
        if (i && separator) {
            out.push_back(separator);
        }
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0F]);
    }
    return out;
}

std::string to_printable(const uint8_t* data, std::size_t size) {
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const uint8_t c = data[i];
        out.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
    }
    return out;
}

bool mostly_printable(const uint8_t* data, std::size_t size) {
    if (size == 0) {
        return false;
    }
    std::size_t good = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const uint8_t c = data[i];
        if ((c >= 0x20 && c < 0x7F) || c == '\n' || c == '\r' || c == '\t' || c >= 0x80) {
            ++good;
        }
    }
    return good * 10 >= size * 9;
}

std::string format_node_id(uint32_t node) {
    if (node == 0xFFFFFFFFU) {
        return "^all";
    }
    char buf[12];
    std::snprintf(buf, sizeof(buf), "!%08x", node);
    return buf;
}

std::string format_eui(const uint8_t* le_bytes) {
    char buf[24];
    std::snprintf(buf,
                  sizeof(buf),
                  "%02X%02X%02X%02X%02X%02X%02X%02X",
                  le_bytes[7], le_bytes[6], le_bytes[5], le_bytes[4],
                  le_bytes[3], le_bytes[2], le_bytes[1], le_bytes[0]);
    return buf;
}

bool base64_decode(const std::string& in, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t buffer = 0;
    int bits = 0;
    for (const char ch : in) {
        int value;
        if (ch >= 'A' && ch <= 'Z') {
            value = ch - 'A';
        } else if (ch >= 'a' && ch <= 'z') {
            value = ch - 'a' + 26;
        } else if (ch >= '0' && ch <= '9') {
            value = ch - '0' + 52;
        } else if (ch == '+' || ch == '-') {
            value = 62;
        } else if (ch == '/' || ch == '_') {
            value = 63;
        } else if (ch == '=' || ch == ' ' || ch == '\n' || ch == '\r') {
            continue;
        } else {
            return false;
        }
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return true;
}

} // namespace decoder
