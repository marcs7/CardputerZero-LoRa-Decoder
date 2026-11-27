/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "radio_service.h"

#include "packet_decoder.h"
#include "sim_radio.h"
#include "sx1262.h"

#include <algorithm>
#include <cmath>

namespace platform::radio {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t kMaxQueuedPackets = 64;

double symbol_ms(const decoder::LoraModem& m) {
    return std::ldexp(1.0, m.spreading_factor) / m.bandwidth_hz * 1000.0;
}

uint8_t next_sf(uint16_t mask, uint8_t after) {
    for (uint8_t sf = static_cast<uint8_t>(after + 1); sf <= 12; ++sf) {
        if (mask & (1U << sf)) {
            return sf;
        }
    }
    return 0;
}

} // namespace

std::unique_ptr<Radio> make_radio_from_backend(const std::string& backend, const Sx1262Config& config) {
    if (backend == "sim" || backend == "simulator") {
        return std::make_unique<SimRadio>();
    }
    return std::make_unique<Sx1262>(config);
}

RadioService::RadioService(std::unique_ptr<Radio> radio) : radio_(std::move(radio)) {
    keys_ = decoder::meshtastic::default_channel_keys();
    meshcore_channels_ = decoder::meshcore::default_channels();
    status_.backend = radio_ ? radio_->name() : "none";
    status_.simulated = radio_ && radio_->simulated();
}

RadioService::~RadioService() {
    stop();
}

bool RadioService::start(std::string& error) {
    if (running_) {
        return true;
    }
    if (!radio_) {
        error = "no radio backend";
        set_error(error);
        return false;
    }
    if (!radio_->begin(error)) {
        set_error(error);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.radio_ready = true;
        status_.error.clear();
    }
    running_ = true;
    worker_ = std::thread(&RadioService::run, this);
    return true;
}

void RadioService::stop() {
    if (running_.exchange(false)) {
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    if (radio_) {
        radio_->end();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.radio_ready = false;
}

void RadioService::request_idle() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Command{};
    pending_.mode = ServiceMode::Idle;
    pending_flag_ = true;
    wake_.notify_all();
}

void RadioService::request_spectrum(const SpectrumConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Command{};
    pending_.mode = ServiceMode::Spectrum;
    pending_.spectrum = config;
    pending_flag_ = true;
    wake_.notify_all();
}

void RadioService::request_hunt(std::vector<decoder::ScanTarget> targets, HuntOptions options) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Command{};
    pending_.mode = ServiceMode::Hunt;
    pending_.targets = std::move(targets);
    pending_.hunt = options;
    pending_flag_ = true;
    wake_.notify_all();
}

void RadioService::request_listen(const decoder::LoraModem& modem) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Command{};
    pending_.mode = ServiceMode::Listen;
    pending_.modem = modem;
    pending_flag_ = true;
    wake_.notify_all();
}

void RadioService::set_meshtastic_keys(std::vector<decoder::meshtastic::ChannelKey> keys) {
    std::lock_guard<std::mutex> lock(mutex_);
    keys_ = std::move(keys);
}

void RadioService::set_meshcore_channels(std::vector<decoder::meshcore::Channel> channels) {
    std::lock_guard<std::mutex> lock(mutex_);
    meshcore_channels_ = std::move(channels);
}

std::vector<PacketRecord> RadioService::take_packets() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PacketRecord> out;
    out.swap(packets_);
    return out;
}

bool RadioService::take_spectrum(SpectrumFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!spectrum_fresh_) {
        return false;
    }
    frame = spectrum_;
    spectrum_fresh_ = false;
    return true;
}

ServiceStatus RadioService::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void RadioService::set_error(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_.error = error;
}

void RadioService::sleep_short() {
    std::this_thread::sleep_for(std::chrono::microseconds(radio_->simulated() ? 1000 : 500));
}

