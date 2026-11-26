/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "base_screen.h"

#include <array>

namespace screen {

class SettingsScreen : public BaseScreen {
public:
    SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    bool handle_key(uint32_t key, bool long_pressed) override;

private:
    static constexpr std::size_t kRows = 6;    // visible
    static constexpr std::size_t kItems = 8;   // total

    void build_content(lv_obj_t* content) override;
    void refresh();
    void change(int delta);
    static void refresh_cb(lv_observer_t* observer, lv_subject_t* subject);

    std::array<lv_obj_t*, kRows> row_panels_{};
    std::array<lv_obj_t*, kRows> key_labels_{};
    std::array<lv_obj_t*, kRows> value_labels_{};
    lv_obj_t* note_label_{nullptr};
    std::size_t selected_{0};
    std::size_t top_{0};
};

} // namespace screen
