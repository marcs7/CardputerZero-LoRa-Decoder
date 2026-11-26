/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Small helpers shared by the LoRa screens (fonts, labels, formatting).
 */

#pragma once

#include "asset_manager.h"
#include "frequency_plan.h"
#include "radio/radio_service.h"
#include "theme.h"

#include "lvgl.h"

#include <chrono>
#include <string>

namespace screen::ui {

const lv_font_t* font(app::AssetManager& assets, uint32_t size, bool bold = false);

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, int32_t x, int32_t y, int32_t w,
                lv_label_long_mode_t mode = LV_LABEL_LONG_CLIP);

lv_obj_t* panel(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h);

std::string time_hms(std::chrono::system_clock::time_point time);
std::string modem_short(const decoder::LoraModem& modem);  // "868.100 SF7/125k"
std::string modem_long(const decoder::LoraModem& modem);   // + CR, sync, IQ
const char* protocol_tag(decoder::Protocol protocol);       // "MT", "LW", "RAW"
lv_color_t protocol_color(decoder::Protocol protocol, const view::ThemePalette& colors);
lv_color_t rssi_color(float dbm, const view::ThemePalette& colors);
std::string packet_row(const platform::radio::PacketRecord& rec);

} // namespace screen::ui