void RadioService::apply_command(Command& active) {
    std::lock_guard<std::mutex> lock(mutex_);
    active = std::move(pending_);
    pending_ = Command{};
    pending_flag_ = false;
    status_.mode = active.mode;
    status_.error.clear();
    status_.hunt_cycles = 0;
    status_.cad_hits = 0;
    status_.target_count = active.targets.size();
    hunt_target_ = 0;
    hunt_sf_ = 0;
}

void RadioService::run() {
    Command active;
    bool listen_configured = false;

    while (running_) {
        if (command_pending()) {
            apply_command(active);
            listen_configured = false;
            std::string err;
            radio_->standby(err);
        }

        switch (active.mode) {
            case ServiceMode::Idle: {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(200),
                               [this] { return pending_flag_.load() || !running_; });
                break;
            }
            case ServiceMode::Spectrum:
                run_spectrum(active.spectrum);
                break;
            case ServiceMode::Hunt:
                run_hunt_step(active);
                break;
            case ServiceMode::Listen:
                run_listen_step(active.modem, listen_configured);
                break;
        }
    }
    std::string err;
    radio_->standby(err);
}

void RadioService::run_spectrum(const SpectrumConfig& config) {
    std::string err;
    if (config.step_hz == 0 || config.end_hz <= config.start_hz) {
        set_error("invalid sweep range");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return;
    }
    decoder::LoraModem m;
    m.freq_hz = config.start_hz;
    m.bandwidth_hz = config.rbw_hz;
    m.spreading_factor = 7;
    if (!radio_->configure(m, err)) {
        set_error(err);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return;
    }

    const auto t0 = Clock::now();
    const std::size_t bins = (config.end_hz - config.start_hz) / config.step_hz + 1;
    std::vector<float> levels(bins, -140.0f);
    for (std::size_t i = 0; i < bins && running_ && !command_pending(); ++i) {
        const uint32_t freq = config.start_hz + static_cast<uint32_t>(i) * config.step_hz;
        if (!radio_->standby(err) || !radio_->set_frequency(freq, err) || !radio_->start_rx(0, err)) {
            set_error(err);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(800));
        float peak = -140.0f;
        for (int s = 0; s < 4; ++s) {
            float dbm;
            if (radio_->read_rssi(dbm, err)) {
                peak = std::max(peak, dbm);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        levels[i] = peak;
    }
    if (command_pending() || !running_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    spectrum_.start_hz = config.start_hz;
    spectrum_.step_hz = config.step_hz;
    spectrum_.dbm = std::move(levels);
    spectrum_.sweep_ms = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
    spectrum_fresh_ = true;
}

void RadioService::run_hunt_step(const Command& active) {
    if (active.targets.empty()) {
        set_error("no scan targets for this plan/profile");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return;
    }
    if (hunt_target_ >= active.targets.size()) {
        hunt_target_ = 0;
    }
    const auto& target = active.targets[hunt_target_];
    uint8_t sf = next_sf(target.sf_mask, hunt_sf_);
    if (sf == 0) {
        // Advance to next target.
        hunt_sf_ = 0;
        if (++hunt_target_ >= active.targets.size()) {
            hunt_target_ = 0;
            std::lock_guard<std::mutex> lock(mutex_);
            ++status_.hunt_cycles;
        }
        return;
    }
    hunt_sf_ = sf;

    decoder::LoraModem m = target.modem;
    m.spreading_factor = sf;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.target_label = target.label;
        status_.target_freq_hz = m.freq_hz;
        status_.target_sf = sf;
        status_.target_index = hunt_target_;
    }

    std::string err;
    if (!radio_->configure(m, err)) {
        set_error(err);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return;
    }

    const double tsym = symbol_ms(m);
    const auto max_packet = std::chrono::milliseconds(
        static_cast<int64_t>(decoder::time_on_air_ms(m, 255) + 100.0));

    if (active.hunt.dwell_ms > 0) {
        if (!radio_->start_rx(0, err)) {
            set_error(err);
            return;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(active.hunt.dwell_ms);
        while (Clock::now() < deadline && running_ && !command_pending()) {
            RxPacket rx;
            const auto ev = radio_->poll(rx, err);
            if (ev == RadioEvent::RxDone) {
                publish_packet(std::move(rx), m, target.label);
            }
            sleep_short();
        }
        return;
    }

    // CAD: on detection the radio itself switches to RX and waits up to
    // (preamble + header) symbols for a valid header.
    const auto rx_timeout = static_cast<uint32_t>((m.preamble + 12) * tsym + 5.0);
    if (!radio_->start_cad(rx_timeout, err)) {
        set_error(err);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return;
    }
    const auto cad_deadline =
        Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(4 * tsym + 50));
    while (running_ && Clock::now() < cad_deadline) {
        RxPacket rx;
        const auto ev = radio_->poll(rx, err);
        if (ev == RadioEvent::CadClear) {
            return;
        }
        if (ev == RadioEvent::CadBusy) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++status_.cad_hits;
            }
            wait_packet(m, target.label, max_packet + std::chrono::milliseconds(rx_timeout));
            return;
        }
        sleep_short();
    }
    radio_->standby(err);  // CAD never completed; reset state
}

