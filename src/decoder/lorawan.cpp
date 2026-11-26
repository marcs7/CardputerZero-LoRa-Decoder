/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lorawan.h"

#include <cstdio>

namespace decoder::lorawan {
namespace {

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

bool is_uplink(MType type) {
    return type == MType::UnconfirmedUp || type == MType::ConfirmedUp;
}

bool is_data(MType type) {
    return type == MType::UnconfirmedUp || type == MType::ConfirmedUp ||
           type == MType::UnconfirmedDown || type == MType::ConfirmedDown;
}

std::string hex8(uint32_t value) {
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%08X", value);
    return buf;
}

} // namespace

const char* mtype_name(MType type) {
    switch (type) {
        case MType::JoinRequest: return "Join Request";
        case MType::JoinAccept: return "Join Accept";
        case MType::UnconfirmedUp: return "Unconfirmed Data Up";
        case MType::UnconfirmedDown: return "Unconfirmed Data Down";
        case MType::ConfirmedUp: return "Confirmed Data Up";
        case MType::ConfirmedDown: return "Confirmed Data Down";
        case MType::RejoinRequest: return "Rejoin Request";
        case MType::Proprietary: default: return "Proprietary";
    }
}

const char* mtype_short(MType type) {
    switch (type) {
        case MType::JoinRequest: return "JoinReq";
        case MType::JoinAccept: return "JoinAcc";
        case MType::UnconfirmedUp: return "UnconfUp";
        case MType::UnconfirmedDown: return "UnconfDn";
        case MType::ConfirmedUp: return "ConfUp";
        case MType::ConfirmedDown: return "ConfDn";
        case MType::RejoinRequest: return "Rejoin";
        case MType::Proprietary: default: return "Prop";
    }
}

Frame parse(const uint8_t* data, std::size_t size) {
    Frame f;
    if (!data || size < 1) {
        return f;
    }
    const uint8_t mhdr = data[0];
    f.mtype = static_cast<MType>(mhdr >> 5);
    f.major = mhdr & 0x03;
    const bool rfu_clear = (mhdr & 0x1C) == 0;
    if (f.major != 0 || !rfu_clear) {
        return f;
    }
    if (size >= 5) {
        f.mic = le32(data + size - 4);
    }

    switch (f.mtype) {
        case MType::JoinRequest:
            if (size != 23) {
                return f;
            }
            for (int i = 0; i < 8; ++i) {
                f.join_eui[i] = data[1 + i];
                f.dev_eui[i] = data[9 + i];
            }
            f.dev_nonce = le16(data + 17);
            f.valid = true;
            return f;
        case MType::JoinAccept:
            // 1 + 12 (+16 CFList) + 4 MIC, encrypted
            f.valid = (size == 17 || size == 33);
            return f;
        case MType::RejoinRequest:
            f.valid = (size == 19 || size == 24);
            return f;
        case MType::Proprietary:
            return f;
        default:
            break;
    }

    // Data frame: MHDR(1) DevAddr(4) FCtrl(1) FCnt(2) FOpts(0..15) [FPort(1) FRMPayload] MIC(4)
    if (size < 12) {
        return f;
    }
    f.dev_addr = le32(data + 1);
    f.fctrl = data[5];
    f.fcnt = le16(data + 6);
    f.fopts_len = f.fctrl & 0x0F;
    const std::size_t header_len = 8 + f.fopts_len;
    if (header_len + 4 > size) {
        return f;
    }
    const std::size_t remaining = size - header_len - 4;
    if (remaining > 0) {
        f.has_fport = true;
        f.fport = data[header_len];
        f.frm_payload_len = remaining - 1;
        // FPort 0 carries MAC commands, so FOpts must be empty in that case.
        if (f.fport == 0 && f.fopts_len != 0) {
            return f;
        }
        // FPort 224 is test mode; 225..255 are RFU.
        if (f.fport > 224) {
            return f;
        }
    }
    f.valid = true;
    return f;
}

std::string describe_dev_addr(uint32_t dev_addr) {
    // Count leading 1 bits of the prefix to get the NetID type (0..7).
    int type = 0;
    while (type < 8 && (dev_addr & (0x80000000U >> type))) {
        ++type;
    }
    if (type >= 8) {
        return "invalid prefix";
    }
    static constexpr int kNwkIdBits[8] = {6, 6, 9, 11, 12, 13, 15, 17};
    const int prefix_bits = type + 1;
    const int nwkid_bits = kNwkIdBits[type];
    const uint32_t nwkid =
        (dev_addr >> (32 - prefix_bits - nwkid_bits)) & ((1U << nwkid_bits) - 1U);
    char buf[48];
    if (type == 0 && (nwkid == 0 || nwkid == 1)) {
        std::snprintf(buf, sizeof(buf), "type 0, NwkID %02X (experimental)", nwkid);
    } else if (type == 0 && nwkid == 0x13) {
        std::snprintf(buf, sizeof(buf), "type 0, NwkID 13 (TTN)");
    } else {
        std::snprintf(buf, sizeof(buf), "type %d, NwkID %X", type, nwkid);
    }
    return buf;
}

bool decode(const uint8_t* data, std::size_t size, DecodedPacket& out) {
    const Frame f = parse(data, size);
    if (!f.valid) {
        return false;
    }

    out.protocol = Protocol::LoRaWAN;
    out.add("Type", mtype_name(f.mtype));
    char buf[96];

    if (f.mtype == MType::JoinRequest) {
        out.add("JoinEUI", format_eui(f.join_eui));
        out.add("DevEUI", format_eui(f.dev_eui));
        std::snprintf(buf, sizeof(buf), "%u", f.dev_nonce);
        out.add("DevNonce", buf);
        out.add("MIC", hex8(f.mic));
        out.summary = "JoinReq DevEUI " + format_eui(f.dev_eui);
        return true;
    }
    if (f.mtype == MType::JoinAccept || f.mtype == MType::RejoinRequest) {
        out.add("Note", "payload encrypted (needs AppKey)");
        out.add("MIC", hex8(f.mic));
        std::snprintf(buf, sizeof(buf), "%s %zuB", mtype_short(f.mtype), size);
        out.summary = buf;
        return true;
    }

    out.add("DevAddr", hex8(f.dev_addr));
    out.add("NetID", describe_dev_addr(f.dev_addr));
    std::string flags;
    if (f.fctrl & 0x80) flags += "ADR ";
    if (is_uplink(f.mtype)) {
        if (f.fctrl & 0x40) flags += "ADRACKReq ";
        if (f.fctrl & 0x10) flags += "ClassB ";
    } else {
        if (f.fctrl & 0x10) flags += "FPending ";
    }
    if (f.fctrl & 0x20) flags += "ACK ";
    if (flags.empty()) flags = "-";
    out.add("FCtrl", flags);
    std::snprintf(buf, sizeof(buf), "%u", f.fcnt);
    out.add("FCnt", buf);
    if (f.fopts_len) {
        out.add("FOpts", to_hex(data + 8, f.fopts_len));
    }
    if (f.has_fport) {
        std::snprintf(buf, sizeof(buf), "%u%s", f.fport,
                      f.fport == 0 ? " (MAC cmds)" : (f.fport == 224 ? " (test)" : ""));
        out.add("FPort", buf);
        std::snprintf(buf, sizeof(buf), "%zu bytes (encrypted)", f.frm_payload_len);
        out.add("FRMPayload", buf);
    }
    out.add("MIC", hex8(f.mic));

    std::snprintf(buf, sizeof(buf), "%s %08X fc%u%s", mtype_short(f.mtype), f.dev_addr, f.fcnt,
                  f.has_fport ? "" : " (no port)");
    out.summary = buf;
    if (f.has_fport) {
        std::snprintf(buf, sizeof(buf), " p%u", f.fport);
        out.summary += buf;
    }
    (void)is_data;
    return true;
}

} // namespace decoder::lorawan
