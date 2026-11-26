/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "spectrum_screen.h"

#include "bindings.h"
#include "screen_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace screen {
namespace {

constexpr int32_t kChartX = 26;
constexpr int32_t kChartY = 16;
constexpr int32_t kChartW = 290;
constexpr int32_t kChartH = 78;
constexpr int32_t kDbMin = -130;
constexpr int32_t kDbMax = -40;

// Waterfall palette: noise floor dark blue -> cyan -> green -> yellow -> red.
lv_color_t heat_color(float dbm) {
    struct Stop { float t; uint8_t r, g, b; };
    static constexpr Stop kStops[] = {
        {0.00f, 0, 0, 24}, {0.25f, 0, 40, 160}, {0.45f, 0, 170, 210},
        {0.62f, 40, 210, 60}, {0.80f, 250, 220, 0}, {1.00f, 255, 40, 20},
    };
    float t = (dbm + 125.0f) / 65.0f;  // -125 .. -60 dBm
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    for (std::size_t i = 1; i < sizeof(kStops) / sizeof(kStops[0]); ++i) {
        if (t <= kStops[i].t) {
            const auto& a = kStops[i - 1];
            const auto& b = kStops[i];
            const float k = (t - a.t) / (b.t - a.t);
            return lv_color_make(static_cast<uint8_t>(a.r + (b.r - a.r) * k),
                                 static_cast<uint8_t>(a.g + (b.g - a.g) * k),
                                 static_cast<uint8_t>(a.b + (b.b - a.b) * k));
        }
    }
    return lv_color_make(255, 40, 20);
}

} // namespace

