/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "meshtastic.h"

#include "aes.h"
#include "protobuf_reader.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace decoder::meshtastic {
namespace {

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::string fmt(const char* format, double value) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), format, value);
    return buf;
}

std::string fmt_u(uint64_t value) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    return buf;
}

int64_t zigzag_or_plain(const ProtoField& f) {
    return static_cast<int32_t>(static_cast<uint32_t>(f.varint));
}

const char* hw_model_name(uint64_t model) {
    switch (model) {
        case 0: return "UNSET";
        case 1: return "TLORA_V2";
        case 2: return "TLORA_V1";
        case 3: return "TLORA_V2_1_1P6";
        case 4: return "TBEAM";
        case 5: return "HELTEC_V2_0";
        case 6: return "TBEAM_V0P7";
        case 7: return "T_ECHO";
        case 8: return "TLORA_V1_1P3";
        case 9: return "RAK4631";
        case 10: return "HELTEC_V2_1";
        case 11: return "HELTEC_V1";
        case 12: return "LILYGO_TBEAM_S3_CORE";
        case 13: return "RAK11200";
        case 14: return "NANO_G1";
        case 15: return "TLORA_V2_1_1P8";
        case 16: return "TLORA_T3_S3";
        case 17: return "NANO_G1_EXPLORER";
        case 18: return "NANO_G2_ULTRA";
        case 25: return "STATION_G1";
        case 26: return "RAK11310";
        case 29: return "CANARYONE";
        case 31: return "STATION_G2";
        case 39: return "DIY_V1";
        case 41: return "DR_DEV";
        case 42: return "M5STACK";
        case 43: return "HELTEC_V3";
        case 44: return "HELTEC_WSL_V3";
        case 47: return "RPI_PICO";
        case 48: return "HELTEC_WIRELESS_TRACKER";
        case 49: return "HELTEC_WIRELESS_PAPER";
        case 50: return "T_DECK";
        case 51: return "T_WATCH_S3";
        case 52: return "PICOMPUTER_S3";
        case 53: return "HELTEC_HT62";
        case 58: return "HELTEC_WIRELESS_PAPER_V1_0";
        case 59: return "HELTEC_WIRELESS_TRACKER_V1_0";
        case 64: return "PORTDUINO";
        case 66: return "SENSECAP_INDICATOR";
        case 71: return "TRACKER_T1000_E";
        case 72: return "RAK3172";
        case 255: return "PRIVATE_HW";
        default: return nullptr;
    }
}

void decode_position(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    bool has_lat = false, has_lon = false;
    double lat = 0, lon = 0;
    while (reader.next(f)) {
        switch (f.number) {
            case 1: lat = f.as_sint32_fixed() * 1e-7; has_lat = true; break;
            case 2: lon = f.as_sint32_fixed() * 1e-7; has_lon = true; break;
            case 3: out.add("Altitude", fmt("%.0f m", static_cast<double>(zigzag_or_plain(f)))); break;
            case 4: out.add("Time", fmt_u(f.varint)); break;
            case 14: out.add("Ground speed", fmt("%.0f m/s", static_cast<double>(f.varint))); break;
            case 19: out.add("Sats", fmt_u(f.varint)); break;
            case 23: out.add("Precision bits", fmt_u(f.varint)); break;
            default: break;
        }
    }
    if (has_lat) out.add("Latitude", fmt("%.6f", lat));
    if (has_lon) out.add("Longitude", fmt("%.6f", lon));
    if (has_lat && has_lon) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "pos %.4f,%.4f", lat, lon);
        brief = buf;
    } else {
        brief = "position";
    }
}

void decode_user(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    std::string long_name, short_name;
    while (reader.next(f)) {
        switch (f.number) {
            case 1: if (f.type == WireType::LengthDelimited) out.add("User ID", f.as_string()); break;
            case 2: if (f.type == WireType::LengthDelimited) { long_name = f.as_string(); out.add("Long name", long_name); } break;
            case 3: if (f.type == WireType::LengthDelimited) { short_name = f.as_string(); out.add("Short name", short_name); } break;
            case 5: {
                const char* name = hw_model_name(f.varint);
                out.add("HW model", name ? name : fmt_u(f.varint));
                break;
            }
            case 6: out.add("Licensed", f.varint ? "yes" : "no"); break;
            case 7: out.add("Role", fmt_u(f.varint)); break;
            default: break;
        }
    }
    brief = "node " + (short_name.empty() ? std::string("?") : short_name) +
            (long_name.empty() ? "" : " " + long_name);
}

