/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "screen_utils.h"

#include <cstdio>
#include <ctime>

namespace screen::ui {

const lv_font_t* font(app::AssetManager& assets, uint32_t size, bool bold) {
    const auto* f = assets.load_standard_font(
        size, bold ? app::StandardFontWeight::Bold : app::StandardFontWeight::Regular);
    if (f) {
        return f;
    }
    if (size <= 10) return &lv_font_montserrat_10;
    if (size <= 12) return &lv_font_montserrat_12;
    if (size <= 14) return &lv_font_montserrat_14;
    return &lv_font_montserrat_16;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, int32_t x, int32_t y, int32_t w,
                lv_label_long_mode_t mode) {
    auto* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_long_mode(l, mode);
    lv_obj_set_width(l, w);
    // Single-line modes need a fixed height, otherwise LVGL wraps before truncating.
    if (mode == LV_LABEL_LONG_DOT || mode == LV_LABEL_LONG_CLIP) {
        lv_obj_set_height(l, lv_font_get_line_height(font));
    }
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t* panel(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h) {
    auto* p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

std::string time_hms(std::chrono::system_clock::time_point time) {
    const std::time_t t = std::chrono::system_clock::to_time_t(time);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[12];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

std::string modem_short(const decoder::LoraModem& m) {
    return decoder::format_mhz(m.freq_hz) + " SF" + std::to_string(m.spreading_factor) + "/" +
           decoder::format_bw(m.bandwidth_hz);
}

std::string modem_long(const decoder::LoraModem& m) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SF%u  BW%s  CR4/%u  sync %02X  IQ %s", m.spreading_factor,
                  decoder::format_bw(m.bandwidth_hz).c_str(), m.coding_rate, m.sync_word,
                  m.iq_inverted ? "inv" : "norm");
    return buf;
}

const char* protocol_tag(decoder::Protocol protocol) {
    switch (protocol) {
        case decoder::Protocol::Meshtastic: return "MT";
        case decoder::Protocol::LoRaWAN: return "LW";
        case decoder::Protocol::MeshCore: return "MC";
        default: return "RAW";
    }
}

lv_color_t protocol_color(decoder::Protocol protocol, const view::ThemePalette& colors) {
    switch (protocol) {
        case decoder::Protocol::Meshtastic: return colors.success;
        case decoder::Protocol::LoRaWAN: return colors.info;
        case decoder::Protocol::MeshCore: return colors.primary;
        default: return colors.warning;
    }
}

lv_color_t rssi_color(float dbm, const view::ThemePalette& colors) {
    if (dbm >= -90.0f) return colors.success;
    if (dbm >= -110.0f) return colors.warning;
    return colors.error;
}

std::string packet_row(const platform::radio::PacketRecord& rec) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), " %4.0f ", rec.rx.rssi_dbm);
    return time_hms(rec.time) + buf + protocol_tag(rec.decoded.protocol) + " " + rec.decoded.summary;
}

} // namespace screen::ui
