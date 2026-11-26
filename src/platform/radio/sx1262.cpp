/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sx1262.h"

#include "io_expander.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

namespace platform::radio {
namespace {

// Opcodes (datasheet section 13)
constexpr uint8_t kSetStandby = 0x80;
constexpr uint8_t kSetRx = 0x82;
constexpr uint8_t kSetCad = 0xC5;
constexpr uint8_t kSetRegulatorMode = 0x96;
constexpr uint8_t kCalibrate = 0x89;
constexpr uint8_t kCalibrateImage = 0x98;
constexpr uint8_t kSetDio3AsTcxo = 0x97;
constexpr uint8_t kSetDio2AsRfSwitch = 0x9D;
constexpr uint8_t kSetPacketType = 0x8A;
constexpr uint8_t kSetRfFrequency = 0x86;
constexpr uint8_t kSetModulationParams = 0x8B;
constexpr uint8_t kSetPacketParams = 0x8C;
constexpr uint8_t kSetCadParams = 0x88;
constexpr uint8_t kSetBufferBaseAddress = 0x8F;
constexpr uint8_t kSetDioIrqParams = 0x08;
constexpr uint8_t kGetIrqStatus = 0x12;
constexpr uint8_t kClearIrqStatus = 0x02;
constexpr uint8_t kGetRxBufferStatus = 0x13;
constexpr uint8_t kGetPacketStatus = 0x14;
constexpr uint8_t kGetRssiInst = 0x15;
constexpr uint8_t kClearDeviceErrors = 0x07;
constexpr uint8_t kStopTimerOnPreamble = 0x9F;
constexpr uint8_t kWriteRegister = 0x0D;
constexpr uint8_t kReadRegister = 0x1D;
constexpr uint8_t kReadBuffer = 0x1E;

// Registers
constexpr uint16_t kRegSyncWordMsb = 0x0740;
constexpr uint16_t kRegIqPolarity = 0x0736;
constexpr uint16_t kRegRxGain = 0x08AC;

// IRQ bits
constexpr uint16_t kIrqRxDone = 1U << 1;
constexpr uint16_t kIrqHeaderErr = 1U << 5;
constexpr uint16_t kIrqCrcErr = 1U << 6;
constexpr uint16_t kIrqCadDone = 1U << 7;
constexpr uint16_t kIrqCadDetected = 1U << 8;
constexpr uint16_t kIrqTimeout = 1U << 9;
constexpr uint16_t kIrqAll = 0x03FF;

void sleep_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

uint8_t bandwidth_code(uint32_t bw_hz) {
    struct Entry { uint32_t hz; uint8_t code; };
    static constexpr Entry kTable[] = {
        {7810, 0x00}, {10420, 0x08}, {15630, 0x01}, {20830, 0x09}, {31250, 0x02},
        {41670, 0x0A}, {62500, 0x03}, {125000, 0x04}, {250000, 0x05}, {500000, 0x06},
    };
    for (const auto& e : kTable) {
        if (bw_hz <= e.hz + 100) {
            return e.code;
        }
    }
    return 0x06;
}

uint8_t tcxo_code(float volts) {
    static constexpr float kLevels[] = {1.6f, 1.7f, 1.8f, 2.2f, 2.4f, 2.7f, 3.0f, 3.3f};
    uint8_t best = 2;
    float best_diff = 10.0f;
    for (uint8_t i = 0; i < 8; ++i) {
        const float diff = std::fabs(kLevels[i] - volts);
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }
    return best;
}

// Semtech AN1200.48 recommended CAD thresholds for 2-symbol CAD.
uint8_t cad_det_peak(uint8_t sf) {
    switch (sf) {
        case 5: case 6: case 7: case 8: return 22;
        case 9: return 24;
        case 10: return 25;
        case 11: return 26;
        default: return 30;
    }
}

uint32_t to_timer_steps(uint32_t ms) {
    const uint64_t steps = static_cast<uint64_t>(ms) * 64U;  // 15.625 us units
    return steps > 0xFFFFFEU ? 0xFFFFFEU : static_cast<uint32_t>(steps);
}

} // namespace

Sx1262::Sx1262(Sx1262Config config) : config_(std::move(config)) {}

Sx1262::~Sx1262() {
    end();
}

bool Sx1262::begin(std::string& error) {
    if (!config_.power_switch.empty()) {
        std::ifstream in(config_.power_switch);
        std::string current;
        if (!(in >> current)) {
            error = "cannot read cap power switch " + config_.power_switch;
            return false;
        }
        if (current == "0") {
            std::ofstream out(config_.power_switch);
            if (!(out << "1" << std::flush)) {
                error = "cannot enable cap 5V power (" + config_.power_switch + ", need gpio group)";
                return false;
            }
            power_saved_ = current;
            sleep_ms(config_.power_on_delay_ms);
        }
    }
    if (config_.antenna_switch_pin >= 0 &&
        !pi4ioe5v6408_set_output(config_.antenna_switch_i2c,
                                 static_cast<uint8_t>(config_.antenna_switch_address),
                                 static_cast<uint8_t>(config_.antenna_switch_pin), true, error)) {
        end();  // restores cap power
        return false;
    }
    spi_ = SpiDevice::open(config_.spi, error);
    if (!spi_) {
        end();  // restores cap power
        return false;
    }

    const auto request = [&](int pin, GpioLine::Direction dir, bool initial,
                             std::unique_ptr<GpioLine>& out, const char* what) {
        if (pin < 0) {
            return true;
        }
        std::string gpio_error;
        out = GpioLine::request(config_.gpio_chip, static_cast<unsigned>(pin), dir, initial,
                                "lora-decoder", gpio_error);
        if (!out) {
            error = std::string(what) + ": " + gpio_error;
            return false;
        }
        return true;
    };
    if (!request(config_.pin_reset, GpioLine::Direction::Output, true, reset_, "reset pin") ||
        !request(config_.pin_busy, GpioLine::Direction::Input, false, busy_, "busy pin") ||
        !request(config_.pin_rxen, GpioLine::Direction::Output, true, rxen_, "rxen pin") ||
        !request(config_.pin_txen, GpioLine::Direction::Output, false, txen_, "txen pin")) {
        end();
        return false;
    }

    if (reset_) {
        std::string ignored;
        reset_->set(false, ignored);
        sleep_ms(2);
        reset_->set(true, ignored);
        sleep_ms(10);
    }
    if (!wait_busy(100, error)) {
        error = "SX1262 stays busy after reset (" + error + ")";
        end();
        return false;
    }

    uint8_t param = 0x00;  // STDBY_RC
    if (!command(kSetStandby, &param, 1, error)) {
        end();
        return false;
    }

    // Presence check: after reset the LoRa sync word register holds 0x1424.
    uint8_t sync[2] = {};
    if (!read_register(kRegSyncWordMsb, sync, 2, error)) {
        end();
        return false;
    }
    if (!((sync[0] == 0x14 && sync[1] == 0x24) || (sync[0] == 0x34 && sync[1] == 0x44))) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "SX1262 not detected on %s (sync reg %02X%02X, expected 1424)",
                      config_.spi.path.c_str(), sync[0], sync[1]);
        error = buf;
        end();
        return false;
    }

    if (config_.tcxo_voltage > 0.0f) {
        const uint32_t delay = to_timer_steps(5);
        const uint8_t tcxo[4] = {tcxo_code(config_.tcxo_voltage),
                                 static_cast<uint8_t>(delay >> 16),
                                 static_cast<uint8_t>(delay >> 8),
                                 static_cast<uint8_t>(delay)};
        if (!command(kSetDio3AsTcxo, tcxo, sizeof(tcxo), error)) {
            end();
            return false;
        }
    }
    param = 0x7F;  // calibrate all blocks
    if (!command(kCalibrate, &param, 1, error) || !wait_busy(50, error)) {
        end();
        return false;
    }
    const uint8_t clear_errors[2] = {0, 0};
    command(kClearDeviceErrors, clear_errors, 2, error);

    param = config_.use_dcdc ? 0x01 : 0x00;
    if (!command(kSetRegulatorMode, &param, 1, error)) {
        end();
        return false;
    }
    if (config_.dio2_rf_switch) {
        param = 0x01;
        if (!command(kSetDio2AsRfSwitch, &param, 1, error)) {
            end();
            return false;
        }
    }
    param = 0x01;  // LoRa
    if (!command(kSetPacketType, &param, 1, error)) {
        end();
        return false;
    }
    const uint8_t base[2] = {0x00, 0x00};
    if (!command(kSetBufferBaseAddress, base, 2, error)) {
        end();
        return false;
    }
    // Route every IRQ to the status register; DIO1 mirrors them for whoever wires it.
    const uint8_t irq[8] = {0x03, 0xFF, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x00};
    if (!command(kSetDioIrqParams, irq, sizeof(irq), error)) {
        end();
        return false;
    }
    // Keep the RX timeout running until the header, not just the preamble.
    param = 0x00;
    command(kStopTimerOnPreamble, &param, 1, error);

    if (config_.rx_boosted_gain) {
        const uint8_t gain = 0x96;
        write_register(kRegRxGain, &gain, 1, error);
    }
    error.clear();
    return true;
}

