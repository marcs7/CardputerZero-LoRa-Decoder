/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "meshcore.h"

#include "aes.h"
#include "sha256.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace decoder::meshcore {
namespace {

constexpr std::size_t kMaxPathBytes = 64;
constexpr std::size_t kMaxPayload = 184;
constexpr std::size_t kPubKeySize = 32;
constexpr std::size_t kSignatureSize = 64;
constexpr std::size_t kMacSize = 2;
constexpr uint8_t kAdvLatLon = 0x10;
constexpr uint8_t kAdvFeat1 = 0x20;
constexpr uint8_t kAdvFeat2 = 0x40;
constexpr uint8_t kAdvName = 0x80;

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

std::string hex2(uint8_t v) {
    char b[4];
    std::snprintf(b, sizeof(b), "%02x", v);
    return b;
}

std::string fmt_u(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof(b), "%llu", static_cast<unsigned long long>(v));
    return b;
}

std::string format_time(uint32_t ts) {
    if (ts < 1500000000U) {
        return fmt_u(ts);  // node without RTC
    }
    const std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char b[32];
    std::strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S UTC", &tm);
    return b;
}

std::string path_text(const Packet& p) {
    if (p.path_hops == 0) {
        return "direct (0 hops)";
    }
    std::string s;
    for (uint8_t h = 0; h < p.path_hops; ++h) {
        if (h) s += ">";
        for (uint8_t b = 0; b < p.path_hash_size; ++b) {
            s += hex2(p.path[h * p.path_hash_size + b]);
        }
    }
    return s + " (" + std::to_string(p.path_hops) + (p.path_hops == 1 ? " hop)" : " hops)");
}

const char* node_type_name(uint8_t t) {
    switch (t) {
        case 1: return "companion";
        case 2: return "repeater";
        case 3: return "room server";
        case 4: return "sensor";
        default: return "unknown";
    }
}

bool payload_size_plausible(PayloadType type, std::size_t n) {
    switch (type) {
        case PayloadType::Advert: return n >= kPubKeySize + 4 + kSignatureSize;
        case PayloadType::GrpTxt:
        case PayloadType::GrpData: return n >= 1 + kMacSize + 16 && (n - 1 - kMacSize) % 16 == 0;
        case PayloadType::TxtMsg:
        case PayloadType::Req:
        case PayloadType::Response:
        case PayloadType::Path: return n >= 2 + kMacSize + 16 && (n - 2 - kMacSize) % 16 == 0;
        case PayloadType::AnonReq: return n >= 1 + kPubKeySize + kMacSize + 16 && (n - 1 - kPubKeySize - kMacSize) % 16 == 0;
        case PayloadType::Ack: return n == 4;
        case PayloadType::Trace: return n >= 9;
        case PayloadType::Multipart:
        case PayloadType::Control:
        case PayloadType::RawCustom: return n >= 1;
    }
    return false;
}

