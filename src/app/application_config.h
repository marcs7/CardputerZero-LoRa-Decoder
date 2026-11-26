/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lora_settings.h"
#include "meshtastic.h"
#include "radio/sx1262.h"

#include <string>
#include <vector>

namespace app {

struct ApplicationConfig {
    bool dark_mode = true;
    model::LoraSettings lora;
    // [radio]
    std::string radio_backend = "auto";  // auto | sx1262 | sim
    platform::radio::Sx1262Config sx1262;
    // [meshtastic]  name = base64 PSK (in addition to the default key)
    std::vector<decoder::meshtastic::ChannelKey> meshtastic_channels;
};

// Unknown keys are ignored; malformed values are reported but parsing goes on
// (the offending value keeps its default) so one typo can't brick the app.
bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);

// Rewrites only [application] and [lora]; other sections are preserved verbatim.
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error);

} // namespace app