bool RadioService::wait_packet(const decoder::LoraModem& modem, const std::string& source,
                               std::chrono::milliseconds max_wait) {
    std::string err;
    const auto deadline = Clock::now() + max_wait;
    while (running_ && Clock::now() < deadline) {
        RxPacket rx;
        const auto ev = radio_->poll(rx, err);
        if (ev == RadioEvent::RxDone) {
            publish_packet(std::move(rx), modem, source);
            return true;
        }
        if (ev == RadioEvent::Timeout || ev == RadioEvent::HeaderError) {
            return false;
        }
        sleep_short();
    }
    radio_->standby(err);
    return false;
}

void RadioService::run_listen_step(const decoder::LoraModem& modem, bool& configured) {
    std::string err;
    if (!configured) {
        if (!radio_->configure(modem, err) || !radio_->start_rx(0, err)) {
            set_error(err);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return;
        }
        configured = true;
        std::lock_guard<std::mutex> lock(mutex_);
        status_.target_label = "Listen";
        status_.target_freq_hz = modem.freq_hz;
        status_.target_sf = modem.spreading_factor;
    }
    const auto until = Clock::now() + std::chrono::milliseconds(100);
    while (Clock::now() < until && running_ && !command_pending()) {
        RxPacket rx;
        const auto ev = radio_->poll(rx, err);
        if (ev == RadioEvent::RxDone) {
            publish_packet(std::move(rx), modem, "Listen");
        }
        sleep_short();
    }
    float rssi;
    if (radio_->read_rssi(rssi, err)) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.rssi_dbm = rssi;
    }
}

void RadioService::publish_packet(RxPacket&& rx, const decoder::LoraModem& modem, const std::string& source) {
    std::vector<decoder::meshtastic::ChannelKey> keys;
    std::vector<decoder::meshcore::Channel> mc_channels;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        keys = keys_;
        mc_channels = meshcore_channels_;
    }
    PacketRecord rec;
    rec.time = std::chrono::system_clock::now();
    rec.modem = modem;
    rec.source = source;
    decoder::DecodeContext ctx;
    ctx.sync_word = modem.sync_word;
    ctx.iq_inverted = modem.iq_inverted;
    ctx.bandwidth_hz = modem.bandwidth_hz;
    ctx.meshtastic_keys = &keys;
    ctx.meshcore_channels = &mc_channels;
    rec.decoded = decoder::decode_packet(rx.data.data(), rx.data.size(), ctx);
    if (rx.crc_error) {
        rec.decoded.summary = "[CRC] " + rec.decoded.summary;
    }
    rec.rx = std::move(rx);

    std::lock_guard<std::mutex> lock(mutex_);
    rec.seq = next_seq_++;
    ++status_.packets;
    if (rec.rx.crc_error) {
        ++status_.crc_errors;
    }
    if (packets_.size() >= kMaxQueuedPackets) {
        packets_.erase(packets_.begin());
    }
    packets_.push_back(std::move(rec));
}

} // namespace platform::radio