std::string printable_name(const uint8_t* p, std::size_t n) {
    std::string s(reinterpret_cast<const char*>(p), n);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

bool name_is_sane(const std::string& s) {
    if (s.empty() || s.size() > 32) return false;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

// MAC-then-decrypt as in MeshCore Utils::MACThenDecrypt (HMAC key = secret zero-padded to 32 bytes).
bool mac_then_decrypt(const std::array<uint8_t, 16>& secret, const uint8_t* src, std::size_t n,
                      std::vector<uint8_t>& plain) {
    if (n <= kMacSize || (n - kMacSize) % 16 != 0) {
        return false;
    }
    uint8_t key32[32] = {};
    std::memcpy(key32, secret.data(), 16);
    uint8_t mac[32];
    hmac_sha256(key32, sizeof(key32), src + kMacSize, n - kMacSize, mac);
    if (std::memcmp(mac, src, kMacSize) != 0) {
        return false;
    }
    plain.resize(n - kMacSize);
    return aes_ecb_decrypt(secret.data(), 16, src + kMacSize, n - kMacSize, plain.data());
}

std::vector<uint8_t> encrypt_then_mac(const std::array<uint8_t, 16>& secret, const std::vector<uint8_t>& plain) {
    std::vector<uint8_t> padded = plain;
    padded.resize((plain.size() + 15) / 16 * 16, 0);
    std::vector<uint8_t> out(kMacSize + padded.size());
    aes_ecb_encrypt(secret.data(), 16, padded.data(), padded.size(), out.data() + kMacSize);
    uint8_t key32[32] = {};
    std::memcpy(key32, secret.data(), 16);
    uint8_t mac[32];
    hmac_sha256(key32, sizeof(key32), out.data() + kMacSize, padded.size(), mac);
    std::memcpy(out.data(), mac, kMacSize);
    return out;
}

std::vector<uint8_t> frame(PayloadType type, uint8_t path_hops, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> f;
    f.push_back(static_cast<uint8_t>((static_cast<uint8_t>(type) << 2) | static_cast<uint8_t>(RouteType::Flood)));
    f.push_back(static_cast<uint8_t>(path_hops & 0x3F));  // 1-byte hashes
    for (uint8_t i = 0; i < path_hops; ++i) {
        f.push_back(static_cast<uint8_t>(0xA0 + i));
    }
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

} // namespace

const char* payload_type_name(PayloadType type) {
    switch (type) {
        case PayloadType::Req: return "Request";
        case PayloadType::Response: return "Response";
        case PayloadType::TxtMsg: return "Text message (direct)";
        case PayloadType::Ack: return "Ack";
        case PayloadType::Advert: return "Advert";
        case PayloadType::GrpTxt: return "Group text";
        case PayloadType::GrpData: return "Group data";
        case PayloadType::AnonReq: return "Anonymous request";
        case PayloadType::Path: return "Returned path";
        case PayloadType::Trace: return "Trace";
        case PayloadType::Multipart: return "Multipart";
        case PayloadType::Control: return "Control";
        case PayloadType::RawCustom: return "Raw custom";
    }
    return "?";
}

const char* route_type_name(RouteType type) {
    switch (type) {
        case RouteType::TransportFlood: return "flood + transport";
        case RouteType::Flood: return "flood";
        case RouteType::Direct: return "direct";
        case RouteType::TransportDirect: return "direct + transport";
    }
    return "?";
}

bool parse(const uint8_t* data, std::size_t size, Packet& out) {
    if (!data || size < 3) {
        return false;
    }
    const uint8_t header = data[0];
    out = Packet{};
    out.route = static_cast<RouteType>(header & 0x03);
    const uint8_t type = (header >> 2) & 0x0F;
    out.version = (header >> 6) & 0x03;
    if (out.version != 0 || (type >= 0x0C && type <= 0x0E)) {
        return false;
    }
    out.type = static_cast<PayloadType>(type);
    std::size_t i = 1;
    out.has_transport = out.route == RouteType::TransportFlood || out.route == RouteType::TransportDirect;
    if (out.has_transport) {
        if (size < i + 4) return false;
        out.transport_codes[0] = static_cast<uint16_t>(data[i] | (data[i + 1] << 8));
        out.transport_codes[1] = static_cast<uint16_t>(data[i + 2] | (data[i + 3] << 8));
        i += 4;
    }
    if (size <= i) return false;
    const uint8_t path_len = data[i++];
    const uint8_t size_code = path_len >> 6;
    if (size_code == 3) return false;  // reserved
    out.path_hash_size = static_cast<uint8_t>(size_code + 1);
    out.path_hops = path_len & 0x3F;
    out.path_bytes = static_cast<std::size_t>(out.path_hops) * out.path_hash_size;
    if (out.path_bytes > kMaxPathBytes || size < i + out.path_bytes) return false;
    out.path = data + i;
    i += out.path_bytes;
    out.payload = data + i;
    out.payload_size = size - i;
    if (out.payload_size == 0 || out.payload_size > kMaxPayload) return false;
    return payload_size_plausible(out.type, out.payload_size);
}

uint8_t Channel::hash() const {
    uint8_t digest[32];
    sha256(secret.data(), secret.size(), digest);
    return digest[0];
}

const Channel& public_channel() {
    static const Channel ch{"Public", {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                       0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72}};
    return ch;
}

std::vector<Channel> default_channels() {
    return {public_channel()};
}

bool parse_secret(const std::string& text, std::array<uint8_t, 16>& out) {
    std::string t;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) t.push_back(c);
    }
    if (t.size() == 32 && t.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
        for (std::size_t i = 0; i < 16; ++i) {
            out[i] = static_cast<uint8_t>(std::stoi(t.substr(2 * i, 2), nullptr, 16));
        }
        return true;
    }
    std::vector<uint8_t> raw;
    if (!base64_decode(t, raw) || raw.size() != 16) {
        return false;
    }
    std::memcpy(out.data(), raw.data(), 16);
    return true;
}

std::vector<Preset> presets_for_plan(const std::string& plan_name) {
    if (plan_name == "EU868") {
        return {
            {"EU Narrow", 869618000, 62500, 8, 8},    // reference preset of LoRaSardegna / EU
            {"EU Legacy", 869525000, 250000, 11, 5},  // old default, still used by some nodes
        };
    }
    if (plan_name == "AU915") {
        return {{"AU Narrow", 916575000, 62500, 7, 7}};
    }
    return {};
}

uint16_t preamble_for_sf(uint8_t sf) {
    return sf <= 8 ? 32 : 16;
}