void decode_telemetry(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    brief = "telemetry";
    while (reader.next(f)) {
        if (f.type != WireType::LengthDelimited) {
            continue;
        }
        ProtoReader sub(f.data, f.size);
        ProtoField m;
        if (f.number == 2) {  // DeviceMetrics
            brief = "telemetry dev";
            while (sub.next(m)) {
                switch (m.number) {
                    case 1: out.add("Battery", fmt_u(m.varint) + " %"); brief += " bat" + fmt_u(m.varint) + "%"; break;
                    case 2: out.add("Voltage", fmt("%.2f V", m.as_float())); break;
                    case 3: out.add("Ch util", fmt("%.1f %%", m.as_float())); break;
                    case 4: out.add("Air util TX", fmt("%.1f %%", m.as_float())); break;
                    case 5: out.add("Uptime", fmt_u(m.varint) + " s"); break;
                    default: break;
                }
            }
        } else if (f.number == 3) {  // EnvironmentMetrics
            brief = "telemetry env";
            while (sub.next(m)) {
                switch (m.number) {
                    case 1: out.add("Temperature", fmt("%.1f C", m.as_float())); brief += fmt(" %.1fC", m.as_float()); break;
                    case 2: out.add("Humidity", fmt("%.0f %%", m.as_float())); break;
                    case 3: out.add("Pressure", fmt("%.1f hPa", m.as_float())); break;
                    default: break;
                }
            }
        } else if (f.number == 4) {
            brief = "telemetry air";
        } else if (f.number == 5) {  // PowerMetrics
            brief = "telemetry power";
            while (sub.next(m)) {
                switch (m.number) {
                    case 1: out.add("Ch1 voltage", fmt("%.2f V", m.as_float())); break;
                    case 2: out.add("Ch1 current", fmt("%.1f mA", m.as_float())); break;
                    case 3: out.add("Ch2 voltage", fmt("%.2f V", m.as_float())); break;
                    case 4: out.add("Ch2 current", fmt("%.1f mA", m.as_float())); break;
                    default: break;
                }
            }
        }
    }
}

void decode_route(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    std::string route;
    while (reader.next(f)) {
        if (f.number == 1 && f.type == WireType::Fixed32) {
            route += (route.empty() ? "" : " > ") + format_node_id(static_cast<uint32_t>(f.varint));
        } else if (f.number == 1 && f.type == WireType::LengthDelimited) {  // packed
            for (std::size_t i = 0; i + 4 <= f.size; i += 4) {
                route += (route.empty() ? "" : " > ") + format_node_id(le32(f.data + i));
            }
        }
    }
    out.add("Route", route.empty() ? "(direct)" : route);
    brief = "traceroute";
}

void decode_routing(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    brief = "routing";
    while (reader.next(f)) {
        if (f.number == 3) {
            out.add("Error reason", fmt_u(f.varint));
            brief = f.varint == 0 ? "ack" : "nak err" + fmt_u(f.varint);
        }
    }
}

std::string fmt_latlon(int32_t lat_i, int32_t lon_i) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.5f,%.5f", lat_i * 1e-7, lon_i * 1e-7);
    return buf;
}

void decode_waypoint(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    int32_t lat = 0, lon = 0;
    bool has_pos = false;
    std::string name;
    while (reader.next(f)) {
        switch (f.number) {
            case 1: out.add("Waypoint ID", fmt_u(f.varint)); break;
            case 2: lat = f.as_sint32_fixed(); has_pos = true; break;
            case 3: lon = f.as_sint32_fixed(); has_pos = true; break;
            case 4: out.add("Expires", fmt_u(f.varint)); break;
            case 6: if (f.type == WireType::LengthDelimited) { name = f.as_string(); out.add("Name", name); } break;
            case 7: if (f.type == WireType::LengthDelimited) out.add("Description", f.as_string()); break;
            default: break;
        }
    }
    if (has_pos) out.add("Position", fmt_latlon(lat, lon));
    brief = "waypoint " + (name.empty() ? std::string("?") : name);
}

void decode_neighbors(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    int count = 0;
    while (reader.next(f)) {
        if (f.number == 1) {
            out.add("Node", format_node_id(static_cast<uint32_t>(f.varint)));
        } else if (f.number == 3) {
            out.add("Interval", fmt_u(f.varint) + " s");
        } else if (f.number == 4 && f.type == WireType::LengthDelimited) {
            ProtoReader sub(f.data, f.size);
            ProtoField n;
            uint32_t id = 0;
            float snr = 0;
            while (sub.next(n)) {
                if (n.number == 1) id = static_cast<uint32_t>(n.varint);
                if (n.number == 2) snr = n.as_float();
            }
            out.add("Neighbor", format_node_id(id) + fmt("  SNR %.1f", snr));
            ++count;
        }
    }
    brief = "neighbors " + std::to_string(count);
}

