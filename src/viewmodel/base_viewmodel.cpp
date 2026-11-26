/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD (modifications)
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

#include <algorithm>
#include <cstdio>

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

template <typename T>
T cycle_option(const std::vector<T>& options, T current, int delta) {
    if (options.empty()) {
        return current;
    }
    auto it = std::find(options.begin(), options.end(), current);
    int index = it == options.end() ? 0 : static_cast<int>(it - options.begin());
    const int n = static_cast<int>(options.size());
    index = ((index + delta) % n + n) % n;
    return options[static_cast<std::size_t>(index)];
}

constexpr uint32_t kPollPeriodMs = 50;
constexpr uint32_t kStatusEveryTicks = 5;  // 250 ms
constexpr float kPeakDecayDb = 0.6f;       // per sweep

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false),
      nav_focus_subject_(false),
      packets_version_subject_(0),
      spectrum_version_subject_(0),
      status_version_subject_(0),
      settings_version_subject_(0) {
    publish_title();
}

BaseViewModel::~BaseViewModel() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

lv_subject_t* BaseViewModel::title_subject() { return title_subject_.native(); }
lv_subject_t* BaseViewModel::dark_mode_subject() { return dark_mode_subject_.native(); }
lv_subject_t* BaseViewModel::current_page_subject() { return current_page_subject_.native(); }
lv_subject_t* BaseViewModel::quit_requested_subject() { return quit_requested_subject_.native(); }
lv_subject_t* BaseViewModel::packets_version_subject() { return packets_version_subject_.native(); }
lv_subject_t* BaseViewModel::spectrum_version_subject() { return spectrum_version_subject_.native(); }
lv_subject_t* BaseViewModel::status_version_subject() { return status_version_subject_.native(); }
lv_subject_t* BaseViewModel::settings_version_subject() { return settings_version_subject_.native(); }

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::show_page(model::AppPage page) {
    if (page == model_.current_page()) {
        return;
    }
    model_.set_current_page(page);
    apply_radio_mode();
    publish_all();
}

void BaseViewModel::step_page(int delta) {
    const int n = model::kPageCount;
    const int next = ((static_cast<int>(model_.current_page()) + delta) % n + n) % n;
    show_page(static_cast<model::AppPage>(next));
}

lv_subject_t* BaseViewModel::nav_focus_subject() { return nav_focus_subject_.native(); }

