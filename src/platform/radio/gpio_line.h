/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Single GPIO line through the Linux GPIO character device (uAPI v2),
 * no libgpiod dependency. The line is released when the object dies.
 */

#pragma once

#include <memory>
#include <string>

namespace platform::radio {

class GpioLine {
public:
    enum class Direction { Input, Output };

    static std::unique_ptr<GpioLine> request(const std::string& chip_path,
                                             unsigned int offset,
                                             Direction direction,
                                             bool initial_value,
                                             const std::string& consumer,
                                             std::string& error);
    ~GpioLine();

    GpioLine(const GpioLine&) = delete;
    GpioLine& operator=(const GpioLine&) = delete;

    bool set(bool value, std::string& error);
    bool get(bool& value, std::string& error);

private:
    explicit GpioLine(int fd) : fd_(fd) {}
    int fd_{-1};
};

} // namespace platform::radio