void decode_map_report(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    int32_t lat = 0, lon = 0;
    bool has_pos = false;
    std::string short_name;
    while (reader.next(f)) {
        const bool str = f.type == WireType::LengthDelimited;
        switch (f.number) {
            case 1: if (str) out.add("Long name", f.as_string()); break;
            case 2: if (str) { short_name = f.as_string(); out.add("Short name", short_name); } break;
            case 4: {
                const char* hw = hw_model_name(f.varint);
                out.add("HW model", hw ? hw : fmt_u(f.varint));
                break;
            }
            case 5: if (str) out.add("Firmware", f.as_string()); break;
            case 6: out.add("Region", fmt_u(f.varint)); break;
            case 7: out.add("Modem preset", fmt_u(f.varint)); break;
            case 9: lat = f.as_sint32_fixed(); has_pos = true; break;
            case 10: lon = f.as_sint32_fixed(); has_pos = true; break;
            case 13: out.add("Online nodes", fmt_u(f.varint)); break;
            default: break;
        }
    }
    if (has_pos) out.add("Position", fmt_latlon(lat, lon));
    brief = "map report " + short_name;
}

void decode_paxcounter(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    ProtoReader reader(data, size);
    ProtoField f;
    uint64_t wifi = 0, ble = 0;
    while (reader.next(f)) {
        if (f.number == 1) wifi = f.varint;
        if (f.number == 2) ble = f.varint;
        if (f.number == 3) out.add("Uptime", fmt_u(f.varint) + " s");
    }
    out.add("WiFi devices", fmt_u(wifi));
    out.add("BLE devices", fmt_u(ble));
    brief = "pax wifi " + fmt_u(wifi) + " ble " + fmt_u(ble);
}

// Decodes a meshtastic Data protobuf. Returns false if it doesn't look like one.
bool decode_data(const uint8_t* data, std::size_t size, DecodedPacket& out, std::string& brief) {
    if (!looks_like_protobuf(data, size)) {
        return false;
    }
    ProtoReader reader(data, size);
    ProtoField f;
    bool has_port = false;
    uint32_t portnum = 0;
    const uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
    std::vector<DecodedField> extra;

    while (reader.next(f)) {
        switch (f.number) {
            case 1:
                if (f.type != WireType::Varint) return false;
                portnum = static_cast<uint32_t>(f.varint);
                has_port = true;
                break;
            case 2:
                if (f.type != WireType::LengthDelimited) return false;
                payload = f.data;
                payload_size = f.size;
                break;
            case 3: extra.push_back({"Want response", f.varint ? "yes" : "no"}); break;
            case 4: extra.push_back({"Dest", format_node_id(static_cast<uint32_t>(f.varint))}); break;
            case 5: extra.push_back({"Source", format_node_id(static_cast<uint32_t>(f.varint))}); break;
            case 6: extra.push_back({"Request ID", fmt_u(f.varint)}); break;
            case 7: extra.push_back({"Reply ID", fmt_u(f.varint)}); break;
            case 8: extra.push_back({"Emoji", fmt_u(f.varint)}); break;
            case 9: break;
            default: return false;  // unknown field: likely wrong key
        }
    }
    const char* port_name = portnum_name(portnum);
    if (!has_port || !port_name) {
        return false;
    }

    out.add("Port", std::string(port_name) + " (" + fmt_u(portnum) + ")");
    for (auto& e : extra) {
        out.fields.push_back(std::move(e));
    }

    if (!payload) {
        brief = port_name;
        return true;
    }
    switch (portnum) {
        case 1: {  // TEXT_MESSAGE_APP
            std::string text(reinterpret_cast<const char*>(payload), payload_size);
            out.add("Text", text);
            brief = "\"" + text + "\"";
            break;
        }
        case 3: decode_position(payload, payload_size, out, brief); break;
        case 4: decode_user(payload, payload_size, out, brief); break;
        case 5: decode_routing(payload, payload_size, out, brief); break;
        case 67: decode_telemetry(payload, payload_size, out, brief); break;
        case 70: decode_route(payload, payload_size, out, brief); break;
        case 8: decode_waypoint(payload, payload_size, out, brief); break;
        case 71: decode_neighbors(payload, payload_size, out, brief); break;
        case 73: decode_map_report(payload, payload_size, out, brief); break;
        case 34: decode_paxcounter(payload, payload_size, out, brief); break;
        case 66: {  // RANGE_TEST: plain text "seq N"
            std::string text(reinterpret_cast<const char*>(payload), payload_size);
            out.add("Range test", text);
            brief = "range test " + text;
            break;
        }
        default:
            out.add("Payload", to_hex(payload, payload_size));
            brief = port_name;
            break;
    }
    return true;
}

} // namespace

