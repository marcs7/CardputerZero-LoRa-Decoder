/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PI4IOE5V6408 8-bit I2C I/O expander (used on M5Stack Cap LoRa-1262 to
 * enable the RF antenna switch on P0). Plain i2c-dev, no kernel driver needed.
 */

#pragma once

#include <cstdint>
#include <string>

namespace platform::radio {

// Configures `pin` as a push-pull output and drives it. Other pins keep their state.
bool pi4ioe5v6408_set_output(const std::string& i2c_bus, uint8_t address, uint8_t pin, bool high,
                             std::string& error);

} // namespace platform::radio