Confidence decode(const uint8_t* data, std::size_t size, const std::vector<Channel>& channels, DecodedPacket& out) {
    Packet p;
    if (!parse(data, size, p)) {
        return Confidence::None;
    }
    out.protocol = Protocol::MeshCore;
    out.add("Type", payload_type_name(p.type));
    out.add("Route", route_type_name(p.route));
    out.add("Path", path_text(p));
    if (p.has_transport) {
        char b[32];
        std::snprintf(b, sizeof(b), "%04X %04X", p.transport_codes[0], p.transport_codes[1]);
        out.add("Transport codes", b);
    }
    const std::string hops = p.path_hops ? " h" + std::to_string(p.path_hops) : "";
    const uint8_t* pl = p.payload;
    const std::size_t n = p.payload_size;
    char buf[96];

    switch (p.type) {
        case PayloadType::Advert: {
            out.add("Public key", to_hex(pl, 8, 0) + "...");
            out.add("Timestamp", format_time(le32(pl + 32)));
            const uint8_t* app = pl + kPubKeySize + 4 + kSignatureSize;
            const std::size_t app_n = n - (kPubKeySize + 4 + kSignatureSize);
            out.add("Signature", "not verified");
            if (app_n == 0) {
                out.summary = "ADV (no appdata)" + hops;
                return Confidence::Structural;
            }
            const uint8_t flags = app[0];
            const uint8_t node_type = flags & 0x0F;
            std::size_t i = 1;
            double lat = 0, lon = 0;
            bool has_loc = false;
            if ((flags & kAdvLatLon) && app_n >= i + 8) {
                lat = static_cast<int32_t>(le32(app + i)) / 1e6;
                lon = static_cast<int32_t>(le32(app + i + 4)) / 1e6;
                has_loc = true;
                i += 8;
            }
            if (flags & kAdvFeat1) i += 2;
            if (flags & kAdvFeat2) i += 2;
            std::string name;
            if ((flags & kAdvName) && app_n > i) {
                name = printable_name(app + i, app_n - i);
            }
            out.add("Node type", node_type_name(node_type));
            const bool loc_ok = has_loc && std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0;
            if (loc_ok) {
                std::snprintf(buf, sizeof(buf), "%.5f,%.5f", lat, lon);
                out.add("Position", buf);
            }
            if (!name.empty()) out.add("Name", name);
            out.summary = std::string("ADV ") + node_type_name(node_type) + (name.empty() ? "" : " " + name) + hops;
            const bool sane = node_type >= 1 && node_type <= 4 && (name.empty() || name_is_sane(name)) &&
                              (!has_loc || (std::fabs(lat) <= 90.0 && std::fabs(lon) <= 180.0));
            return sane ? Confidence::Strong : Confidence::Structural;
        }
        case PayloadType::GrpTxt:
        case PayloadType::GrpData: {
            const uint8_t ch_hash = pl[0];
            std::snprintf(buf, sizeof(buf), "0x%02X", ch_hash);
            out.add("Channel hash", buf);
            for (const auto& ch : channels) {
                if (ch.hash() != ch_hash) continue;
                std::vector<uint8_t> plain;
                if (!mac_then_decrypt(ch.secret, pl + 1, n - 1, plain) || plain.size() < 5) continue;
                out.add("Channel", ch.name);
                out.add("Sent", format_time(le32(plain.data())));
                if (p.type == PayloadType::GrpTxt) {
                    const uint8_t txt_type = plain[4] >> 2;
                    std::string text = printable_name(plain.data() + 5, plain.size() - 5);
                    out.add("Text type", txt_type == 0 ? "plain" : fmt_u(txt_type));
                    const auto colon = text.find(": ");
                    if (colon != std::string::npos) {
                        out.add("Sender", text.substr(0, colon) + " (unverified)");
                        out.add("Message", text.substr(colon + 2));
                    } else {
                        out.add("Message", text);
                    }
                    out.summary = "#" + ch.name + " " + text + hops;
                } else {
                    const uint16_t data_type = static_cast<uint16_t>(plain[0] | (plain[1] << 8));
                    out.add("Data type", fmt_u(data_type));
                    out.add("Data", to_hex(plain.data() + 3, std::min<std::size_t>(plain[2], plain.size() - 3)));
                    out.summary = "#" + ch.name + " data " + fmt_u(data_type) + hops;
                }
                return Confidence::Strong;
            }
            out.add("Payload", std::to_string(n - 3) + " bytes encrypted (unknown channel key)");
            std::snprintf(buf, sizeof(buf), "GRP ch %02X enc%s", ch_hash, hops.c_str());
            out.summary = buf;
            return Confidence::Structural;
        }
        case PayloadType::TxtMsg:
        case PayloadType::Req:
        case PayloadType::Response:
        case PayloadType::Path: {
            out.add("Destination", hex2(pl[0]));
            out.add("Source", hex2(pl[1]));
            out.add("Payload", std::to_string(n - 4) + " bytes encrypted (private)");
            const char* tag = p.type == PayloadType::TxtMsg ? "DM"
                              : p.type == PayloadType::Req  ? "REQ"
                              : p.type == PayloadType::Path ? "PATH"
                                                            : "RESP";
            std::snprintf(buf, sizeof(buf), "%s %02x>%02x%s", tag, pl[1], pl[0], hops.c_str());
            out.summary = buf;
            return Confidence::Structural;
        }
        case PayloadType::AnonReq:
            out.add("Destination", hex2(pl[0]));
            out.add("Sender key", to_hex(pl + 1, 8, 0) + "...");
            out.summary = "ANON " + to_hex(pl + 1, 2, 0) + ">" + hex2(pl[0]) + hops;
            return Confidence::Structural;
        case PayloadType::Ack:
            std::snprintf(buf, sizeof(buf), "%08X", le32(pl));
            out.add("Ack CRC", buf);
            out.summary = std::string("ACK ") + buf + hops;
            return Confidence::Structural;
        case PayloadType::Trace: {
            std::snprintf(buf, sizeof(buf), "%08X", le32(pl));
            out.add("Trace tag", buf);
            const uint8_t flags = pl[8];
            const std::size_t hash_size = std::size_t(1) << (flags & 0x03);
            const std::size_t hop_count = (n - 9) / hash_size;
            out.add("Route hops", std::to_string(hop_count));
            std::string snrs;
            for (uint8_t h = 0; h < p.path_hops && h < p.path_bytes; ++h) {
                std::snprintf(buf, sizeof(buf), "%s%.1f", h ? " " : "", static_cast<int8_t>(p.path[h]) / 4.0);
                snrs += buf;
            }
            if (!snrs.empty()) out.add("SNR per hop (dB)", snrs);
            out.summary = "TRACE " + std::to_string(hop_count) + " hops";
            return Confidence::Structural;
        }
        case PayloadType::Multipart:
            out.add("Remaining", fmt_u(pl[0] >> 4));
            out.add("Inner type", fmt_u(pl[0] & 0x0F));
            out.summary = "MULTI" + hops;
            return Confidence::Structural;
        case PayloadType::Control:
        case PayloadType::RawCustom:
            out.add("Data", to_hex(pl, n));
            out.summary = std::string(p.type == PayloadType::Control ? "CTRL " : "CUSTOM ") + std::to_string(n) + "B" + hops;
            return Confidence::Structural;
    }
    return Confidence::Structural;
}

