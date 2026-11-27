/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "listen_screen.h"

#include "bindings.h"
#include "screen_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace screen {

ListenScreen::ListenScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void ListenScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    const auto* big = ui::font(assets(), 18, true);
    const auto* small = ui::font(assets(), 12);
    const auto* tiny = ui::font(assets(), 10);

    freq_label_ = ui::label(content, big, 6, 0, 200);
    step_label_ = ui::label(content, small, 200, 4, 114);
    lv_obj_set_style_text_align(step_label_, LV_TEXT_ALIGN_RIGHT, 0);
    reactive::bind_theme(step_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);

    modem_label_ = ui::label(content, small, 6, 22, 308);
    reactive::bind_theme(modem_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);

    auto* rssi_caption = ui::label(content, tiny, 6, 39, 30);
    lv_label_set_text(rssi_caption, "RSSI");
    reactive::bind_theme(rssi_caption, vm.dark_mode_subject(), reactive::ThemeRole::Text);
    rssi_bar_ = lv_bar_create(content);
    lv_obj_set_pos(rssi_bar_, 36, 40);
    lv_obj_set_size(rssi_bar_, 200, 9);
    lv_bar_set_range(rssi_bar_, -130, -40);
    lv_obj_set_style_radius(rssi_bar_, 2, 0);
    lv_obj_set_style_radius(rssi_bar_, 2, LV_PART_INDICATOR);
    rssi_label_ = ui::label(content, small, 240, 37, 74);
    lv_obj_set_style_text_align(rssi_label_, LV_TEXT_ALIGN_RIGHT, 0);

    info_label_ = ui::label(content, tiny, 6, 52, 308);

    for (std::size_t i = 0; i < row_labels_.size(); ++i) {
        const int32_t y = 66 + static_cast<int32_t>(i) * 14;
        tag_labels_[i] = ui::label(content, tiny, 6, y + 1, 26);
        row_labels_[i] = ui::label(content, small, 32, y, 284, LV_LABEL_LONG_DOT);
        reactive::bind_theme(row_labels_[i], vm.dark_mode_subject(), reactive::ThemeRole::Text);
    }

    reactive::observe_obj(content, vm.status_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.packets_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.settings_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.dark_mode_subject(), refresh_cb, this);
    refresh();
}

void ListenScreen::refresh() {
    auto& vm = view_model();
    const auto colors = view::palette(vm.is_dark_mode());
    const auto& s = vm.settings();
    const auto st = vm.radio_status();

    lv_label_set_text_fmt(freq_label_, "%s MHz", decoder::format_mhz(s.listen.freq_hz).c_str());
    lv_obj_set_style_text_color(freq_label_, colors.primary, 0);
    lv_label_set_text_fmt(step_label_, "step %s", decoder::format_bw(s.listen_step_hz).c_str());
    lv_label_set_text(modem_label_, ui::modem_long(s.listen).c_str());

    const bool listening = st.mode == platform::radio::ServiceMode::Listen && st.error.empty();
    const float rssi = listening ? st.rssi_dbm : -130.0f;
    lv_bar_set_value(rssi_bar_, std::clamp(static_cast<int32_t>(std::lround(rssi)), -130, -40), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(rssi_bar_, colors.border, 0);
    lv_obj_set_style_bg_color(rssi_bar_, ui::rssi_color(rssi + 20.0f, colors), LV_PART_INDICATOR);
    if (listening) {
        lv_label_set_text_fmt(rssi_label_, "%d dBm", static_cast<int>(std::lround(rssi)));
    } else {
        lv_label_set_text(rssi_label_, "--");
    }
    lv_obj_set_style_text_color(rssi_label_, colors.text, 0);

    if (!st.error.empty()) {
        lv_label_set_text(info_label_, st.error.c_str());
        lv_obj_set_style_text_color(info_label_, colors.error, 0);
    } else {
        const double toa = decoder::time_on_air_ms(s.listen, 32);
        lv_label_set_text_fmt(info_label_, "preamble %u  CRC %s  ToA(32B) %.0f ms  packets %u", s.listen.preamble,
                              s.listen.crc_on ? "on" : "off", toa, st.packets);
        lv_obj_set_style_text_color(info_label_, colors.text, 0);
        lv_obj_set_style_text_opa(info_label_, LV_OPA_70, 0);
    }

    // Only packets heard on this channel.
    std::size_t row = 0;
    for (const auto& rec : vm.packets()) {
        if (row >= row_labels_.size()) break;
        if (rec.source != "Listen") continue;
        lv_label_set_text(tag_labels_[row], ui::protocol_tag(rec.decoded.protocol));
        lv_obj_set_style_text_color(tag_labels_[row], ui::protocol_color(rec.decoded.protocol, colors), 0);
        char head[40];
        std::snprintf(head, sizeof(head), "%.0f/%.1f ", rec.rx.rssi_dbm, rec.rx.snr_db);
        lv_label_set_text(row_labels_[row], (std::string(head) + rec.decoded.summary).c_str());
        lv_obj_set_style_text_opa(row_labels_[row], rec.rx.crc_error ? LV_OPA_50 : LV_OPA_COVER, 0);
        ++row;
    }
    for (; row < row_labels_.size(); ++row) {
        lv_label_set_text(tag_labels_[row], "");
        lv_label_set_text(row_labels_[row], row == 0 ? "listening..." : "");
        lv_obj_set_style_text_opa(row_labels_[row], LV_OPA_50, 0);
    }
}

void ListenScreen::next_preset() {
    auto& vm = view_model();
    const auto* plan = decoder::find_band_plan(vm.settings().band);
    if (!plan) return;
    auto presets = decoder::build_scan_targets(*plan, decoder::ScanProfile::Meshtastic);
    const auto mc = decoder::build_scan_targets(*plan, decoder::ScanProfile::MeshCore);
    presets.insert(presets.end(), mc.begin(), mc.end());
    if (presets.empty()) return;
    preset_index_ = (preset_index_ + 1) % presets.size();
    vm.set_listen_modem(presets[preset_index_].modem);
}

bool ListenScreen::handle_key(uint32_t key, bool long_pressed) {
    auto& vm = view_model();
    const int mult = long_pressed ? 10 : 1;
    switch (key) {
        case LV_KEY_LEFT: vm.tune_listen(-mult); return true;
        case LV_KEY_RIGHT: vm.tune_listen(+mult); return true;
        default: break;
    }
    if (long_pressed) {
        return false;
    }
    switch (key) {
        case LV_KEY_UP: vm.cycle_listen_sf(+1); return true;
        case LV_KEY_DOWN: vm.cycle_listen_sf(-1); return true;
        case 'b': case 'B': vm.cycle_listen_bw(+1); return true;
        case 'r': case 'R': vm.cycle_listen_cr(+1); return true;
        case 's': case 'S': vm.cycle_listen_step(+1); return true;
        case 'w': case 'W': vm.cycle_listen_sync(+1); return true;
        case 'i': case 'I': vm.toggle_listen_iq(); return true;
        case 'm': case 'M': next_preset(); return true;
        default: return false;
    }
}

void ListenScreen::refresh_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<ListenScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh();
    }
}

} // namespace screen
