/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "settings_screen.h"

#include "bindings.h"
#include "screen_utils.h"

#include <cstdio>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace screen {
namespace {

enum Row : std::size_t { kBand, kProfile, kHuntMode, kSpectrumStep, kTheme, kRadio };
constexpr const char* kRowNames[] = {"Band plan", "Scan profile", "Hunt mode", "Spectrum step", "Theme", "Radio"};

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void SettingsScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    const auto* small = ui::font(assets(), 12);
    const auto* bold = ui::font(assets(), 12, true);
    const auto* tiny = ui::font(assets(), 10);

    for (std::size_t i = 0; i < kRows; ++i) {
        auto* row = ui::panel(content, 2, 2 + static_cast<int32_t>(i) * 15, 316, 15);
        lv_obj_set_style_radius(row, 3, 0);
        row_panels_[i] = row;
        key_labels_[i] = ui::label(row, small, 6, 0, 110);
        lv_label_set_text(key_labels_[i], kRowNames[i]);
        value_labels_[i] = ui::label(row, bold, 116, 0, 196, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(value_labels_[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    note_label_ = ui::label(content, tiny, 6, 2 + kRows * 15 + 2, 308, LV_LABEL_LONG_DOT);
    reactive::bind_theme(note_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(content, vm.settings_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.status_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.dark_mode_subject(), refresh_cb, this);
    refresh();
}

void SettingsScreen::refresh() {
    auto& vm = view_model();
    const auto colors = view::palette(vm.is_dark_mode());
    const auto& s = vm.settings();
    const auto st = vm.radio_status();

    char buf[96];
    for (std::size_t i = 0; i < kRows; ++i) {
        const bool sel = i == selected_;
        lv_obj_set_style_bg_color(row_panels_[i], colors.primary, 0);
        lv_obj_set_style_bg_opa(row_panels_[i], sel ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(key_labels_[i], colors.text, 0);
        lv_obj_set_style_text_color(value_labels_[i], i == kRadio ? colors.text : colors.primary, 0);
        const char* arrows_l = (sel && i != kRadio) ? "< " : "";
        const char* arrows_r = (sel && i != kRadio) ? " >" : "";
        switch (i) {
            case kBand:
                std::snprintf(buf, sizeof(buf), "%s%s%s", arrows_l, s.band.c_str(), arrows_r);
                break;
            case kProfile:
                std::snprintf(buf, sizeof(buf), "%s%s%s", arrows_l, decoder::scan_profile_name(s.profile), arrows_r);
                break;
            case kHuntMode:
                if (s.hunt_dwell_ms == 0) {
                    std::snprintf(buf, sizeof(buf), "%sCAD hopping%s", arrows_l, arrows_r);
                } else {
                    std::snprintf(buf, sizeof(buf), "%sdwell %u ms%s", arrows_l, s.hunt_dwell_ms, arrows_r);
                }
                break;
            case kSpectrumStep:
                std::snprintf(buf, sizeof(buf), "%s%s%s", arrows_l, decoder::format_bw(s.spectrum_step_hz).c_str(), arrows_r);
                break;
            case kTheme:
                std::snprintf(buf, sizeof(buf), "%s%s%s", arrows_l, vm.is_dark_mode() ? "Dark" : "Light", arrows_r);
                break;
            case kRadio:
                std::snprintf(buf, sizeof(buf), "%s %s", st.backend.c_str(),
                              !st.error.empty() ? "ERROR" : (st.radio_ready ? "ready" : "offline"));
                lv_obj_set_style_text_color(value_labels_[i],
                                            !st.error.empty() || !st.radio_ready ? colors.error : colors.success, 0);
                break;
            default:
                buf[0] = '\0';
        }
        lv_label_set_text(value_labels_[i], buf);
    }

    if (!st.error.empty()) {
        lv_label_set_text(note_label_, st.error.c_str());
        lv_obj_set_style_text_color(note_label_, colors.error, 0);
    } else {
        lv_label_set_text(note_label_, st.simulated
                                           ? "SIMULATOR  -  v" APP_VERSION "  by MarcS7 - IS0LTD"
                                           : "LoRa Decoder v" APP_VERSION "  -  by MarcS7 - IS0LTD");
        lv_obj_set_style_text_color(note_label_, colors.text, 0);
        lv_obj_set_style_text_opa(note_label_, LV_OPA_70, 0);
    }
}

void SettingsScreen::change(int delta) {
    auto& vm = view_model();
    switch (selected_) {
        case kBand: vm.cycle_band(delta); break;
        case kProfile: vm.cycle_profile(delta); break;
        case kHuntMode: vm.cycle_hunt_dwell(delta); break;
        case kSpectrumStep: vm.cycle_spectrum_step(delta); break;
        case kTheme: vm.toggle_dark_mode(); break;
        default: break;
    }
    refresh();
}

bool SettingsScreen::handle_key(uint32_t key, bool long_pressed) {
    if (long_pressed) {
        return false;
    }
    switch (key) {
        case LV_KEY_UP:
            selected_ = (selected_ + kRows - 1) % kRows;
            refresh();
            return true;
        case LV_KEY_DOWN:
            selected_ = (selected_ + 1) % kRows;
            refresh();
            return true;
        case LV_KEY_LEFT: change(-1); return true;
        case LV_KEY_RIGHT:
        case LV_KEY_ENTER: change(+1); return true;
        case 't':
        case 'T': view_model().toggle_dark_mode(); return true;
        default: return false;
    }
}

void SettingsScreen::refresh_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh();
    }
}

} // namespace screen
