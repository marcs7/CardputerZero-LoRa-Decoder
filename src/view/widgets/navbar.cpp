/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "linux_input.h"
#include "theme.h"

namespace view::widgets {
namespace {

constexpr const char* kTabNames[model::kPageCount] = {"Scan", "Hunt", "RX", "Log", "Cfg"};
constexpr const char* kTabKeys[model::kPageCount] = {"4", "5", "6", "7", "8"};

} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        platform::unregister_nav_button(i, tabs_[i]);
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }
    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(core_obj_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(core_obj_, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(core_obj_, 4, 0);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);
    platform::set_nav_shortcut_mode(false);

    const lv_font_t* key_font = assets_.load_standard_font(12);
    const lv_font_t* text_font = assets_.load_standard_font(13, app::StandardFontWeight::Regular);

    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        auto* tab = lv_obj_create(core_obj_);
        lv_obj_remove_style_all(tab);
        lv_obj_set_size(tab, 58, 22);
        lv_obj_set_style_radius(tab, 4, 0);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(tab, 3, 0);
        lv_obj_add_event_cb(tab, tab_clicked_cb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(tab, reinterpret_cast<void*>(static_cast<intptr_t>(i)));

        auto* key = lv_label_create(tab);
        lv_label_set_text(key, kTabKeys[i]);
        lv_obj_set_style_text_font(key, key_font ? key_font : &lv_font_montserrat_12, 0);
        auto* text = lv_label_create(tab);
        lv_label_set_text(text, kTabNames[i]);
        lv_obj_set_style_text_font(text, text_font ? text_font : &lv_font_montserrat_12, 0);

        tabs_[i] = tab;
        key_labels_[i] = key;
        text_labels_[i] = text;
        platform::register_nav_button(i, tab);
    }

    reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_cb, this);
    reactive::observe_obj(core_obj_, view_model_.nav_focus_subject(), update_cb, this);
    update_tabs();
}

void NavBar::update_tabs() {
    const auto colors = view::palette(view_model_.is_dark_mode());
    const auto current = static_cast<std::size_t>(view_model_.current_page());
    const bool focused = view_model_.nav_focused();
    lv_obj_set_style_border_side(core_obj_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(core_obj_, focused ? 2 : 0, 0);
    lv_obj_set_style_border_color(core_obj_, colors.warning, 0);
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const bool active = i == current;
        lv_obj_set_style_outline_width(tabs_[i], active && focused ? 2 : 0, 0);
        lv_obj_set_style_outline_color(tabs_[i], colors.warning, 0);
        lv_obj_set_style_outline_pad(tabs_[i], 1, 0);
        lv_obj_set_style_bg_color(tabs_[i], colors.primary, 0);
        lv_obj_set_style_bg_opa(tabs_[i], active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(key_labels_[i], active ? colors.background : colors.primary, 0);
        lv_obj_set_style_text_color(text_labels_[i], active ? colors.background : colors.text, 0);
    }
}

void NavBar::tab_clicked_cb(lv_event_t* event) {
    auto* self = static_cast<NavBar*>(lv_event_get_user_data(event));
    auto* tab = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!self || !tab) {
        return;
    }
    const auto index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(tab)));
    self->view_model_.show_page(static_cast<model::AppPage>(index));
}

void NavBar::update_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (self) {
        self->update_tabs();
    }
}

} // namespace view::widgets