bool parse_header(const uint8_t* data, std::size_t size, Header& header) {
    if (!data || size < kHeaderSize) {
        return false;
    }
    header.to = le32(data);
    header.from = le32(data + 4);
    header.id = le32(data + 8);
    header.flags = data[12];
    header.channel_hash = data[13];
    header.next_hop = data[14];
    header.relay_node = data[15];
    return true;
}

uint8_t xor_hash(const uint8_t* data, std::size_t size) {
    uint8_t h = 0;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= data[i];
    }
    return h;
}

uint8_t ChannelKey::hash() const {
    return static_cast<uint8_t>(
        xor_hash(reinterpret_cast<const uint8_t*>(name.data()), name.size()) ^
        xor_hash(key.data(), key.size()));
}

const std::vector<uint8_t>& default_key() {
    static const std::vector<uint8_t> key = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                             0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};
    return key;
}

std::vector<uint8_t> expand_psk(const std::vector<uint8_t>& psk) {
    if (psk.size() == 1) {
        if (psk[0] == 0) {
            return {};  // no encryption
        }
        auto key = default_key();
        key.back() = static_cast<uint8_t>(key.back() + psk[0] - 1);
        return key;
    }
    if (psk.size() < 16 && !psk.empty()) {
        std::vector<uint8_t> key = psk;
        key.resize(16, 0);
        return key;
    }
    if (psk.size() > 16 && psk.size() < 32) {
        std::vector<uint8_t> key = psk;
        key.resize(32, 0);
        return key;
    }
    return psk;
}

std::vector<ChannelKey> default_channel_keys() {
    std::vector<ChannelKey> keys;
    for (const auto& preset : modem_presets()) {
        keys.push_back({preset.name, default_key()});
    }
    return keys;
}

bool decrypt(const Header& header,
             const std::vector<uint8_t>& key,
             const uint8_t* cipher,
             std::size_t size,
             std::vector<uint8_t>& plain) {
    plain.assign(cipher, cipher + size);
    if (key.empty()) {
        return true;  // channel without encryption
    }
    uint8_t nonce[16] = {};
    const uint64_t packet_id = header.id;
    for (int i = 0; i < 8; ++i) {
        nonce[i] = static_cast<uint8_t>(packet_id >> (8 * i));
    }
    for (int i = 0; i < 4; ++i) {
        nonce[8 + i] = static_cast<uint8_t>(header.from >> (8 * i));
    }
    return aes_ctr_xcrypt(key.data(), key.size(), nonce, cipher, size, plain.data());
}

bool decode(const uint8_t* data,
            std::size_t size,
            const std::vector<ChannelKey>& keys,
            DecodedPacket& out,
            bool* decrypted) {
    if (decrypted) {
        *decrypted = false;
    }
    Header h;
    if (!parse_header(data, size, h)) {
        return false;
    }
    // A Meshtastic sender is never broadcast/zero and hop_start >= hop_limit.
    if (h.from == 0 || h.from == 0xFFFFFFFFU) {
        return false;
    }

    out.protocol = Protocol::Meshtastic;
    out.add("From", format_node_id(h.from));
    out.add("To", format_node_id(h.to));
    char buf[64];
    std::snprintf(buf, sizeof(buf), "0x%08X", h.id);
    out.add("Packet ID", buf);
    std::snprintf(buf, sizeof(buf), "%u/%u%s%s", h.hop_limit(), h.hop_start(),
                  h.want_ack() ? " ack" : "", h.via_mqtt() ? " mqtt" : "");
    out.add("Hops left/start", buf);
    std::snprintf(buf, sizeof(buf), "0x%02X", h.channel_hash);
    out.add("Channel hash", buf);
    if (h.next_hop || h.relay_node) {
        std::snprintf(buf, sizeof(buf), "next %02X relay %02X", h.next_hop, h.relay_node);
        out.add("Relay", buf);
    }

    const uint8_t* cipher = data + kHeaderSize;
    const std::size_t cipher_size = size - kHeaderSize;
    std::string from_short = format_node_id(h.from);

    for (const auto& ck : keys) {
        if (ck.hash() != h.channel_hash) {
            continue;
        }
        std::vector<uint8_t> plain;
        if (!decrypt(h, ck.key, cipher, cipher_size, plain)) {
            continue;
        }
        DecodedPacket attempt = out;
        std::string brief;
        if (decode_data(plain.data(), plain.size(), attempt, brief)) {
            attempt.fields.insert(attempt.fields.begin() + 5, {"Channel", ck.name});
            attempt.summary = from_short + " " + brief;
            out = std::move(attempt);
            if (decrypted) {
                *decrypted = true;
            }
            return true;
        }
    }

    std::snprintf(buf, sizeof(buf), "%zu bytes", cipher_size);
    out.add("Payload", std::string(buf) + " encrypted (unknown key)");
    out.summary = from_short + " > " + format_node_id(h.to) + " enc";
    return true;
}

