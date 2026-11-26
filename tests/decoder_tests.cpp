/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host unit tests for the LoRa packet decoder library (no LVGL, no hardware).
 */

#include "aes.h"
#include "decoded_packet.h"
#include "frequency_plan.h"
#include "lorawan.h"
#include "meshtastic.h"
#include "packet_decoder.h"
#include "protobuf_reader.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace decoder;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                      \
    } while (0)

static std::vector<uint8_t> from_hex(const std::string& hex) {
    std::vector<uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

static std::string field(const DecodedPacket& p, const std::string& key) {
    for (const auto& f : p.fields) {
        if (f.key == key) return f.value;
    }
    return "<missing>";
}

static void test_aes() {
    // FIPS-197 Appendix C vectors
    const auto pt = from_hex("00112233445566778899aabbccddeeff");
    uint8_t out[16];
    Aes aes;
    CHECK(aes.set_key(from_hex("000102030405060708090a0b0c0d0e0f").data(), 16));
    aes.encrypt_block(pt.data(), out);
    CHECK(to_hex(out, 16, 0) == "69C4E0D86A7B0430D8CDB78070B4C55A");

    CHECK(aes.set_key(from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f").data(), 32));
    aes.encrypt_block(pt.data(), out);
    CHECK(to_hex(out, 16, 0) == "8EA2B7CA516745BFEAFC49904B496089");

    // NIST SP800-38A F.5.1 CTR-AES128
    const auto key = from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    const auto ctr = from_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const auto p = from_hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
    std::vector<uint8_t> c(p.size());
    CHECK(aes_ctr_xcrypt(key.data(), 16, ctr.data(), p.data(), p.size(), c.data()));
    CHECK(to_hex(c.data(), c.size(), 0) ==
          "874D6191B620E3261BEF6864990DB6CE9806F66B7970FDFF8617187BB9FFFDFF");
}

static void test_meshtastic_constants() {
    meshtastic::ChannelKey lf{"LongFast", meshtastic::default_key()};
    CHECK(lf.hash() == 0x08);
    CHECK(meshtastic::expand_psk({1}) == meshtastic::default_key());

    const auto* us = meshtastic::find_region("US");
    const auto* eu = meshtastic::find_region("EU_868");
    CHECK(us && eu);
    const auto& long_fast = meshtastic::modem_presets()[0];
    CHECK(meshtastic::default_frequency_hz(*us, long_fast, "LongFast") == 906875000);
    CHECK(meshtastic::default_frequency_hz(*eu, long_fast, "LongFast") == 869525000);

    std::vector<uint8_t> b64;
    CHECK(base64_decode("AQ==", b64) && b64.size() == 1 && b64[0] == 1);
}

static std::vector<uint8_t> build_meshtastic_packet(uint32_t from, uint32_t to, uint32_t id,
                                                    const std::vector<uint8_t>& data_pb,
                                                    const std::string& channel) {
    meshtastic::ChannelKey ck{channel, meshtastic::default_key()};
    std::vector<uint8_t> pkt(16);
    auto put32 = [&](std::size_t off, uint32_t v) {
        for (int i = 0; i < 4; ++i) pkt[off + i] = static_cast<uint8_t>(v >> (8 * i));
    };
    put32(0, to);
    put32(4, from);
    put32(8, id);
    pkt[12] = static_cast<uint8_t>((3 << 5) | 3);  // hop_start 3, hop_limit 3
    pkt[13] = ck.hash();
    meshtastic::Header h;
    meshtastic::parse_header(pkt.data(), pkt.size(), h);
    std::vector<uint8_t> enc;
    meshtastic::decrypt(h, ck.key, data_pb.data(), data_pb.size(), enc);
    pkt.insert(pkt.end(), enc.begin(), enc.end());
    return pkt;
}

static void test_meshtastic_roundtrip() {
    // Data { portnum: TEXT_MESSAGE_APP(1), payload: "hello mesh" }
    std::vector<uint8_t> pb = {0x08, 0x01, 0x12, 10};
    const std::string text = "hello mesh";
    pb.insert(pb.end(), text.begin(), text.end());
    auto pkt = build_meshtastic_packet(0xdeadbeef, 0xffffffff, 0x12345678, pb, "LongFast");

    DecodeContext ctx;
    ctx.sync_word = meshtastic::kSyncWord;
    auto p = decode_packet(pkt.data(), pkt.size(), ctx);
    CHECK(p.protocol == Protocol::Meshtastic);
    CHECK(field(p, "Text") == "hello mesh");
    CHECK(field(p, "From") == "!deadbeef");
    CHECK(field(p, "To") == "^all");
    CHECK(field(p, "Channel") == "LongFast");
    CHECK(p.summary == "!deadbeef \"hello mesh\"");

    // Position: lat 45.4642040 lon 9.1899820 (sfixed32 * 1e7)
    std::vector<uint8_t> pos = {0x0d};
    int32_t lat = 454642040, lon = 91899820;
    for (int i = 0; i < 4; ++i) pos.push_back(static_cast<uint8_t>(static_cast<uint32_t>(lat) >> (8 * i)));
    pos.push_back(0x15);
    for (int i = 0; i < 4; ++i) pos.push_back(static_cast<uint8_t>(static_cast<uint32_t>(lon) >> (8 * i)));
    std::vector<uint8_t> pb2 = {0x08, 0x03, 0x12, static_cast<uint8_t>(pos.size())};
    pb2.insert(pb2.end(), pos.begin(), pos.end());
    auto pkt2 = build_meshtastic_packet(0x0a0b0c0d, 0xffffffff, 77, pb2, "MediumFast");
    auto p2 = decode_packet(pkt2.data(), pkt2.size(), ctx);
    CHECK(field(p2, "Latitude") == "45.464204");
    CHECK(field(p2, "Longitude") == "9.189982");
    CHECK(field(p2, "Channel") == "MediumFast");

    // Wrong key -> still Meshtastic, flagged encrypted
    pkt[13] ^= 0x55;
    auto p3 = decode_packet(pkt.data(), pkt.size(), ctx);
    CHECK(p3.protocol == Protocol::Meshtastic);
    CHECK(p3.summary.find("enc") != std::string::npos);
}

static void test_lorawan() {
    const auto frame = from_hex("40F17DBE4900020001954378762B11FF0D");
    auto f = lorawan::parse(frame.data(), frame.size());
    CHECK(f.valid);
    CHECK(f.mtype == lorawan::MType::UnconfirmedUp);
    CHECK(f.dev_addr == 0x49BE7DF1);
    CHECK(f.fcnt == 2);
    CHECK(f.has_fport && f.fport == 1);
    CHECK(f.frm_payload_len == 4);
    CHECK(f.mic == 0x0DFF112B);

    DecodeContext ctx;
    auto p = decode_packet(frame.data(), frame.size(), ctx);
    CHECK(p.protocol == Protocol::LoRaWAN);
    CHECK(field(p, "DevAddr") == "49BE7DF1");
    CHECK(p.summary == "UnconfUp 49BE7DF1 fc2 p1");

    // Join request: MHDR 00, JoinEUI, DevEUI, DevNonce, MIC = 23 bytes
    const auto join = from_hex("00DC0000D07ED5B37000000000000000001F09A0B1C2D3");
    CHECK(join.size() == 23);
    auto pj = decode_packet(join.data(), join.size(), ctx);
    CHECK(pj.protocol == Protocol::LoRaWAN);
    CHECK(field(pj, "JoinEUI") == "70B3D57ED00000DC");

    CHECK(lorawan::describe_dev_addr(0x26011234).find("13 (TTN)") != std::string::npos);

    // Garbage on public sync word -> raw
    const auto junk = from_hex("FF0102");
    auto pr = decode_packet(junk.data(), junk.size(), ctx);
    CHECK(pr.protocol == Protocol::Raw);
}

static void test_raw_text_and_plans() {
    const std::string t = "HELLO LORA 123";
    DecodeContext ctx;
    ctx.sync_word = 0x12;
    auto p = decode_packet(reinterpret_cast<const uint8_t*>(t.data()), t.size(), ctx);
    CHECK(p.protocol == Protocol::Raw);
    CHECK(field(p, "Text") == t);

    const auto* eu = find_band_plan("EU868");
    CHECK(eu != nullptr);
    auto targets = build_scan_targets(*eu, ScanProfile::All);
    CHECK(!targets.empty());
    CHECK(targets[0].label == "MT LongFast");
    CHECK(targets[0].modem.freq_hz == 869525000);
    auto lw = build_scan_targets(*eu, ScanProfile::LoRaWAN);
    CHECK(lw.size() == 8);
    CHECK(lw[0].sf_mask == sf_mask_range(7, 12));
    CHECK(format_mhz(868100000) == "868.100");
    CHECK(format_bw(62500) == "62.5k");

    LoraModem m;
    m.spreading_factor = 7;
    m.bandwidth_hz = 125000;
    m.preamble = 8;
    const double toa = time_on_air_ms(m, 10);
    CHECK(toa > 40.0 && toa < 42.0);  // ~41.2 ms (Semtech calculator)

    CHECK(looks_like_protobuf(reinterpret_cast<const uint8_t*>("\x08\x01"), 2));
}

int main() {
    test_aes();
    test_meshtastic_constants();
    test_meshtastic_roundtrip();
    test_lorawan();
    test_raw_text_and_plans();
    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
