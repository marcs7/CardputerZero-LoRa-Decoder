/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace app {
namespace {

std::string trim(std::string value) {
    const auto is_not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string strip_comment(const std::string& value) {
    const auto pos = value.find_first_of(";#");
    return trim(pos == std::string::npos ? value : value.substr(0, pos));
}

bool parse_bool(const std::string& value, bool& result) {
    const auto v = lowercase(value);
    if (v == "yes" || v == "true" || v == "1" || v == "on") { result = true; return true; }
    if (v == "no" || v == "false" || v == "0" || v == "off") { result = false; return true; }
    return false;
}

bool parse_long(const std::string& value, long long& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    out = std::strtoll(value.c_str(), &end, 0);  // accepts 0x.. too
    return end && *end == '\0';
}

bool parse_float(const std::string& value, float& out) {
    if (value.empty()) return false;
    char* end = nullptr;
    out = std::strtof(value.c_str(), &end);
    return end && *end == '\0';
}

template <typename T>
bool parse_uint(const std::string& value, T& out, long long lo, long long hi) {
    long long v;
    if (!parse_long(value, v) || v < lo || v > hi) return false;
    out = static_cast<T>(v);
    return true;
}

bool parse_int(const std::string& value, int& out) {
    long long v;
    if (!parse_long(value, v) || v < -1 || v > 100000) return false;
    out = static_cast<int>(v);
    return true;
}

bool apply_key(ApplicationConfig& c, const std::string& section, const std::string& key,
               const std::string& raw_value) {
    const std::string value = (section == "meshtastic" || section == "meshcore") ? trim(raw_value)
                                                                                 : strip_comment(raw_value);
    auto& l = c.lora;
    auto& r = c.sx1262;
    if (section == "application") {
        if (key == "dark_mode") return parse_bool(value, c.dark_mode);
        return true;
    }
    if (section == "lora") {
        if (key == "band") { if (!decoder::find_band_plan(value)) return false; l.band = value; return true; }
        if (key == "profile") return model::parse_profile(lowercase(value), l.profile);
        if (key == "hunt_dwell_ms") return parse_uint(value, l.hunt_dwell_ms, 0, 60000);
        if (key == "spectrum_step_hz") return parse_uint(value, l.spectrum_step_hz, 1000, 5000000);
        if (key == "listen_step_hz") return parse_uint(value, l.listen_step_hz, 100, 10000000);
        if (key == "listen_freq_hz") return parse_uint(value, l.listen.freq_hz, 150000000, 960000000);
        if (key == "listen_bw_hz") return parse_uint(value, l.listen.bandwidth_hz, 7800, 500000);
        if (key == "listen_sf") return parse_uint(value, l.listen.spreading_factor, 5, 12);
        if (key == "listen_cr") return parse_uint(value, l.listen.coding_rate, 5, 8);
        if (key == "listen_sync") return parse_uint(value, l.listen.sync_word, 0, 255);
        if (key == "listen_preamble") return parse_uint(value, l.listen.preamble, 6, 65535);
        if (key == "listen_iq_inverted") return parse_bool(value, l.listen.iq_inverted);
        if (key == "listen_crc") return parse_bool(value, l.listen.crc_on);
        if (key == "capture") return parse_bool(value, l.capture);
        if (key == "waterfall") return parse_bool(value, l.waterfall);
        return true;
    }
    if (section == "radio") {
        if (key == "backend") { c.radio_backend = lowercase(value); return true; }
        if (key == "spi_device") { r.spi.path = value; return true; }
        if (key == "spi_speed_hz") return parse_uint(value, r.spi.speed_hz, 100000, 16000000);
        if (key == "gpio_chip") { r.gpio_chip = value; return true; }
        if (key == "pin_reset") return parse_int(value, r.pin_reset);
        if (key == "pin_busy") return parse_int(value, r.pin_busy);
        if (key == "pin_rxen") return parse_int(value, r.pin_rxen);
        if (key == "pin_txen") return parse_int(value, r.pin_txen);
        if (key == "tcxo_voltage") return parse_float(value, r.tcxo_voltage);
        if (key == "dio2_rf_switch") return parse_bool(value, r.dio2_rf_switch);
        if (key == "use_dcdc") return parse_bool(value, r.use_dcdc);
        if (key == "rx_boosted_gain") return parse_bool(value, r.rx_boosted_gain);
        if (key == "power_switch") { r.power_switch = value == "none" ? "" : value; return true; }
        if (key == "power_on_delay_ms") return parse_uint(value, r.power_on_delay_ms, 0, 5000);
        if (key == "antenna_switch_i2c") { r.antenna_switch_i2c = value; return true; }
        if (key == "antenna_switch_address") return parse_uint(value, r.antenna_switch_address, 0x03, 0x77);
        if (key == "antenna_switch_pin") return parse_int(value, r.antenna_switch_pin) && r.antenna_switch_pin <= 7;
        return true;
    }
    if (section == "meshcore") {
        decoder::meshcore::Channel ch;
        ch.name = key;
        if (!decoder::meshcore::parse_secret(value, ch.secret)) return false;
        c.meshcore_channels.push_back(ch);
        return true;
    }
    if (section == "meshtastic") {
        std::vector<uint8_t> psk;
        if (!decoder::base64_decode(value, psk) || psk.empty() || psk.size() > 32) return false;
        c.meshtastic_channels.push_back({key, decoder::meshtastic::expand_psk(psk)});
        return true;
    }
    return true;
}

} // namespace

bool load_application_config(const std::string& path, ApplicationConfig& config, std::string& error) {
    error.clear();
    std::ifstream input(path);
    if (!input) {
        error = "could not open file";
        return false;
    }
    std::string section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lowercase(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        // Meshtastic channel names are case sensitive.
        const auto raw_key = trim(line.substr(0, separator));
        const auto key = (section == "meshtastic" || section == "meshcore") ? raw_key : lowercase(raw_key);
        if (!apply_key(config, section, key, line.substr(separator + 1))) {
            if (!error.empty()) error += "; ";
            error += "bad " + key + " at line " + std::to_string(line_number);
        }
    }
    return true;
}

bool save_application_config(const std::string& path, const ApplicationConfig& config, std::string& error) {
    error.clear();
    const std::filesystem::path config_path(path);
    if (const auto parent = config_path.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "could not create parent directory: " + ec.message();
            return false;
        }
    }

    const auto& l = config.lora;
    char sync[8];
    std::snprintf(sync, sizeof(sync), "0x%02X", l.listen.sync_word);
    const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> managed = {
        {"application", {{"dark_mode", config.dark_mode ? "yes" : "no"}}},
        {"lora",
         {{"band", l.band},
          {"profile", model::profile_key(l.profile)},
          {"hunt_dwell_ms", std::to_string(l.hunt_dwell_ms)},
          {"spectrum_step_hz", std::to_string(l.spectrum_step_hz)},
          {"listen_step_hz", std::to_string(l.listen_step_hz)},
          {"listen_freq_hz", std::to_string(l.listen.freq_hz)},
          {"listen_bw_hz", std::to_string(l.listen.bandwidth_hz)},
          {"listen_sf", std::to_string(l.listen.spreading_factor)},
          {"listen_cr", std::to_string(l.listen.coding_rate)},
          {"listen_sync", sync},
          {"listen_preamble", std::to_string(l.listen.preamble)},
          {"listen_iq_inverted", l.listen.iq_inverted ? "yes" : "no"},
          {"listen_crc", l.listen.crc_on ? "yes" : "no"},
          {"capture", l.capture ? "yes" : "no"},
          {"waterfall", l.waterfall ? "yes" : "no"}}},
    };

    // Keep everything outside the managed sections untouched.
    // Lines before the first section (file header comments) stay on top.
    std::vector<std::string> preamble;
    std::vector<std::string> kept;
    {
        std::ifstream input(path);
        std::string line;
        bool skipping = false;
        bool in_preamble = true;
        while (std::getline(input, line)) {
            const auto t = trim(line);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
                const auto name = lowercase(trim(t.substr(1, t.size() - 2)));
                skipping = name == "application" || name == "lora";
                in_preamble = false;
            }
            if (in_preamble) {
                preamble.push_back(line);
            } else if (!skipping) {
                kept.push_back(line);
            }
        }
    }
    while (!preamble.empty() && trim(preamble.back()).empty()) {
        preamble.pop_back();
    }
    while (!kept.empty() && trim(kept.back()).empty()) {
        kept.pop_back();
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "could not open file for writing";
        return false;
    }
    for (const auto& line : preamble) {
        output << line << '\n';
    }
    if (!preamble.empty()) {
        output << '\n';
    }
    for (const auto& [name, values] : managed) {
        output << '[' << name << "]\n";
        for (const auto& [k, v] : values) {
            output << k << '=' << v << '\n';
        }
        output << '\n';
    }
    for (const auto& line : kept) {
        output << line << '\n';
    }
    if (!output) {
        error = "failed while writing file";
        return false;
    }
    return true;
}

} // namespace app