const char* portnum_name(uint32_t portnum) {
    switch (portnum) {
        case 0: return "UNKNOWN_APP";
        case 1: return "TEXT_MESSAGE";
        case 2: return "REMOTE_HARDWARE";
        case 3: return "POSITION";
        case 4: return "NODEINFO";
        case 5: return "ROUTING";
        case 6: return "ADMIN";
        case 7: return "TEXT_COMPRESSED";
        case 8: return "WAYPOINT";
        case 9: return "AUDIO";
        case 10: return "DETECTION_SENSOR";
        case 11: return "ALERT";
        case 12: return "KEY_VERIFICATION";
        case 32: return "REPLY";
        case 33: return "IP_TUNNEL";
        case 34: return "PAXCOUNTER";
        case 64: return "SERIAL";
        case 65: return "STORE_FORWARD";
        case 66: return "RANGE_TEST";
        case 67: return "TELEMETRY";
        case 68: return "ZPS";
        case 69: return "SIMULATOR";
        case 70: return "TRACEROUTE";
        case 71: return "NEIGHBORINFO";
        case 72: return "ATAK_PLUGIN";
        case 73: return "MAP_REPORT";
        case 74: return "POWERSTRESS";
        case 76: return "RETICULUM_TUNNEL";
        case 77: return "CAYENNE";
        case 256: return "PRIVATE_APP";
        case 257: return "ATAK_FORWARDER";
        default: return nullptr;
    }
}

const std::vector<ModemPreset>& modem_presets() {
    static const std::vector<ModemPreset> presets = {
        {"LongFast", 250000, 11, 5},
        {"MediumFast", 250000, 9, 5},
        {"MediumSlow", 250000, 10, 5},
        {"LongSlow", 125000, 12, 8},
        {"LongMod", 125000, 11, 8},
        {"ShortFast", 250000, 7, 5},
        {"ShortSlow", 250000, 8, 5},
        {"ShortTurbo", 500000, 7, 5},
        {"LongTurbo", 500000, 11, 8},
    };
    return presets;
}

const std::vector<Region>& regions() {
    static const std::vector<Region> list = {
        {"US", 902.0, 928.0, 0.0},
        {"EU_868", 869.4, 869.65, 0.0},
        {"EU_433", 433.0, 434.0, 0.0},
        {"ANZ", 915.0, 928.0, 0.0},
        {"IN", 865.0, 867.0, 0.0},
        {"KR", 920.0, 923.0, 0.0},
        {"JP", 920.5, 923.5, 0.0},
        {"CN", 470.0, 510.0, 0.0},
        {"RU", 868.7, 869.2, 0.0},
        {"TW", 920.0, 925.0, 0.0},
        {"NZ_865", 864.0, 868.0, 0.0},
        {"UA_868", 868.0, 868.6, 0.0},
        {"BR_902", 902.0, 907.5, 0.0},
    };
    return list;
}

const Region* find_region(const std::string& name) {
    for (const auto& region : regions()) {
        if (name == region.name) {
            return &region;
        }
    }
    return nullptr;
}

uint32_t djb2_hash(const std::string& text) {
    uint32_t hash = 5381;
    for (const unsigned char c : text) {
        hash = (hash << 5) + hash + c;
    }
    return hash;
}

uint32_t default_frequency_hz(const Region& region, const ModemPreset& preset,
                              const std::string& channel_name) {
    const double bw_mhz = preset.bandwidth_hz / 1e6;
    const double span = region.freq_end_mhz - region.freq_start_mhz;
    const auto num_channels =
        static_cast<uint32_t>(std::floor(span / (region.spacing_mhz + bw_mhz) + 1e-9));
    if (num_channels == 0) {
        return 0;
    }
    const uint32_t slot = djb2_hash(channel_name) % num_channels;
    const double freq = region.freq_start_mhz + region.spacing_mhz + bw_mhz / 2.0 +
                        slot * (bw_mhz + region.spacing_mhz);
    return static_cast<uint32_t>(std::lround(freq * 1e6));
}

} // namespace decoder::meshtastic
