/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "packet_decoder.h"

#include "frequency_plan.h"
#include "lorawan.h"

#include <cstdio>

namespace decoder {
namespace {

void decode_raw(const uint8_t* data, std::size_t size, DecodedPacket& out) {
    out.protocol = Protocol::Raw;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%zu bytes", size);
    out.add("Length", buf);
    if (mostly_printable(data, size)) {
        std::string text(reinterpret_cast<const char*>(data), size);
        out.add("Text", text);
        out.summary = "\"" + to_printable(data, size) + "\"";
    } else {
        const std::size_t preview = size < 12 ? size : 12;
        out.summary = to_hex(data, preview) + (size > preview ? ".." : "");
    }
}

} // namespace

DecodedPacket decode_packet(const uint8_t* data, std::size_t size, const DecodeContext& context) {
    DecodedPacket out;
    static const std::vector<meshtastic::ChannelKey> kDefaultKeys = meshtastic::default_channel_keys();
    const auto& keys = context.meshtastic_keys ? *context.meshtastic_keys : kDefaultKeys;

    if (size == 0) {
        out.summary = "(empty)";
        return out;
    }

    if (context.sync_word == meshtastic::kSyncWord) {
        if (meshtastic::decode(data, size, keys, out)) {
            return out;
        }
        out = DecodedPacket{};
    } else if (context.sync_word == kSyncLoRaWANPublic) {
        if (lorawan::decode(data, size, out)) {
            return out;
        }
        out = DecodedPacket{};
    } else {
        // Unknown network: accept Meshtastic only if a key actually decrypts it,
        // then fall back to a structural LoRaWAN match.
        bool decrypted = false;
        DecodedPacket attempt;
        if (meshtastic::decode(data, size, keys, attempt, &decrypted) && decrypted) {
            return attempt;
        }
        if (lorawan::decode(data, size, out)) {
            out.add("Note", "LoRaWAN structure on non-public sync word");
            return out;
        }
        out = DecodedPacket{};
    }

    decode_raw(data, size, out);
    return out;
}

} // namespace decoder
