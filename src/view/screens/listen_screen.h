/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

// Continuous receive on one channel with full modem control.
class ListenScreen : public BaseScreen {
public:
    ListenScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key, bool long_pressed) override;

private:
    void build_content(lv_obj_t* content) override;
    void refresh();
    void next_preset();
    static void refresh_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* freq_label_{nullptr};
    lv_obj_t* step_label_{nullptr};
    lv_obj_t* modem_label_{nullptr};
    lv_obj_t* rssi_bar_{nullptr};
    lv_obj_t* rssi_label_{nullptr};
    lv_obj_t* info_label_{nullptr};
    std::array<lv_obj_t*, 3> tag_labels_{};
    std::array<lv_obj_t*, 3> row_labels_{};
    std::size_t preset_index_{0};
};

} // namespace screen
