/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Receive-only LoRa radio abstraction used by RadioService. Implemented by
 * the SX1262 hardware driver and by a desktop traffic simulator.
 * All methods are called from the radio worker thread only.
 */

#pragma once

#include "frequency_plan.h"

#include <cstdint>
#include <string>
#include <vector>

namespace platform::radio {

struct RxPacket {
    std::vector<uint8_t> data;
    float rssi_dbm{0};
    float snr_db{0};
    float signal_rssi_dbm{0};
    bool crc_error{false};
};

enum class RadioEvent {
    None,
    RxDone,     // packet (possibly with crc_error)
    HeaderError,
    Timeout,
    CadClear,   // CAD finished, nothing detected
    CadBusy,    // CAD detected activity; radio moved to RX automatically
};

class Radio {
public:
    virtual ~Radio() = default;

    virtual bool begin(std::string& error) = 0;
    virtual void end() = 0;
    virtual const char* name() const = 0;
    virtual bool simulated() const = 0;

    // Configures RF + modem + packet params; leaves the radio in standby.
    virtual bool configure(const decoder::LoraModem& modem, std::string& error) = 0;
    // Fast retune for spectrum sweeps (radio may stay in RX).
    virtual bool set_frequency(uint32_t freq_hz, std::string& error) = 0;

    // timeout_ms == 0 means continuous receive.
    virtual bool start_rx(uint32_t timeout_ms, std::string& error) = 0;
    // CAD on the currently configured modem; on detection the radio enters
    // RX for up to rx_timeout_ms waiting for a header.
    virtual bool start_cad(uint32_t rx_timeout_ms, std::string& error) = 0;
    virtual bool standby(std::string& error) = 0;

    virtual RadioEvent poll(RxPacket& packet, std::string& error) = 0;
    virtual bool read_rssi(float& dbm, std::string& error) = 0;
};

} // namespace platform::radio
