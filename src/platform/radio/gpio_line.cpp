/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "gpio_line.h"

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace platform::radio {

std::unique_ptr<GpioLine> GpioLine::request(const std::string& chip_path,
                                            unsigned int offset,
                                            Direction direction,
                                            bool initial_value,
                                            const std::string& consumer,
                                            std::string& error) {
#if defined(__linux__) && defined(GPIO_V2_GET_LINE_IOCTL)
    const int chip_fd = ::open(chip_path.c_str(), O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        error = "open " + chip_path + ": " + std::strerror(errno);
        return nullptr;
    }
    gpio_v2_line_request req{};
    req.offsets[0] = offset;
    req.num_lines = 1;
    std::strncpy(req.consumer, consumer.c_str(), sizeof(req.consumer) - 1);
    if (direction == Direction::Output) {
        req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
        req.config.num_attrs = 1;
        req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        req.config.attrs[0].attr.values = initial_value ? 1 : 0;
        req.config.attrs[0].mask = 1;
    } else {
        req.config.flags = GPIO_V2_LINE_FLAG_INPUT;
    }
    const int rc = ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req);
    const int saved_errno = errno;
    ::close(chip_fd);
    if (rc < 0) {
        error = chip_path + " line " + std::to_string(offset) + ": " + std::strerror(saved_errno) +
                (saved_errno == EBUSY ? " (in use - stop meshtasticd or other LoRa apps)" : "");
        return nullptr;
    }
    return std::unique_ptr<GpioLine>(new GpioLine(req.fd));
#else
    (void)chip_path; (void)offset; (void)direction; (void)initial_value; (void)consumer;
    error = "GPIO character device v2 not available on this platform";
    return nullptr;
#endif
}

GpioLine::~GpioLine() {
#if defined(__linux__)
    if (fd_ >= 0) {
        ::close(fd_);
    }
#endif
}

bool GpioLine::set(bool value, std::string& error) {
#if defined(__linux__) && defined(GPIO_V2_LINE_SET_VALUES_IOCTL)
    gpio_v2_line_values values{};
    values.bits = value ? 1 : 0;
    values.mask = 1;
    if (ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        error = std::string("GPIO set: ") + std::strerror(errno);
        return false;
    }
    return true;
#else
    (void)value;
    error = "GPIO unavailable";
    return false;
#endif
}

bool GpioLine::get(bool& value, std::string& error) {
#if defined(__linux__) && defined(GPIO_V2_LINE_GET_VALUES_IOCTL)
    gpio_v2_line_values values{};
    values.mask = 1;
    if (ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) {
        error = std::string("GPIO get: ") + std::strerror(errno);
        return false;
    }
    value = (values.bits & 1) != 0;
    return true;
#else
    (void)value;
    error = "GPIO unavailable";
    return false;
#endif
}

} // namespace platform::radio
