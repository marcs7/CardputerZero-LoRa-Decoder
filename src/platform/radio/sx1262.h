/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Semtech SX1262 receive driver over Linux spidev + GPIO character device.
 * Reference: SX1261/2 datasheet rev 2.1 (DS.SX1261-2.W.APP).
 */

#pragma once

#include "gpio_line.h"
#include "radio.h"
#include "spi_device.h"

#include <memory>
#include <string>

namespace platform::radio {

// Defaults match an M5Stack Cap LoRa-1262 on the CardputerZero EXT header:
// NSS=GPIO7 (SPI0 CE1), RST=GPIO26, DIO1=GPIO23, BUSY=GPIO22, antenna switch
// on PI4IOE5V6408 P0 @ 0x43 on I2C1 (GPIO2/3), TCXO 3.0 V, LDO regulator.
struct Sx1262Config {
    SpiConfig spi{"/dev/spidev0.1", 2000000, 0, 8};
    std::string gpio_chip{"/dev/gpiochip0"};
    int pin_reset{26};   // NRESET, active low
    int pin_busy{22};    // BUSY input; -1 => fixed delays instead
    int pin_rxen{-1};    // optional external LNA/RF switch enable (e.g. Ebyte E22)
    int pin_txen{-1};    // optional; held low (we never transmit)
    float tcxo_voltage{3.0f};  // 0 => crystal, otherwise DIO3 drives TCXO
    bool dio2_rf_switch{true};
    bool use_dcdc{false};
    bool rx_boosted_gain{true};
    // sysfs LED node that switches the 5 V rail feeding the cap; empty disables.
    // Turned on in begin() and restored to its previous value in end().
    std::string power_switch{"/sys/class/leds/ext_5v_out/brightness"};
    uint32_t power_on_delay_ms{300};
    // Antenna switch behind an I2C PI4IOE5V6408 expander; pin -1 disables.
    std::string antenna_switch_i2c{"/dev/i2c-1"};
    int antenna_switch_address{0x43};
    int antenna_switch_pin{0};
};

class Sx1262 final : public Radio {
public:
    explicit Sx1262(Sx1262Config config);
    ~Sx1262() override;

    bool begin(std::string& error) override;
    void end() override;
    const char* name() const override { return "SX1262"; }
    bool simulated() const override { return false; }

    bool configure(const decoder::LoraModem& modem, std::string& error) override;
    bool set_frequency(uint32_t freq_hz, std::string& error) override;
    bool start_rx(uint32_t timeout_ms, std::string& error) override;
    bool start_cad(uint32_t rx_timeout_ms, std::string& error) override;
    bool standby(std::string& error) override;
    RadioEvent poll(RxPacket& packet, std::string& error) override;
    bool read_rssi(float& dbm, std::string& error) override;

private:
    bool wait_busy(uint32_t timeout_ms, std::string& error);
    bool command(uint8_t opcode, const uint8_t* params, std::size_t len, std::string& error);
    bool read_command(uint8_t opcode, uint8_t* out, std::size_t len, std::string& error);
    bool write_register(uint16_t address, const uint8_t* data, std::size_t len, std::string& error);
    bool read_register(uint16_t address, uint8_t* data, std::size_t len, std::string& error);
    bool read_buffer(uint8_t offset, uint8_t* data, std::size_t len, std::string& error);
    bool clear_irq(uint16_t mask, std::string& error);
    bool get_irq(uint16_t& irq, std::string& error);
    bool calibrate_image(uint32_t freq_hz, std::string& error);
    bool apply_frequency(uint32_t freq_hz, std::string& error);

    Sx1262Config config_;
    std::unique_ptr<SpiDevice> spi_;
    std::unique_ptr<GpioLine> reset_;
    std::unique_ptr<GpioLine> busy_;
    std::unique_ptr<GpioLine> rxen_;
    std::unique_ptr<GpioLine> txen_;
    decoder::LoraModem modem_{};
    int calibrated_band_{-1};
    bool cad_pending_{false};
    std::string power_saved_;  // previous power_switch value, restored on end()
};

} // namespace platform::radio
