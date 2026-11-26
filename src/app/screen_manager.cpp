/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "screen_manager.h"

#include "hunt_screen.h"
#include "linux_input.h"
#include "listen_screen.h"
#include "packets_screen.h"
#include "settings_screen.h"
#include "spectrum_screen.h"

namespace app {
namespace {

void flush_requested_page_async(void* user_data) {
    auto* manager = static_cast<ScreenManager*>(user_data);
    if (manager) {
        manager->flush_requested_page();
    }
}

void current_page_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* manager = static_cast<ScreenManager*>(lv_observer_get_target(observer));
    if (manager) {
        manager->request_page(static_cast<model::AppPage>(lv_subject_get_int(subject)));
    }
}

} // namespace

ScreenManager::ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets)
    : view_model_(view_model), assets_(assets) {}

ScreenManager::~ScreenManager() {
    platform::clear_screen_key_listener(screen_key_listener, this);
    lv_async_call_cancel(flush_requested_page_async, this);
    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
}

void ScreenManager::start() {
    if (page_observer_) {
        return;
    }
    platform::set_screen_key_listener(screen_key_listener, this);
    page_observer_ = lv_subject_add_observer_with_target(view_model_.current_page_subject(),
                                                         current_page_observer, this, nullptr);
}

void ScreenManager::request_page(model::AppPage page) {
    requested_page_ = page;
    if (page_switch_scheduled_) {
        return;
    }
    page_switch_scheduled_ = lv_async_call(flush_requested_page_async, this) == LV_RESULT_OK;
}

void ScreenManager::flush_requested_page() {
    page_switch_scheduled_ = false;
    if (has_loaded_page_ && requested_page_ == loaded_page_) {
        return;
    }
    load_page(requested_page_);
}

void ScreenManager::load_page(model::AppPage page) {
    std::unique_ptr<screen::BaseScreen> next;
    switch (page) {
        case model::AppPage::Spectrum: next = std::make_unique<screen::SpectrumScreen>(view_model_, assets_); break;
        case model::AppPage::Hunt: next = std::make_unique<screen::HuntScreen>(view_model_, assets_); break;
        case model::AppPage::Listen: next = std::make_unique<screen::ListenScreen>(view_model_, assets_); break;
        case model::AppPage::Packets: next = std::make_unique<screen::PacketsScreen>(view_model_, assets_); break;
        case model::AppPage::Settings: next = std::make_unique<screen::SettingsScreen>(view_model_, assets_); break;
    }
    if (!next || !next->root()) {
        return;
    }
    lv_screen_load(next->root());
    current_screen_ = std::move(next);
    loaded_page_ = page;
    has_loaded_page_ = true;
}

lv_obj_t* ScreenManager::current_screen() const {
    return current_screen_ ? current_screen_->root() : nullptr;
}

bool ScreenManager::screen_key_listener(uint32_t key, const char*, bool long_pressed, void* user_data) {
    auto* self = static_cast<ScreenManager*>(user_data);
    if (!self) {
        return false;
    }
    auto& vm = self->view_model_;

    // Tab bar focus: arrows move between pages, Enter/Down/ESC go back to the page.
    if (vm.nav_focused()) {
        if (long_pressed) {
            return key != LV_KEY_ESC;  // hold ESC still exits
        }
        switch (key) {
            case LV_KEY_LEFT: vm.step_page(-1); return true;
            case LV_KEY_RIGHT:
            case LV_KEY_NEXT: vm.step_page(+1); return true;
            case LV_KEY_PREV: vm.step_page(-1); return true;
            case LV_KEY_ENTER:
            case LV_KEY_DOWN:
            case LV_KEY_ESC:
            case LV_KEY_UP:
                vm.set_nav_focus(false);
                return true;
            default:
                if (key >= '4' && key <= '8') {
                    vm.set_nav_focus(false);
                    return false;  // let the nav key switch page
                }
                return true;
        }
    }

    if (self->current_screen_ && !self->page_switch_scheduled_ &&
        self->current_screen_->handle_key(key, long_pressed)) {
        return true;
    }
    if (long_pressed) {
        return false;
    }
    switch (key) {
        case LV_KEY_ESC: vm.set_nav_focus(true); return false;  // ShortcutController still arms hold-to-exit
        case LV_KEY_NEXT: vm.step_page(+1); return true;
        case LV_KEY_PREV: vm.step_page(-1); return true;
        default: return false;
    }
}

} // namespace app
