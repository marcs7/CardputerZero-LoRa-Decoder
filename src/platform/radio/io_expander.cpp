/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "io_expander.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::radio {
namespace {

// PI4IOE5V6408 register map (datasheet rev 1)
constexpr uint8_t kRegDeviceId = 0x01;      // read only to check the chip answers
constexpr uint8_t kRegDirection = 0x03;     // 1 = output
constexpr uint8_t kRegOutputState = 0x05;
constexpr uint8_t kRegOutputHighZ = 0x07;   // 1 = high impedance

#if defined(__linux__)
bool read_reg(int fd, uint8_t reg, uint8_t& value) {
    return ::write(fd, &reg, 1) == 1 && ::read(fd, &value, 1) == 1;
}

bool write_reg(int fd, uint8_t reg, uint8_t value) {
    const uint8_t buf[2] = {reg, value};
    return ::write(fd, buf, 2) == 2;
}
#endif

} // namespace

bool pi4ioe5v6408_set_output(const std::string& i2c_bus, uint8_t address, uint8_t pin, bool high,
                             std::string& error) {
#if defined(__linux__)
    if (pin > 7) {
        error = "PI4IOE5V6408 pin out of range";
        return false;
    }
    const int fd = ::open(i2c_bus.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = "open " + i2c_bus + ": " + std::strerror(errno);
        return false;
    }
    char where[48];
    std::snprintf(where, sizeof(where), "%s@0x%02X", i2c_bus.c_str(), address);
    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        error = std::string("antenna switch expander ") + where + ": " + std::strerror(errno) +
                (errno == EBUSY ? " (claimed by a kernel driver)" : "");
        ::close(fd);
        return false;
    }
    uint8_t id = 0;
    if (!read_reg(fd, kRegDeviceId, id)) {
        error = std::string("antenna switch expander not responding at ") + where +
                " (is the Cap LoRa-1262 attached?)";
        ::close(fd);
        return false;
    }
    const uint8_t mask = static_cast<uint8_t>(1U << pin);
    uint8_t out = 0, dir = 0, hiz = 0;
    bool ok = read_reg(fd, kRegOutputState, out) && read_reg(fd, kRegDirection, dir) &&
              read_reg(fd, kRegOutputHighZ, hiz);
    // Set the level first so the pin doesn't glitch when it becomes an output.
    ok = ok && write_reg(fd, kRegOutputState, high ? (out | mask) : (out & ~mask));
    ok = ok && write_reg(fd, kRegDirection, dir | mask);
    ok = ok && write_reg(fd, kRegOutputHighZ, hiz & ~mask);
    ::close(fd);
    if (!ok) {
        error = std::string("I2C write to antenna switch expander ") + where + " failed";
        return false;
    }
    return true;
#else
    (void)i2c_bus; (void)address; (void)pin; (void)high;
    error = "i2c-dev is only available on Linux";
    return false;
#endif
}

} // namespace platform::radio
