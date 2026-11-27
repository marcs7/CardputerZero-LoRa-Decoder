/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Owns the Radio on a worker thread and runs one of three modes:
 *   - Spectrum: RSSI sweep across a frequency span
 *   - Hunt:     hop through scan targets, CAD per SF, receive + decode
 *   - Listen:   continuous receive on one modem configuration
 * The LVGL thread only exchanges plain data with it under a mutex.
 */

#pragma once

#include "decoded_packet.h"
#include "frequency_plan.h"
#include "meshcore.h"
#include "meshtastic.h"
#include "radio.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform::radio {

struct Sx1262Config;

enum class ServiceMode : uint8_t { Idle = 0, Spectrum, Hunt, Listen };

struct SpectrumConfig {
    uint32_t start_hz{863000000};
    uint32_t end_hz{870000000};
    uint32_t step_hz{50000};
    uint32_t rbw_hz{125000};
};

struct HuntOptions {
    uint32_t dwell_ms{0};  // 0 => CAD-driven hopping, otherwise fixed dwell per target/SF
};

struct PacketRecord {
    uint64_t seq{0};
    std::chrono::system_clock::time_point time{};
    decoder::LoraModem modem{};
    std::string source;  // scan target label or "Listen"
    RxPacket rx;
    decoder::DecodedPacket decoded;
};

struct SpectrumFrame {
    uint32_t start_hz{0};
    uint32_t step_hz{0};
    std::vector<float> dbm;
    uint32_t sweep_ms{0};
};

struct ServiceStatus {
    ServiceMode mode{ServiceMode::Idle};
    bool radio_ready{false};
    bool simulated{false};
    std::string backend;
    std::string error;
    // hunt
    std::string target_label;
    uint32_t target_freq_hz{0};
    uint8_t target_sf{0};
    uint32_t hunt_cycles{0};
    uint32_t cad_hits{0};
    std::size_t target_index{0};
    std::size_t target_count{0};
    // listen
    float rssi_dbm{-140.0f};
    // counters
    uint32_t packets{0};
    uint32_t crc_errors{0};
};

class RadioService {
public:
    explicit RadioService(std::unique_ptr<Radio> radio);
    ~RadioService();

    RadioService(const RadioService&) = delete;
    RadioService& operator=(const RadioService&) = delete;

    // Initialises the radio and spawns the worker. Returns false (with error)
    // if the radio could not be brought up; the service stays usable (Idle).
    bool start(std::string& error);
    void stop();

    void request_idle();
    void request_spectrum(const SpectrumConfig& config);
    void request_hunt(std::vector<decoder::ScanTarget> targets, HuntOptions options);
    void request_listen(const decoder::LoraModem& modem);
    void set_meshtastic_keys(std::vector<decoder::meshtastic::ChannelKey> keys);
    void set_meshcore_channels(std::vector<decoder::meshcore::Channel> channels);

    // LVGL thread side
    std::vector<PacketRecord> take_packets();
    bool take_spectrum(SpectrumFrame& frame);
    ServiceStatus status() const;

private:
    struct Command {
        ServiceMode mode{ServiceMode::Idle};
        SpectrumConfig spectrum;
        std::vector<decoder::ScanTarget> targets;
        HuntOptions hunt;
        decoder::LoraModem modem;
    };

    void run();
    bool command_pending() const { return pending_flag_.load(std::memory_order_acquire); }
    void apply_command(Command& active);
    void run_spectrum(const SpectrumConfig& config);
    void run_hunt_step(const Command& active);
    void run_listen_step(const decoder::LoraModem& modem, bool& configured);
    bool wait_packet(const decoder::LoraModem& modem, const std::string& source,
                     std::chrono::milliseconds max_wait);
    void publish_packet(RxPacket&& rx, const decoder::LoraModem& modem, const std::string& source);
    void set_error(const std::string& error);
    void sleep_short();

    std::unique_ptr<Radio> radio_;
    std::thread worker_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Command pending_;
    std::atomic<bool> pending_flag_{false};
    std::vector<decoder::meshtastic::ChannelKey> keys_;
    std::vector<decoder::meshcore::Channel> meshcore_channels_;
    std::vector<PacketRecord> packets_;
    SpectrumFrame spectrum_;
    bool spectrum_fresh_{false};
    ServiceStatus status_;
    uint64_t next_seq_{1};

    // worker-only hunt cursor
    std::size_t hunt_target_{0};
    uint8_t hunt_sf_{0};
};

std::unique_ptr<Radio> make_radio_from_backend(const std::string& backend,
                                               const Sx1262Config& config);

} // namespace platform::radio
