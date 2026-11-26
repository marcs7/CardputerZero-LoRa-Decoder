/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

#include <array>

namespace app {
class AssetManager;
}

namespace view::widgets {

// Bottom tab bar: one tab per page, bound to the physical keys 4..8.
class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;

private:
    void update_tabs();
    static void tab_clicked_cb(lv_event_t* event);
    static void update_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::array<lv_obj_t*, model::kPageCount> tabs_{};
    std::array<lv_obj_t*, model::kPageCount> key_labels_{};
    std::array<lv_obj_t*, model::kPageCount> text_labels_{};
};

} // namespace view::widgets
