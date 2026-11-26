/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"

namespace model {

const char* page_name(AppPage page) {
    switch (page) {
        case AppPage::Spectrum: return "Spectrum";
        case AppPage::Hunt: return "Hunt";
        case AppPage::Listen: return "Listen";
        case AppPage::Packets: return "Packets";
        case AppPage::Settings: return "Settings";
        default: return "?";
    }
}

const char* BaseModel::app_title() const {
    return "LoRa Decoder";
}

bool BaseModel::dark_mode() const {
    return dark_mode_;
}

void BaseModel::set_dark_mode(bool enabled) {
    dark_mode_ = enabled;
}

void BaseModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
}

AppPage BaseModel::current_page() const {
    return current_page_;
}

void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
}

} // namespace model
