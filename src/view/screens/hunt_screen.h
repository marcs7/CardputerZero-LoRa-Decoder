/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

// Frequency/SF hopping scanner with live decode of captured packets.
class HuntScreen : public BaseScreen {
public:
    HuntScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key, bool long_pressed) override;

private:
    void build_content(lv_obj_t* content) override;
    void refresh();
    static void refresh_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* plan_label_{nullptr};
    lv_obj_t* mode_label_{nullptr};
    lv_obj_t* target_label_{nullptr};
    lv_obj_t* progress_label_{nullptr};
    lv_obj_t* stats_label_{nullptr};
    lv_obj_t* activity_bar_{nullptr};
    lv_obj_t* divider_{nullptr};
    std::array<lv_obj_t*, 4> tag_labels_{};
    std::array<lv_obj_t*, 4> row_labels_{};
    uint32_t last_cad_hits_{0};
};

} // namespace screen
