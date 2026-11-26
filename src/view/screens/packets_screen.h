/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "base_screen.h"

#include <array>
#include <cstdint>

namespace screen {

// Packet log with a scrollable decoded detail view.
class PacketsScreen : public BaseScreen {
public:
    PacketsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key, bool long_pressed) override;

private:
    static constexpr std::size_t kRows = 6;

    void build_content(lv_obj_t* content) override;
    void refresh();
    void open_detail();
    void close_detail();
    int selected_index();
    static void refresh_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* header_label_{nullptr};
    std::array<lv_obj_t*, kRows> row_panels_{};
    std::array<lv_obj_t*, kRows> tag_labels_{};
    std::array<lv_obj_t*, kRows> row_labels_{};
    lv_obj_t* detail_{nullptr};
    lv_obj_t* detail_label_{nullptr};
    uint64_t selected_seq_{0};
    std::size_t top_{0};
    bool follow_newest_{true};
};

} // namespace screen