SpectrumScreen::SpectrumScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void SpectrumScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    const auto* small = ui::font(assets(), 12);
    const auto* tiny = ui::font(assets(), 10);

    range_label_ = ui::label(content, small, 4, 1, 150);
    reactive::bind_theme(range_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);
    cursor_label_ = ui::label(content, small, 150, 1, 166);
    lv_obj_set_style_text_align(cursor_label_, LV_TEXT_ALIGN_RIGHT, 0);

    // dB scale
    for (int i = 0; i <= 3; ++i) {
        auto* tick = ui::label(content, tiny, 0, kChartY + i * (kChartH - 10) / 3, 25);
        lv_label_set_text_fmt(tick, "%d", kDbMax - i * (kDbMax - kDbMin) / 3);
        lv_obj_set_style_text_align(tick, LV_TEXT_ALIGN_RIGHT, 0);
        reactive::bind_theme(tick, vm.dark_mode_subject(), reactive::ThemeRole::Text);
        lv_obj_set_style_text_opa(tick, LV_OPA_60, 0);
    }

    chart_ = lv_chart_create(content);
    lv_obj_set_pos(chart_, kChartX, kChartY);
    lv_obj_set_size(chart_, kChartW, kChartH);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, kDbMin, kDbMax);
    lv_chart_set_div_line_count(chart_, 4, 0);
    lv_chart_set_update_mode(chart_, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_obj_set_style_pad_all(chart_, 0, 0);
    lv_obj_set_style_radius(chart_, 2, 0);
    lv_obj_set_style_border_width(chart_, 1, 0);
    lv_obj_set_style_line_width(chart_, 1, LV_PART_ITEMS);
    lv_obj_set_style_size(chart_, 0, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(chart_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chart_, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_point_count(chart_, 2);

    const auto colors = view::palette(vm.is_dark_mode());
    peak_series_ = lv_chart_add_series(chart_, colors.warning, LV_CHART_AXIS_PRIMARY_Y);
    live_series_ = lv_chart_add_series(chart_, colors.primary, LV_CHART_AXIS_PRIMARY_Y);

    // Waterfall canvas, same frame as the chart (newest sweep at the top).
    waterfall_buf_.resize(LV_CANVAS_BUF_SIZE(kChartW, kChartH, 16, LV_DRAW_BUF_STRIDE_ALIGN));
    waterfall_ = lv_canvas_create(content);
    lv_canvas_set_buffer(waterfall_, waterfall_buf_.data(), kChartW, kChartH, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(waterfall_, lv_color_make(0, 0, 24), LV_OPA_COVER);
    lv_obj_set_pos(waterfall_, kChartX, kChartY);

    cursor_ = lv_obj_create(content);
    lv_obj_remove_style_all(cursor_);
    lv_obj_set_size(cursor_, 1, kChartH);
    lv_obj_set_style_bg_opa(cursor_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(cursor_, LV_OBJ_FLAG_CLICKABLE);

    waiting_label_ = ui::label(content, small, kChartX, kChartY + kChartH / 2 - 8, kChartW);
    lv_obj_set_style_text_align(waiting_label_, LV_TEXT_ALIGN_CENTER, 0);
    reactive::bind_theme(waiting_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);

    footer_label_ = ui::label(content, tiny, 4, kChartY + kChartH + 2, 312);
    reactive::bind_theme(footer_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);
    lv_obj_set_style_text_opa(footer_label_, LV_OPA_70, 0);

    const auto cfg = vm.current_spectrum_config();
    cursor_hz_ = vm.spectrum_center_hz() ? vm.spectrum_center_hz() : vm.settings().listen.freq_hz;
    if (cursor_hz_ < cfg.start_hz || cursor_hz_ > cfg.end_hz) {
        cursor_hz_ = (cfg.start_hz + cfg.end_hz) / 2;
    }

    reactive::observe_obj(content, vm.spectrum_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.status_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.dark_mode_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.settings_version_subject(), refresh_cb, this);
    refresh();
}

int SpectrumScreen::cursor_index() {
    const auto& sp = view_model().spectrum();
    if (sp.step_hz == 0 || sp.live.empty() || cursor_hz_ < sp.start_hz) {
        return 0;
    }
    const auto idx = static_cast<int>((cursor_hz_ - sp.start_hz + sp.step_hz / 2) / sp.step_hz);
    return std::clamp(idx, 0, static_cast<int>(sp.live.size()) - 1);
}

void SpectrumScreen::place_cursor() {
    const auto& sp = view_model().spectrum();
    const int n = static_cast<int>(sp.live.size());
    if (n < 2) {
        lv_obj_add_flag(cursor_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(cursor_, LV_OBJ_FLAG_HIDDEN);
    const int idx = cursor_index();
    const int32_t x = kChartX + 1 + static_cast<int32_t>(idx * (kChartW - 3) / (n - 1));
    lv_obj_set_pos(cursor_, x, kChartY);
}

void SpectrumScreen::refresh() {
    auto& vm = view_model();
    const auto colors = view::palette(vm.is_dark_mode());
    const auto& sp = vm.spectrum();
    const auto cfg = vm.current_spectrum_config();
    const auto status = vm.radio_status();

    lv_obj_set_style_bg_color(chart_, colors.surface, 0);
    lv_obj_set_style_border_color(chart_, colors.border, 0);
    lv_obj_set_style_line_color(chart_, colors.border, LV_PART_MAIN);
    lv_chart_set_series_color(chart_, live_series_, colors.primary);
    lv_chart_set_series_color(chart_, peak_series_, colors.warning);
    lv_obj_set_style_bg_color(cursor_, colors.error, 0);

    lv_label_set_text_fmt(range_label_, "%s-%s MHz", decoder::format_mhz(cfg.start_hz).c_str(),
                          decoder::format_mhz(cfg.end_hz).c_str());

    const bool waterfall = vm.settings().waterfall;
    if (waterfall) {
        lv_obj_add_flag(chart_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(waterfall_, LV_OBJ_FLAG_HIDDEN);
        draw_waterfall();
    } else {
        lv_obj_remove_flag(chart_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(waterfall_, LV_OBJ_FLAG_HIDDEN);
    }

    if (sp.live.empty()) {
        lv_obj_remove_flag(waiting_label_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(waiting_label_, status.error.empty() ? "sweeping..." : status.error.c_str());
        lv_obj_set_style_text_color(waiting_label_, status.error.empty() ? colors.text : colors.error, 0);
        lv_label_set_text(cursor_label_, "");
        lv_chart_set_point_count(chart_, 2);
        lv_chart_set_all_values(chart_, live_series_, LV_CHART_POINT_NONE);
        lv_chart_set_all_values(chart_, peak_series_, LV_CHART_POINT_NONE);
        lv_label_set_text_fmt(footer_label_, "step %s  rbw %s", decoder::format_bw(cfg.step_hz).c_str(),
                              decoder::format_bw(cfg.rbw_hz).c_str());
        place_cursor();
        return;
    }
    lv_obj_add_flag(waiting_label_, LV_OBJ_FLAG_HIDDEN);

    const auto n = static_cast<uint32_t>(sp.live.size());
    if (lv_chart_get_point_count(chart_) != n) {
        lv_chart_set_point_count(chart_, n);
    }
    int32_t* live = lv_chart_get_series_y_array(chart_, live_series_);
    int32_t* peak = lv_chart_get_series_y_array(chart_, peak_series_);
    for (uint32_t i = 0; i < n; ++i) {
        live[i] = std::clamp(static_cast<int32_t>(std::lround(sp.live[i])), kDbMin, kDbMax);
        peak[i] = i < sp.peak.size()
                      ? std::clamp(static_cast<int32_t>(std::lround(sp.peak[i])), kDbMin, kDbMax)
                      : live[i];
    }
    lv_chart_refresh(chart_);

    const int idx = cursor_index();
    cursor_hz_ = sp.start_hz + static_cast<uint32_t>(idx) * sp.step_hz;
    const float level = sp.live[static_cast<std::size_t>(idx)];
    const float pk = static_cast<std::size_t>(idx) < sp.peak.size() ? sp.peak[static_cast<std::size_t>(idx)] : level;
    lv_label_set_text_fmt(cursor_label_, "%s  %d dBm  pk %d", decoder::format_mhz(cursor_hz_).c_str(),
                          static_cast<int>(std::lround(level)), static_cast<int>(std::lround(pk)));
    lv_obj_set_style_text_color(cursor_label_, ui::rssi_color(pk, colors), 0);

    // strongest peak in view
    const auto best = std::max_element(sp.peak.begin(), sp.peak.end());
    const uint32_t best_hz = sp.start_hz + static_cast<uint32_t>(best - sp.peak.begin()) * sp.step_hz;
    static constexpr const char* kZoom[] = {"full", "2M", "500k"};
    lv_label_set_text_fmt(footer_label_, "max %s %d dBm   step %s  %ums  zoom %s", decoder::format_mhz(best_hz).c_str(),
                          static_cast<int>(std::lround(*best)), decoder::format_bw(sp.step_hz).c_str(),
                          sp.sweep_ms, kZoom[vm.spectrum_zoom()]);
    place_cursor();
}

void SpectrumScreen::draw_waterfall() {
    const auto& history = view_model().spectrum().history;
    lv_draw_buf_t* buf = lv_canvas_get_draw_buf(waterfall_);
    if (!buf) {
        return;
    }
    const uint32_t stride = buf->header.stride;
    auto* base = static_cast<uint8_t*>(buf->data);
    const uint16_t floor_px = lv_color_to_u16(lv_color_make(0, 0, 24));
    for (int32_t y = 0; y < kChartH; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(base + static_cast<std::size_t>(y) * stride);
        if (static_cast<std::size_t>(y) >= history.size() || history[static_cast<std::size_t>(y)].size() < 2) {
            for (int32_t x = 0; x < kChartW; ++x) row[x] = floor_px;
            continue;
        }
        const auto& sweep = history[static_cast<std::size_t>(y)];
        const auto n = static_cast<int32_t>(sweep.size());
        for (int32_t x = 0; x < kChartW; ++x) {
            const int32_t bin = x * (n - 1) / (kChartW - 1);
            row[x] = lv_color_to_u16(heat_color(sweep[static_cast<std::size_t>(bin)]));
        }
    }
    lv_obj_invalidate(waterfall_);
}

bool SpectrumScreen::handle_key(uint32_t key, bool long_pressed) {
    auto& vm = view_model();
    const auto& sp = vm.spectrum();
    const uint32_t step = sp.step_hz ? sp.step_hz : vm.current_spectrum_config().step_hz;
    const int mult = long_pressed ? 10 : 1;
    switch (key) {
        case LV_KEY_LEFT:
            if (cursor_hz_ > step * mult) cursor_hz_ -= step * mult;
            refresh();
            return true;
        case LV_KEY_RIGHT:
            cursor_hz_ += step * mult;
            refresh();
            return true;
        case LV_KEY_UP:
            if (!long_pressed) vm.zoom_spectrum(+1, cursor_hz_);
            return true;
        case LV_KEY_DOWN:
            if (!long_pressed) vm.zoom_spectrum(-1, cursor_hz_);
            return true;
        case LV_KEY_ENTER:
            if (!long_pressed) {
                vm.set_listen_frequency(cursor_hz_);
                vm.show_page(model::AppPage::Listen);
            }
            return true;
        case 'p':
        case 'P':
            vm.reset_spectrum_peaks();
            return true;
        case 'w':
        case 'W':
            if (!long_pressed) vm.toggle_waterfall();
            return true;
        default:
            return false;
    }
}

void SpectrumScreen::refresh_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<SpectrumScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh();
    }
}

} // namespace screen
