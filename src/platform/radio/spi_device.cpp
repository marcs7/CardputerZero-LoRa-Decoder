/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "spi_device.h"

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::radio {

std::unique_ptr<SpiDevice> SpiDevice::open(const SpiConfig& config, std::string& error) {
#if defined(__linux__)
    const int fd = ::open(config.path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = "open " + config.path + ": " + std::strerror(errno) +
                (errno == ENOENT ? " (enable SPI / spidev overlay)" : "");
        return nullptr;
    }
    uint8_t mode = config.mode;
    uint8_t bits = config.bits_per_word;
    uint32_t speed = config.speed_hz;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        error = "configure " + config.path + ": " + std::strerror(errno);
        ::close(fd);
        return nullptr;
    }
    return std::unique_ptr<SpiDevice>(new SpiDevice(fd, config));
#else
    (void)config;
    error = "spidev is only available on Linux";
    return nullptr;
#endif
}

SpiDevice::~SpiDevice() {
#if defined(__linux__)
    if (fd_ >= 0) {
        ::close(fd_);
    }
#endif
}

bool SpiDevice::transfer(const uint8_t* tx, uint8_t* rx, std::size_t len, std::string& error) {
#if defined(__linux__)
    if (len == 0 || len > 4096) {
        error = "invalid SPI transfer length";
        return false;
    }
    spi_ioc_transfer xfer{};
    xfer.tx_buf = reinterpret_cast<uintptr_t>(tx);
    xfer.rx_buf = reinterpret_cast<uintptr_t>(rx);
    xfer.len = static_cast<uint32_t>(len);
    xfer.speed_hz = config_.speed_hz;
    xfer.bits_per_word = config_.bits_per_word;
    if (ioctl(fd_, SPI_IOC_MESSAGE(1), &xfer) < 0) {
        error = std::string("SPI transfer: ") + std::strerror(errno);
        return false;
    }
    return true;
#else
    (void)tx; (void)rx; (void)len;
    error = "spidev is only available on Linux";
    return false;
#endif
}

} // namespace platform::radio
