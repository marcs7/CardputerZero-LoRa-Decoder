/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pcap_writer.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

namespace decoder {
namespace {

void put_le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void put_le16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

uint8_t encode_rssi(float dbm) {
    // LoRaTAP: rssi_dbm = -139 + value
    const long v = std::lround(dbm + 139.0f);
    return static_cast<uint8_t>(std::clamp<long>(v, 0, 255));
}

} // namespace

void build_loratap_header(const LoraModem& modem, float rssi_dbm, float snr_db, uint8_t out[kLoRaTapHeaderSize]) {
    out[0] = 0;  // version
    out[1] = 0;  // padding
    out[2] = 0;  // header length, big endian
    out[3] = static_cast<uint8_t>(kLoRaTapHeaderSize);
    const uint32_t f = modem.freq_hz;  // big endian
    out[4] = static_cast<uint8_t>(f >> 24);
    out[5] = static_cast<uint8_t>(f >> 16);
    out[6] = static_cast<uint8_t>(f >> 8);
    out[7] = static_cast<uint8_t>(f);
    out[8] = static_cast<uint8_t>(modem.bandwidth_hz / 125000U);  // 1=125k 2=250k 4=500k
    out[9] = modem.spreading_factor;
    const uint8_t rssi = encode_rssi(rssi_dbm);
    out[10] = rssi;  // packet rssi
    out[11] = rssi;  // max rssi
    out[12] = rssi;  // current rssi
    out[13] = static_cast<uint8_t>(static_cast<int8_t>(std::clamp<long>(std::lround(snr_db * 4.0f), -128, 127)));
    out[14] = modem.sync_word;
}

PcapWriter::~PcapWriter() {
    close();
}

bool PcapWriter::open(const std::string& path, std::string& error) {
    close();
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) {
        error = "open " + path + ": " + std::strerror(errno);
        return false;
    }
    uint8_t header[24];
    put_le32(header, 0xa1b2c3d4);  // microsecond timestamps
    put_le16(header + 4, 2);
    put_le16(header + 6, 4);
    put_le32(header + 8, 0);       // thiszone
    put_le32(header + 12, 0);      // sigfigs
    put_le32(header + 16, 65535);  // snaplen
    put_le32(header + 20, kLinkTypeLoRaTap);
    if (std::fwrite(header, sizeof(header), 1, file_) != 1 || std::fflush(file_) != 0) {
        error = "write " + path + ": " + std::strerror(errno);
        close();
        return false;
    }
    path_ = path;
    count_ = 0;
    return true;
}

void PcapWriter::close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

bool PcapWriter::write(const LoraModem& modem, const uint8_t* data, std::size_t size, float rssi_dbm, float snr_db,
                       std::chrono::system_clock::time_point time) {
    if (!file_) {
        return false;
    }
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count();
    const auto total = static_cast<uint32_t>(kLoRaTapHeaderSize + size);
    uint8_t rec[16 + kLoRaTapHeaderSize];
    put_le32(rec, static_cast<uint32_t>(us / 1000000));
    put_le32(rec + 4, static_cast<uint32_t>(us % 1000000));
    put_le32(rec + 8, total);
    put_le32(rec + 12, total);
    build_loratap_header(modem, rssi_dbm, snr_db, rec + 16);
    const bool ok = std::fwrite(rec, sizeof(rec), 1, file_) == 1 &&
                    (size == 0 || std::fwrite(data, size, 1, file_) == 1) && std::fflush(file_) == 0;
    if (ok) {
        ++count_;
    }
    return ok;
}

} // namespace decoder
