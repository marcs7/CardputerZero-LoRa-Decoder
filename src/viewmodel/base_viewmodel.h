/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"
#include "pcap_writer.h"
#include "radio/radio_service.h"

#include "lvgl.h"

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace viewmodel {

struct SpectrumView {
    uint32_t start_hz{0};
    uint32_t step_hz{0};
    std::vector<float> live;
    std::vector<float> peak;
    uint32_t sweep_ms{0};
    uint32_t sweeps{0};
    // Newest sweep first; reset whenever the sweep layout changes.
    std::deque<std::vector<float>> history;
    static constexpr std::size_t kHistoryRows = 78;
};

class BaseViewModel {
public:
    static constexpr std::size_t kMaxPackets = 300;

    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    // ---- template-level state ----
    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void show_page(model::AppPage page);
    void step_page(int delta);  // wraps around

    // Keyboard focus on the bottom tab bar (arrows switch pages).
    lv_subject_t* nav_focus_subject();
    bool nav_focused() const { return nav_focus_subject_.value(); }
    void set_nav_focus(bool focused);
    void request_quit();

    // ---- radio ----
    // Attaches the radio service (owned by the application) and starts polling it.
    void attach_radio(platform::radio::RadioService* service);
    platform::radio::ServiceStatus radio_status() const;

    lv_subject_t* packets_version_subject();   // bumps on new/cleared packets
    lv_subject_t* spectrum_version_subject();  // bumps on each sweep
    lv_subject_t* status_version_subject();    // bumps ~4 Hz
    lv_subject_t* settings_version_subject();  // bumps on settings change

    const std::deque<platform::radio::PacketRecord>& packets() const { return packets_; }
    void clear_packets();
    uint32_t packet_count_total() const { return packets_total_; }

    const SpectrumView& spectrum() const { return spectrum_; }
    void reset_spectrum_peaks();
    int spectrum_zoom() const { return spectrum_zoom_; }
    uint32_t spectrum_center_hz() const { return spectrum_center_hz_; }
    void zoom_spectrum(int delta, uint32_t center_hz);

    // ---- settings ----
    const model::LoraSettings& settings() const { return model_.lora(); }
    void cycle_band(int delta);
    void cycle_profile(int delta);
    void cycle_hunt_dwell(int delta);
    void cycle_spectrum_step(int delta);
    void tune_listen(int steps);
    void set_listen_frequency(uint32_t freq_hz);
    void set_listen_modem(const decoder::LoraModem& modem);
    void cycle_listen_step(int delta);
    void cycle_listen_sf(int delta);
    void cycle_listen_bw(int delta);
    void cycle_listen_cr(int delta);
    void cycle_listen_sync(int delta);
    void toggle_listen_iq();
    void toggle_waterfall();

    // ---- PCAP capture (LoRaTAP) ----
    void set_capture_directory(std::string directory);
    void toggle_capture();
    bool capture_active() const { return pcap_.is_open(); }
    const std::string& capture_path() const { return pcap_.path(); }
    uint32_t capture_count() const { return pcap_.count(); }
    const std::string& capture_error() const { return capture_error_; }
    void apply_settings(const model::LoraSettings& settings);  // from config, no save

    std::vector<decoder::ScanTarget> current_targets() const;
    platform::radio::SpectrumConfig current_spectrum_config() const;

    // Called after any user-driven settings/theme change (persistence hook).
    void set_settings_listener(std::function<void()> listener);

private:
    void publish_all();
    void publish_title();
    void settings_changed();
    void apply_radio_mode();
    static void poll_timer_cb(lv_timer_t* timer);
    void poll_radio();

    model::BaseModel model_;
    reactive::StringSubject<40> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::BoolSubject nav_focus_subject_;
    reactive::IntSubject packets_version_subject_;
    reactive::IntSubject spectrum_version_subject_;
    reactive::IntSubject status_version_subject_;
    reactive::IntSubject settings_version_subject_;

    platform::radio::RadioService* radio_{nullptr};
    lv_timer_t* poll_timer_{nullptr};
    uint32_t poll_ticks_{0};
    std::deque<platform::radio::PacketRecord> packets_;
    uint32_t packets_total_{0};
    SpectrumView spectrum_;
    int spectrum_zoom_{0};  // 0 = full band, 1 = 2 MHz, 2 = 500 kHz
    uint32_t spectrum_center_hz_{0};
    std::function<void()> settings_listener_;
    void start_capture();
    std::string capture_directory_;
    decoder::PcapWriter pcap_;
    std::string capture_error_;
};

} // namespace viewmodel
