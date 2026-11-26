/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RAII wrapper around a Linux spidev node (/dev/spidevB.C).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace platform::radio {

struct SpiConfig {
    std::string path{"/dev/spidev0.0"};
    uint32_t speed_hz{2000000};
    uint8_t mode{0};
    uint8_t bits_per_word{8};
};

class SpiDevice {
public:
    static std::unique_ptr<SpiDevice> open(const SpiConfig& config, std::string& error);
    ~SpiDevice();

    SpiDevice(const SpiDevice&) = delete;
    SpiDevice& operator=(const SpiDevice&) = delete;

    // Full-duplex transfer; rx may be nullptr. len <= 4096.
    bool transfer(const uint8_t* tx, uint8_t* rx, std::size_t len, std::string& error);

private:
    SpiDevice(int fd, SpiConfig config) : fd_(fd), config_(std::move(config)) {}
    int fd_{-1};
    SpiConfig config_;
};

} // namespace platform::radio
