/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LoRaWAN 1.0.x / 1.1 PHYPayload header parser. Payloads stay encrypted
 * (no session keys); we expose the cleartext MAC header fields only.
 */

#pragma once

#include "decoded_packet.h"

#include <cstddef>
#include <cstdint>

namespace decoder::lorawan {

enum class MType : uint8_t {
    JoinRequest = 0,
    JoinAccept = 1,
    UnconfirmedUp = 2,
    UnconfirmedDown = 3,
    ConfirmedUp = 4,
    ConfirmedDown = 5,
    RejoinRequest = 6,
    Proprietary = 7,
};

const char* mtype_name(MType type);
const char* mtype_short(MType type);

struct Frame {
    MType mtype{MType::Proprietary};
    uint8_t major{0};
    bool valid{false};   // structurally plausible
    // Data frames
    uint32_t dev_addr{0};
    uint8_t fctrl{0};
    uint16_t fcnt{0};
    uint8_t fopts_len{0};
    bool has_fport{false};
    uint8_t fport{0};
    std::size_t frm_payload_len{0};
    // Join request
    uint8_t join_eui[8]{};
    uint8_t dev_eui[8]{};
    uint16_t dev_nonce{0};
    uint32_t mic{0};
};

// Structural parse. `valid` is false when lengths/fields don't fit LoRaWAN.
Frame parse(const uint8_t* data, std::size_t size);

// Fills a DecodedPacket from a parsed frame; returns false if not valid.
bool decode(const uint8_t* data, std::size_t size, DecodedPacket& out);

// NetID type/NwkID derived from DevAddr prefix (LoRaWAN Backend Interfaces 1.0).
std::string describe_dev_addr(uint32_t dev_addr);

} // namespace decoder::lorawan
