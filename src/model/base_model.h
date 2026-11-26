/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lora_settings.h"

namespace model {

// Order matches the nav keys 4..8.
enum class AppPage {
    Spectrum = 0,
    Hunt = 1,
    Listen = 2,
    Packets = 3,
    Settings = 4,
};

constexpr int kPageCount = 5;

const char* page_name(AppPage page);

class BaseModel {
public:
    const char* app_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

    LoraSettings& lora() { return lora_; }
    const LoraSettings& lora() const { return lora_; }

private:
    bool dark_mode_ = true;
    AppPage current_page_ = AppPage::Hunt;
    LoraSettings lora_;
};

} // namespace model
