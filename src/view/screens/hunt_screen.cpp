/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hunt_screen.h"

#include "bindings.h"
#include "screen_utils.h"

#include <cstdio>

namespace screen {

HuntScreen::HuntScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void HuntScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    const auto* bold = ui::font(assets(), 14, true);
    const auto* small = ui::font(assets(), 12);
    const auto* tiny = ui::font(assets(), 10);

    plan_label_ = ui::label(content, bold, 6, 1, 220);
    mode_label_ = ui::label(content, small, 200, 2, 114);
    lv_obj_set_style_text_align(mode_label_, LV_TEXT_ALIGN_RIGHT, 0);
    reactive::bind_theme(mode_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);

    activity_bar_ = ui::panel(content, 6, 21, 6, 10);
    lv_obj_set_style_radius(activity_bar_, 2, 0);
    lv_obj_set_style_bg_opa(activity_bar_, LV_OPA_COVER, 0);
    target_label_ = ui::label(content, small, 16, 19, 230);
    reactive::bind_theme(target_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);
    progress_label_ = ui::label(content, small, 240, 19, 74);
    lv_obj_set_style_text_align(progress_label_, LV_TEXT_ALIGN_RIGHT, 0);
    reactive::bind_theme(progress_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);

    stats_label_ = ui::label(content, tiny, 6, 35, 308);

    divider_ = ui::panel(content, 6, 49, 308, 1);
    lv_obj_set_style_bg_opa(divider_, LV_OPA_COVER, 0);

    for (std::size_t i = 0; i < row_labels_.size(); ++i) {
        const int32_t y = 53 + static_cast<int32_t>(i) * 14;
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

void HuntScreen::refresh() {
    auto& vm = view_model();
    const auto colors = view::palette(vm.is_dark_mode());
    const auto& settings = vm.settings();
    const auto st = vm.radio_status();

    lv_label_set_text_fmt(plan_label_, "%s  %s", settings.band.c_str(),
                          decoder::scan_profile_name(settings.profile));
    lv_obj_set_style_text_color(plan_label_, colors.primary, 0);
    if (settings.hunt_dwell_ms == 0) {
        lv_label_set_text(mode_label_, "CAD hop");
    } else {
        lv_label_set_text_fmt(mode_label_, "dwell %u ms", settings.hunt_dwell_ms);
    }

    const bool running = st.mode == platform::radio::ServiceMode::Hunt && st.error.empty();
    if (running && !st.target_label.empty()) {
        // LoRaWAN labels already carry the frequency; Meshtastic ones carry the preset name.
        if (st.target_label.rfind("MT ", 0) == 0) {
            lv_label_set_text_fmt(target_label_, "%s  %s SF%u", st.target_label.c_str(),
                                  decoder::format_mhz(st.target_freq_hz).c_str(), st.target_sf);
        } else {
            lv_label_set_text_fmt(target_label_, "%s  SF%u", st.target_label.c_str(), st.target_sf);
        }
        lv_label_set_text_fmt(progress_label_, "%u/%u", static_cast<unsigned>(st.target_index + 1),
                              static_cast<unsigned>(st.target_count));
    } else {
        lv_label_set_text(target_label_, st.radio_ready ? "starting..." : "radio offline");
        lv_label_set_text(progress_label_, "");
    }
    // Activity LED: flashes on CAD detections.
    const bool hit = st.cad_hits != last_cad_hits_;
    last_cad_hits_ = st.cad_hits;
    lv_obj_set_style_bg_color(activity_bar_, hit ? colors.warning : (running ? colors.success : colors.error), 0);

    if (!st.error.empty()) {
        lv_label_set_text(stats_label_, st.error.c_str());
        lv_obj_set_style_text_color(stats_label_, colors.error, 0);
    } else {
        lv_label_set_text_fmt(stats_label_, "sweeps %u   CAD hits %u   packets %u   CRC err %u", st.hunt_cycles,
                              st.cad_hits, st.packets, st.crc_errors);
        lv_obj_set_style_text_color(stats_label_, colors.text, 0);
        lv_obj_set_style_text_opa(stats_label_, LV_OPA_70, 0);
    }
    lv_obj_set_style_bg_color(divider_, colors.border, 0);

    const auto& packets = vm.packets();
    for (std::size_t i = 0; i < row_labels_.size(); ++i) {
        if (i >= packets.size()) {
            lv_label_set_text(tag_labels_[i], "");
            lv_label_set_text(row_labels_[i], i == 0 ? "waiting for packets..." : "");
            lv_obj_set_style_text_opa(row_labels_[i], LV_OPA_50, 0);
            continue;
        }
        const auto& rec = packets[i];
        lv_label_set_text(tag_labels_[i], ui::protocol_tag(rec.decoded.protocol));
        lv_obj_set_style_text_color(tag_labels_[i], ui::protocol_color(rec.decoded.protocol, colors), 0);
        char rssi[16];
        std::snprintf(rssi, sizeof(rssi), "%.0f ", rec.rx.rssi_dbm);
        lv_label_set_text(row_labels_[i], (std::string(rssi) + rec.decoded.summary).c_str());
        lv_obj_set_style_text_opa(row_labels_[i], rec.rx.crc_error ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
}

bool HuntScreen::handle_key(uint32_t key, bool long_pressed) {
    if (long_pressed) {
        return false;
    }
    auto& vm = view_model();
    switch (key) {
        case LV_KEY_LEFT: vm.cycle_profile(-1); return true;
        case LV_KEY_RIGHT: vm.cycle_profile(+1); return true;
        case LV_KEY_UP: vm.cycle_band(-1); return true;
        case LV_KEY_DOWN: vm.cycle_band(+1); return true;
        case 'd':
        case 'D': vm.cycle_hunt_dwell(+1); return true;
        case LV_KEY_ENTER: vm.show_page(model::AppPage::Packets); return true;
        default: return false;
    }
}

void HuntScreen::refresh_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<HuntScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh();
    }
}

} // namespace screen
