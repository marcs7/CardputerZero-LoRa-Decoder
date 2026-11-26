/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Top-level classifier: given raw bytes plus the radio context they were
 * received with, pick the most plausible protocol and decode it.
 */

#pragma once

#include "decoded_packet.h"
#include "meshtastic.h"

#include <cstdint>
#include <vector>

namespace decoder {

struct DecodeContext {
    uint8_t sync_word{0x34};
    bool iq_inverted{false};
    const std::vector<meshtastic::ChannelKey>* meshtastic_keys{nullptr};
};

DecodedPacket decode_packet(const uint8_t* data, std::size_t size, const DecodeContext& context);

} // namespace decoder
