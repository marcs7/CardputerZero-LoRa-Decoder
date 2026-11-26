/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "base_screen.h"

namespace screen {

// RSSI sweep across the selected band with peak hold and a tuning cursor.
class SpectrumScreen : public BaseScreen {
public:
    SpectrumScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key, bool long_pressed) override;

private:
    void build_content(lv_obj_t* content) override;
    void refresh();
    void place_cursor();
    int cursor_index();
    static void refresh_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* range_label_{nullptr};
    lv_obj_t* cursor_label_{nullptr};
    lv_obj_t* footer_label_{nullptr};
    lv_obj_t* chart_{nullptr};
    lv_obj_t* cursor_{nullptr};
    lv_obj_t* waiting_label_{nullptr};
    lv_chart_series_t* live_series_{nullptr};
    lv_chart_series_t* peak_series_{nullptr};
    uint32_t cursor_hz_{0};
};

} // namespace screen
