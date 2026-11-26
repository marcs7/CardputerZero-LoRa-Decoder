/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Desktop traffic simulator implementing the Radio interface. It models a
 * handful of virtual transmitters (Meshtastic node, LoRaWAN end devices, a
 * private-network sensor) in EU868 and US915 so the full scan/CAD/decode
 * pipeline can be exercised without hardware. Always reported as simulated.
 */

#pragma once

#include "radio.h"

#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace platform::radio {

class SimRadio final : public Radio {
public:
    SimRadio();

    bool begin(std::string& error) override;
    void end() override {}
    const char* name() const override { return "Simulator"; }
    bool simulated() const override { return true; }

    bool configure(const decoder::LoraModem& modem, std::string& error) override;
    bool set_frequency(uint32_t freq_hz, std::string& error) override;
    bool start_rx(uint32_t timeout_ms, std::string& error) override;
    bool start_cad(uint32_t rx_timeout_ms, std::string& error) override;
    bool standby(std::string& error) override;
    RadioEvent poll(RxPacket& packet, std::string& error) override;
    bool read_rssi(float& dbm, std::string& error) override;

private:
    using Clock = std::chrono::steady_clock;

    struct Transmitter {
        decoder::LoraModem modem;
        uint32_t period_ms;
        float rssi_dbm;
        float snr_db;
        std::function<std::vector<uint8_t>()> generate;
        Clock::time_point start{};
        Clock::time_point end{};
        std::vector<uint8_t> frame;
    };

    enum class Mode { Standby, Rx, Cad };

    bool matches(const Transmitter& tx) const;
    void advance(Clock::time_point now);
    void schedule(Transmitter& tx, Clock::time_point after);
    RxPacket make_packet(const Transmitter& tx);

    std::vector<Transmitter> transmitters_;
    decoder::LoraModem modem_{};
    Mode mode_{Mode::Standby};
    Clock::time_point mode_since_{};
    Clock::time_point rx_deadline_{};
    bool rx_has_deadline_{false};
    Clock::time_point cad_done_at_{};
    int locked_tx_{-1};  // transmitter captured by CAD/RX
    std::mt19937 rng_;
};

} // namespace platform::radio