std::vector<uint8_t> encode_group_text(const Channel& channel, uint32_t timestamp, const std::string& sender,
                                       const std::string& text, uint8_t path_hops) {
    std::vector<uint8_t> plain;
    for (int i = 0; i < 4; ++i) plain.push_back(static_cast<uint8_t>(timestamp >> (8 * i)));
    plain.push_back(0x00);  // plain text, attempt 0
    const std::string body = sender + ": " + text;
    plain.insert(plain.end(), body.begin(), body.end());
    std::vector<uint8_t> payload = {channel.hash()};
    const auto enc = encrypt_then_mac(channel.secret, plain);
    payload.insert(payload.end(), enc.begin(), enc.end());
    return frame(PayloadType::GrpTxt, path_hops, payload);
}

std::vector<uint8_t> encode_advert(const uint8_t pub_key[32], uint32_t timestamp, uint8_t node_type,
                                   bool has_location, double lat, double lon, const std::string& name,
                                   uint8_t path_hops) {
    std::vector<uint8_t> payload(pub_key, pub_key + 32);
    for (int i = 0; i < 4; ++i) payload.push_back(static_cast<uint8_t>(timestamp >> (8 * i)));
    for (std::size_t i = 0; i < kSignatureSize; ++i) payload.push_back(static_cast<uint8_t>(0x5A ^ i));  // fake signature
    uint8_t flags = node_type & 0x0F;
    if (has_location) flags |= kAdvLatLon;
    if (!name.empty()) flags |= kAdvName;
    payload.push_back(flags);
    if (has_location) {
        const auto la = static_cast<int32_t>(std::lround(lat * 1e6));
        const auto lo = static_cast<int32_t>(std::lround(lon * 1e6));
        for (int i = 0; i < 4; ++i) payload.push_back(static_cast<uint8_t>(static_cast<uint32_t>(la) >> (8 * i)));
        for (int i = 0; i < 4; ++i) payload.push_back(static_cast<uint8_t>(static_cast<uint32_t>(lo) >> (8 * i)));
    }
    payload.insert(payload.end(), name.begin(), name.end());
    return frame(PayloadType::Advert, path_hops, payload);
}

} // namespace decoder::meshcore
