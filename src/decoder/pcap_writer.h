/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PCAP capture writer using the LoRaTAP v0 link type (DLT 270), readable by
 * Wireshark and Kismet. Every record is flushed so a capture survives power loss.
 */

#pragma once

#include "frequency_plan.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace decoder {

constexpr uint32_t kLinkTypeLoRaTap = 270;
constexpr std::size_t kLoRaTapHeaderSize = 15;

class PcapWriter {
public:
    PcapWriter() = default;
    ~PcapWriter();

    PcapWriter(const PcapWriter&) = delete;
    PcapWriter& operator=(const PcapWriter&) = delete;

    bool open(const std::string& path, std::string& error);
    void close();
    bool is_open() const { return file_ != nullptr; }
    const std::string& path() const { return path_; }
    uint32_t count() const { return count_; }

    bool write(const LoraModem& modem, const uint8_t* data, std::size_t size, float rssi_dbm, float snr_db,
               std::chrono::system_clock::time_point time);

private:
    std::FILE* file_{nullptr};
    std::string path_;
    uint32_t count_{0};
};

// Builds the 15-byte LoRaTAP v0 header (exposed for tests).
void build_loratap_header(const LoraModem& modem, float rssi_dbm, float snr_db, uint8_t out[kLoRaTapHeaderSize]);

} // namespace decoder