void Sx1262::end() {
    if (spi_) {
        std::string ignored;
        uint8_t param = 0x00;
        command(kSetStandby, &param, 1, ignored);
    }
    txen_.reset();
    rxen_.reset();
    busy_.reset();
    reset_.reset();
    spi_.reset();
    calibrated_band_ = -1;
    if (!power_saved_.empty()) {
        std::ofstream out(config_.power_switch);
        out << power_saved_ << std::flush;
        power_saved_.clear();
    }
}

bool Sx1262::wait_busy(uint32_t timeout_ms, std::string& error) {
    if (!busy_) {
        // No BUSY line: worst-case delays (calibration takes up to ~3.5 ms per block).
        sleep_ms(timeout_ms >= 50 ? 12 : 1);
        return true;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool busy = true;
    while (true) {
        if (!busy_->get(busy, error)) {
            return false;
        }
        if (!busy) {
            return true;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            error = "BUSY timeout";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

bool Sx1262::command(uint8_t opcode, const uint8_t* params, std::size_t len, std::string& error) {
    if (!spi_) {
        error = "radio not open";
        return false;
    }
    if (!wait_busy(20, error)) {
        return false;
    }
    uint8_t tx[32];
    if (len + 1 > sizeof(tx)) {
        error = "command too long";
        return false;
    }
    tx[0] = opcode;
    if (len) {
        std::memcpy(tx + 1, params, len);
    }
    return spi_->transfer(tx, nullptr, len + 1, error);
}

bool Sx1262::read_command(uint8_t opcode, uint8_t* out, std::size_t len, std::string& error) {
    if (!spi_) {
        error = "radio not open";
        return false;
    }
    if (!wait_busy(20, error)) {
        return false;
    }
    uint8_t tx[16] = {};
    uint8_t rx[16] = {};
    if (len + 2 > sizeof(tx)) {
        error = "read too long";
        return false;
    }
    tx[0] = opcode;
    if (!spi_->transfer(tx, rx, len + 2, error)) {
        return false;
    }
    std::memcpy(out, rx + 2, len);
    return true;
}

bool Sx1262::write_register(uint16_t address, const uint8_t* data, std::size_t len, std::string& error) {
    uint8_t params[16];
    if (len + 2 > sizeof(params)) {
        error = "register write too long";
        return false;
    }
    params[0] = static_cast<uint8_t>(address >> 8);
    params[1] = static_cast<uint8_t>(address);
    std::memcpy(params + 2, data, len);
    return command(kWriteRegister, params, len + 2, error);
}

bool Sx1262::read_register(uint16_t address, uint8_t* data, std::size_t len, std::string& error) {
    if (!spi_) {
        error = "radio not open";
        return false;
    }
    if (!wait_busy(20, error)) {
        return false;
    }
    uint8_t tx[20] = {};
    uint8_t rx[20] = {};
    if (len + 4 > sizeof(tx)) {
        error = "register read too long";
        return false;
    }
    tx[0] = kReadRegister;
    tx[1] = static_cast<uint8_t>(address >> 8);
    tx[2] = static_cast<uint8_t>(address);
    if (!spi_->transfer(tx, rx, len + 4, error)) {
        return false;
    }
    std::memcpy(data, rx + 4, len);
    return true;
}

bool Sx1262::read_buffer(uint8_t offset, uint8_t* data, std::size_t len, std::string& error) {
    if (!wait_busy(20, error)) {
        return false;
    }
    uint8_t tx[260] = {};
    uint8_t rx[260] = {};
    tx[0] = kReadBuffer;
    tx[1] = offset;
    if (!spi_->transfer(tx, rx, len + 3, error)) {
        return false;
    }
    std::memcpy(data, rx + 3, len);
    return true;
}

bool Sx1262::clear_irq(uint16_t mask, std::string& error) {
    const uint8_t params[2] = {static_cast<uint8_t>(mask >> 8), static_cast<uint8_t>(mask)};
    return command(kClearIrqStatus, params, 2, error);
}

bool Sx1262::get_irq(uint16_t& irq, std::string& error) {
    uint8_t out[2];
    if (!read_command(kGetIrqStatus, out, 2, error)) {
        return false;
    }
    irq = static_cast<uint16_t>((out[0] << 8) | out[1]);
    return true;
}

bool Sx1262::calibrate_image(uint32_t freq_hz, std::string& error) {
    struct Band { uint32_t lo; uint32_t hi; uint8_t f1; uint8_t f2; };
    static constexpr Band kBands[] = {
        {430000000, 440000000, 0x6B, 0x6F},
        {470000000, 510000000, 0x75, 0x81},
        {779000000, 787000000, 0xC1, 0xC5},
        {863000000, 870000000, 0xD7, 0xDB},
        {902000000, 928000000, 0xE1, 0xE9},
    };
    int band = -1;
    uint8_t f1 = 0, f2 = 0;
    for (int i = 0; i < 5; ++i) {
        if (freq_hz >= kBands[i].lo && freq_hz <= kBands[i].hi) {
            band = i;
            f1 = kBands[i].f1;
            f2 = kBands[i].f2;
        }
    }
    if (band < 0) {
        // Generic 4 MHz-step window around the frequency.
        band = 100 + static_cast<int>(freq_hz / 4000000U);
        f1 = static_cast<uint8_t>((freq_hz - 4000000U) / 4000000U);
        f2 = static_cast<uint8_t>((freq_hz + 4000000U) / 4000000U);
    }
    if (band == calibrated_band_) {
        return true;
    }
    const uint8_t params[2] = {f1, f2};
    if (!command(kCalibrateImage, params, 2, error) || !wait_busy(50, error)) {
        return false;
    }
    calibrated_band_ = band;
    return true;
}

bool Sx1262::apply_frequency(uint32_t freq_hz, std::string& error) {
    const uint32_t rf = static_cast<uint32_t>((static_cast<uint64_t>(freq_hz) << 25) / 32000000ULL);
    const uint8_t params[4] = {static_cast<uint8_t>(rf >> 24), static_cast<uint8_t>(rf >> 16),
                               static_cast<uint8_t>(rf >> 8), static_cast<uint8_t>(rf)};
    return command(kSetRfFrequency, params, 4, error);
}

bool Sx1262::configure(const decoder::LoraModem& modem, std::string& error) {
    if (!standby(error) || !calibrate_image(modem.freq_hz, error) ||
        !apply_frequency(modem.freq_hz, error)) {
        return false;
    }
    const double symbol_ms = std::ldexp(1.0, modem.spreading_factor) / modem.bandwidth_hz * 1000.0;
    const uint8_t ldro = symbol_ms >= 16.38 ? 1 : 0;
    const uint8_t cr = static_cast<uint8_t>(modem.coding_rate >= 5 && modem.coding_rate <= 8
                                                ? modem.coding_rate - 4 : 1);
    const uint8_t mod[4] = {modem.spreading_factor, bandwidth_code(modem.bandwidth_hz), cr, ldro};
    if (!command(kSetModulationParams, mod, 4, error)) {
        return false;
    }
    const uint8_t pkt[6] = {static_cast<uint8_t>(modem.preamble >> 8),
                            static_cast<uint8_t>(modem.preamble),
                            0x00,  // explicit header
                            0xFF,  // max payload
                            static_cast<uint8_t>(modem.crc_on ? 1 : 0),
                            static_cast<uint8_t>(modem.iq_inverted ? 1 : 0)};
    if (!command(kSetPacketParams, pkt, 6, error)) {
        return false;
    }
    // Datasheet 15.4: IQ polarity workaround.
    uint8_t iq = 0;
    if (!read_register(kRegIqPolarity, &iq, 1, error)) {
        return false;
    }
    iq = modem.iq_inverted ? static_cast<uint8_t>(iq & ~0x04) : static_cast<uint8_t>(iq | 0x04);
    if (!write_register(kRegIqPolarity, &iq, 1, error)) {
        return false;
    }
    const uint8_t sw = modem.sync_word;
    const uint8_t sync[2] = {static_cast<uint8_t>((sw & 0xF0) | 0x04),
                             static_cast<uint8_t>(((sw & 0x0F) << 4) | 0x04)};
    if (!write_register(kRegSyncWordMsb, sync, 2, error)) {
        return false;
    }
    modem_ = modem;
    return true;
}

bool Sx1262::set_frequency(uint32_t freq_hz, std::string& error) {
    if (!calibrate_image(freq_hz, error)) {
        return false;
    }
    modem_.freq_hz = freq_hz;
    return apply_frequency(freq_hz, error);
}

bool Sx1262::start_rx(uint32_t timeout_ms, std::string& error) {
    if (!clear_irq(kIrqAll, error)) {
        return false;
    }
    const uint32_t steps = timeout_ms == 0 ? 0xFFFFFFU : to_timer_steps(timeout_ms);
    const uint8_t params[3] = {static_cast<uint8_t>(steps >> 16), static_cast<uint8_t>(steps >> 8),
                               static_cast<uint8_t>(steps)};
    cad_pending_ = false;
    return command(kSetRx, params, 3, error);
}

bool Sx1262::start_cad(uint32_t rx_timeout_ms, std::string& error) {
    const uint32_t steps = to_timer_steps(rx_timeout_ms);
    const uint8_t params[7] = {
        0x01,  // 2 symbols
        cad_det_peak(modem_.spreading_factor),
        10,    // det min
        0x01,  // CAD_RX: enter RX on detection
        static_cast<uint8_t>(steps >> 16), static_cast<uint8_t>(steps >> 8),
        static_cast<uint8_t>(steps)};
    if (!command(kSetCadParams, params, sizeof(params), error) || !clear_irq(kIrqAll, error)) {
        return false;
    }
    cad_pending_ = true;
    return command(kSetCad, nullptr, 0, error);
}

bool Sx1262::standby(std::string& error) {
    const uint8_t param = 0x00;
    cad_pending_ = false;
    return command(kSetStandby, &param, 1, error);
}

RadioEvent Sx1262::poll(RxPacket& packet, std::string& error) {
    uint16_t irq = 0;
    if (!get_irq(irq, error)) {
        return RadioEvent::None;
    }
    if (irq == 0) {
        return RadioEvent::None;
    }

    if (cad_pending_ && (irq & kIrqCadDone)) {
        cad_pending_ = false;
        clear_irq(kIrqCadDone | kIrqCadDetected, error);
        return (irq & kIrqCadDetected) ? RadioEvent::CadBusy : RadioEvent::CadClear;
    }

    if (irq & kIrqRxDone) {
        uint8_t status[2] = {};
        uint8_t pkt_status[3] = {};
        if (!read_command(kGetRxBufferStatus, status, 2, error) ||
            !read_command(kGetPacketStatus, pkt_status, 3, error)) {
            clear_irq(kIrqAll, error);
            return RadioEvent::None;
        }
        packet.data.resize(status[0]);
        if (status[0] && !read_buffer(status[1], packet.data.data(), status[0], error)) {
            clear_irq(kIrqAll, error);
            return RadioEvent::None;
        }
        packet.rssi_dbm = -static_cast<float>(pkt_status[0]) / 2.0f;
        packet.snr_db = static_cast<float>(static_cast<int8_t>(pkt_status[1])) / 4.0f;
        packet.signal_rssi_dbm = -static_cast<float>(pkt_status[2]) / 2.0f;
        packet.crc_error = (irq & kIrqCrcErr) != 0;
        clear_irq(kIrqAll, error);
        return RadioEvent::RxDone;
    }
    if (irq & kIrqHeaderErr) {
        clear_irq(kIrqAll, error);
        return RadioEvent::HeaderError;
    }
    if (irq & kIrqTimeout) {
        clear_irq(kIrqAll, error);
        return RadioEvent::Timeout;
    }
    // Preamble / sync / header-valid notifications: acknowledge and keep waiting.
    clear_irq(static_cast<uint16_t>(irq & ~(kIrqRxDone | kIrqCadDone | kIrqCadDetected)), error);
    return RadioEvent::None;
}

bool Sx1262::read_rssi(float& dbm, std::string& error) {
    uint8_t value = 0;
    if (!read_command(kGetRssiInst, &value, 1, error)) {
        return false;
    }
    dbm = -static_cast<float>(value) / 2.0f;
    return true;
}

} // namespace platform::radio