void BaseViewModel::set_nav_focus(bool focused) {
    if (nav_focus_subject_.value() != focused) {
        nav_focus_subject_.set(focused);
    }
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::publish_all() {
    publish_title();
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

void BaseViewModel::publish_title() {
    char title[40];
    const bool sim = radio_ && radio_->status().simulated;
    std::snprintf(title, sizeof(title), "%s%s", model::page_name(model_.current_page()),
                  sim ? " [SIM]" : "");
    title_subject_.set(title);
}

// ---------------------------------------------------------------- radio

void BaseViewModel::attach_radio(platform::radio::RadioService* service) {
    radio_ = service;
    if (!poll_timer_) {
        poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    }
    apply_radio_mode();
    publish_title();
}

platform::radio::ServiceStatus BaseViewModel::radio_status() const {
    if (!radio_) {
        platform::radio::ServiceStatus st;
        st.error = "radio not initialised";
        return st;
    }
    return radio_->status();
}

void BaseViewModel::apply_radio_mode() {
    if (!radio_) {
        return;
    }
    switch (model_.current_page()) {
        case model::AppPage::Spectrum:
            spectrum_.live.clear();
            spectrum_.peak.clear();
            radio_->request_spectrum(current_spectrum_config());
            break;
        case model::AppPage::Hunt:
            radio_->request_hunt(current_targets(), {model_.lora().hunt_dwell_ms});
            break;
        case model::AppPage::Listen:
            radio_->request_listen(model_.lora().listen);
            break;
        case model::AppPage::Packets:
        case model::AppPage::Settings:
        default:
            break;  // keep whatever the radio was doing
    }
}

void BaseViewModel::poll_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<BaseViewModel*>(lv_timer_get_user_data(timer));
    if (self) {
        self->poll_radio();
    }
}

void BaseViewModel::poll_radio() {
    if (!radio_) {
        return;
    }
    auto fresh = radio_->take_packets();
    if (!fresh.empty()) {
        for (auto& rec : fresh) {
            packets_.push_front(std::move(rec));
            ++packets_total_;
        }
        while (packets_.size() > kMaxPackets) {
            packets_.pop_back();
        }
        packets_version_subject_.set(packets_version_subject_.value() + 1);
    }

    platform::radio::SpectrumFrame frame;
    if (radio_->take_spectrum(frame)) {
        const bool same_layout = frame.start_hz == spectrum_.start_hz &&
                                 frame.step_hz == spectrum_.step_hz &&
                                 frame.dbm.size() == spectrum_.peak.size();
        spectrum_.start_hz = frame.start_hz;
        spectrum_.step_hz = frame.step_hz;
        spectrum_.sweep_ms = frame.sweep_ms;
        if (!same_layout) {
            spectrum_.peak = frame.dbm;
            spectrum_.sweeps = 0;
        } else {
            for (std::size_t i = 0; i < frame.dbm.size(); ++i) {
                spectrum_.peak[i] = std::max(frame.dbm[i], spectrum_.peak[i] - kPeakDecayDb);
            }
        }
        spectrum_.live = std::move(frame.dbm);
        ++spectrum_.sweeps;
        spectrum_version_subject_.set(spectrum_version_subject_.value() + 1);
    }

    if (++poll_ticks_ >= kStatusEveryTicks) {
        poll_ticks_ = 0;
        status_version_subject_.set(status_version_subject_.value() + 1);
    }
}

void BaseViewModel::clear_packets() {
    packets_.clear();
    packets_version_subject_.set(packets_version_subject_.value() + 1);
}

void BaseViewModel::reset_spectrum_peaks() {
    spectrum_.peak = spectrum_.live;
    spectrum_version_subject_.set(spectrum_version_subject_.value() + 1);
}

void BaseViewModel::zoom_spectrum(int delta, uint32_t center_hz) {
    const int next = std::clamp(spectrum_zoom_ + delta, 0, 2);
    if (next == spectrum_zoom_) {
        return;
    }
    spectrum_zoom_ = next;
    spectrum_center_hz_ = center_hz;
    if (model_.current_page() == model::AppPage::Spectrum) {
        spectrum_.live.clear();
        spectrum_.peak.clear();
        if (radio_) {
            radio_->request_spectrum(current_spectrum_config());
        }
    }
    settings_version_subject_.set(settings_version_subject_.value() + 1);
}

std::vector<decoder::ScanTarget> BaseViewModel::current_targets() const {
    const auto* plan = decoder::find_band_plan(model_.lora().band);
    if (!plan) {
        return {};
    }
    return decoder::build_scan_targets(*plan, model_.lora().profile);
}

platform::radio::SpectrumConfig BaseViewModel::current_spectrum_config() const {
    platform::radio::SpectrumConfig cfg;
    const auto* plan = decoder::find_band_plan(model_.lora().band);
    uint32_t start = plan ? plan->sweep_start_hz : 863000000;
    uint32_t end = plan ? plan->sweep_end_hz : 870000000;
    if (spectrum_zoom_ > 0) {
        const uint32_t span = spectrum_zoom_ == 1 ? 2000000 : 500000;
        uint32_t center = spectrum_center_hz_ ? spectrum_center_hz_ : (start + end) / 2;
        center = std::clamp(center, start + span / 2, end - span / 2);
        start = center - span / 2;
        end = center + span / 2;
    }
    // Keep at most ~300 bins so a sweep stays well under a second.
    uint32_t step = spectrum_zoom_ == 2 ? 5000 : (spectrum_zoom_ == 1 ? 12500 : model_.lora().spectrum_step_hz);
    while ((end - start) / step > 300) {
        step *= 2;
    }
    cfg.start_hz = start;
    cfg.end_hz = end;
    cfg.step_hz = step;
    cfg.rbw_hz = spectrum_zoom_ == 2 ? 62500 : 125000;
    return cfg;
}

// ---------------------------------------------------------------- settings

void BaseViewModel::set_settings_listener(std::function<void()> listener) {
    settings_listener_ = std::move(listener);
}

void BaseViewModel::settings_changed() {
    settings_version_subject_.set(settings_version_subject_.value() + 1);
    if (settings_listener_) {
        settings_listener_();
    }
}

void BaseViewModel::apply_settings(const model::LoraSettings& settings) {
    model_.lora() = settings;
    settings_version_subject_.set(settings_version_subject_.value() + 1);
    apply_radio_mode();
}

void BaseViewModel::cycle_band(int delta) {
    const auto& plans = decoder::band_plans();
    std::vector<std::string> names;
    for (const auto& p : plans) {
        names.emplace_back(p.name);
    }
    auto& lora = model_.lora();
    lora.band = cycle_option(names, lora.band, delta);
    // Keep Listen inside the new band.
    if (const auto* plan = decoder::find_band_plan(lora.band)) {
        if (lora.listen.freq_hz < plan->sweep_start_hz || lora.listen.freq_hz > plan->sweep_end_hz) {
            const auto mt = decoder::build_scan_targets(*plan, decoder::ScanProfile::Meshtastic);
            if (!mt.empty()) {
                lora.listen = mt.front().modem;
            } else if (!plan->uplinks.empty()) {
                lora.listen.freq_hz = plan->uplinks.front().freq_hz;
            }
        }
    }
    spectrum_zoom_ = 0;
    apply_radio_mode();
    settings_changed();
}

void BaseViewModel::cycle_profile(int delta) {
    auto& lora = model_.lora();
    const int n = static_cast<int>(decoder::ScanProfile::Count);
    lora.profile = static_cast<decoder::ScanProfile>(((static_cast<int>(lora.profile) + delta) % n + n) % n);
    apply_radio_mode();
    settings_changed();
}

void BaseViewModel::cycle_hunt_dwell(int delta) {
    auto& lora = model_.lora();
    lora.hunt_dwell_ms = cycle_option(model::hunt_dwell_options(), lora.hunt_dwell_ms, delta);
    apply_radio_mode();
    settings_changed();
}

void BaseViewModel::cycle_spectrum_step(int delta) {
    auto& lora = model_.lora();
    lora.spectrum_step_hz = cycle_option(model::spectrum_step_options(), lora.spectrum_step_hz, delta);
    apply_radio_mode();
    settings_changed();
}

void BaseViewModel::set_listen_frequency(uint32_t freq_hz) {
    model_.lora().listen.freq_hz = freq_hz;
    if (model_.current_page() == model::AppPage::Listen && radio_) {
        radio_->request_listen(model_.lora().listen);
    }
    settings_changed();
}

void BaseViewModel::set_listen_modem(const decoder::LoraModem& modem) {
    model_.lora().listen = modem;
    if (model_.current_page() == model::AppPage::Listen && radio_) {
        radio_->request_listen(model_.lora().listen);
    }
    settings_changed();
}

void BaseViewModel::tune_listen(int steps) {
    auto& lora = model_.lora();
    const int64_t next = static_cast<int64_t>(lora.listen.freq_hz) +
                         static_cast<int64_t>(steps) * static_cast<int64_t>(lora.listen_step_hz);
    set_listen_frequency(static_cast<uint32_t>(std::clamp<int64_t>(next, 150000000, 960000000)));
}

void BaseViewModel::cycle_listen_step(int delta) {
    auto& lora = model_.lora();
    lora.listen_step_hz = cycle_option(model::listen_step_options(), lora.listen_step_hz, delta);
    settings_changed();
}

void BaseViewModel::cycle_listen_sf(int delta) {
    auto m = model_.lora().listen;
    m.spreading_factor = static_cast<uint8_t>(std::clamp(m.spreading_factor + delta, 5, 12));
    set_listen_modem(m);
}

void BaseViewModel::cycle_listen_bw(int delta) {
    auto m = model_.lora().listen;
    m.bandwidth_hz = cycle_option(model::bandwidth_options(), m.bandwidth_hz, delta);
    set_listen_modem(m);
}

void BaseViewModel::cycle_listen_cr(int delta) {
    auto m = model_.lora().listen;
    m.coding_rate = static_cast<uint8_t>(((m.coding_rate - 5 + delta) % 4 + 4) % 4 + 5);
    set_listen_modem(m);
}

void BaseViewModel::cycle_listen_sync(int delta) {
    auto m = model_.lora().listen;
    m.sync_word = cycle_option(model::sync_word_options(), m.sync_word, delta);
    m.preamble = m.sync_word == 0x2B ? 16 : 8;
    set_listen_modem(m);
}

void BaseViewModel::toggle_listen_iq() {
    auto m = model_.lora().listen;
    m.iq_inverted = !m.iq_inverted;
    m.crc_on = !m.iq_inverted;  // LoRaWAN downlinks carry no payload CRC
    set_listen_modem(m);
}

} // namespace viewmodel
