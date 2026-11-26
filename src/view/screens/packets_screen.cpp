/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "packets_screen.h"

#include "bindings.h"
#include "screen_utils.h"

#include <cstdio>
#include <string>

namespace screen {
namespace {

std::string hex_dump(const std::vector<uint8_t>& data) {
    std::string out;
    char line[96];
    for (std::size_t off = 0; off < data.size(); off += 8) {
        std::snprintf(line, sizeof(line), "%03zX ", off);
        out += line;
        for (std::size_t i = off; i < off + 8 && i < data.size(); ++i) {
            std::snprintf(line, sizeof(line), "%02X ", data[i]);
            out += line;
        }
        out += " ";
        out += decoder::to_printable(data.data() + off, std::min<std::size_t>(8, data.size() - off));
        out += "\n";
    }
    return out;
}

} // namespace

PacketsScreen::PacketsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {
    init();
}

void PacketsScreen::build_content(lv_obj_t* content) {
    auto& vm = view_model();
    const auto* small = ui::font(assets(), 12);
    const auto* tiny = ui::font(assets(), 10);

    header_label_ = ui::label(content, tiny, 6, 1, 308);
    reactive::bind_theme(header_label_, vm.dark_mode_subject(), reactive::ThemeRole::Text);

    for (std::size_t i = 0; i < kRows; ++i) {
        auto* row = ui::panel(content, 2, 14 + static_cast<int32_t>(i) * 16, 316, 16);
        lv_obj_set_style_radius(row, 3, 0);
        row_panels_[i] = row;
        tag_labels_[i] = ui::label(row, tiny, 4, 2, 26);
        row_labels_[i] = ui::label(row, small, 30, 1, 284, LV_LABEL_LONG_DOT);
    }

    detail_ = lv_obj_create(content);
    lv_obj_remove_style_all(detail_);
    lv_obj_set_size(detail_, 320, 110);
    lv_obj_set_pos(detail_, 0, 0);
    lv_obj_set_style_bg_opa(detail_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(detail_, 6, 0);
    lv_obj_set_style_pad_ver(detail_, 2, 0);
    lv_obj_set_scroll_dir(detail_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(detail_, LV_SCROLLBAR_MODE_ACTIVE);
    detail_label_ = lv_label_create(detail_);
    lv_obj_set_width(detail_label_, 300);
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(detail_label_, small, 0);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

    reactive::observe_obj(content, vm.packets_version_subject(), refresh_cb, this);
    reactive::observe_obj(content, vm.dark_mode_subject(), refresh_cb, this);
    if (!vm.packets().empty()) {
        selected_seq_ = vm.packets().front().seq;
    }
    refresh();
}

int PacketsScreen::selected_index() {
    const auto& packets = view_model().packets();
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (packets[i].seq == selected_seq_) {
            return static_cast<int>(i);
        }
    }
    return packets.empty() ? -1 : 0;
}

void PacketsScreen::refresh() {
    auto& vm = view_model();
    const auto colors = view::palette(vm.is_dark_mode());
    const auto& packets = vm.packets();

    lv_obj_set_style_bg_color(detail_, colors.surface, 0);
    lv_obj_set_style_text_color(detail_label_, colors.text, 0);

    if (follow_newest_ && !packets.empty()) {
        selected_seq_ = packets.front().seq;
    }
    int sel = selected_index();
    if (sel >= 0) {
        selected_seq_ = packets[static_cast<std::size_t>(sel)].seq;
    }
    if (sel >= 0) {
        if (static_cast<std::size_t>(sel) < top_) top_ = static_cast<std::size_t>(sel);
        if (static_cast<std::size_t>(sel) >= top_ + kRows) top_ = static_cast<std::size_t>(sel) - kRows + 1;
    } else {
        top_ = 0;
    }

    if (packets.empty()) {
        lv_label_set_text(header_label_, "No packets yet - start Hunt (5) or Listen (6)");
    } else {
        lv_label_set_text_fmt(header_label_, "%u of %u   ENTER detail   L listen   E clear",
                              static_cast<unsigned>(sel + 1), static_cast<unsigned>(packets.size()));
    }

    for (std::size_t i = 0; i < kRows; ++i) {
        const std::size_t idx = top_ + i;
        auto* row = row_panels_[i];
        if (idx >= packets.size()) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
        const auto& rec = packets[idx];
        const bool selected = static_cast<int>(idx) == sel;
        lv_obj_set_style_bg_color(row, colors.primary, 0);
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_label_set_text(tag_labels_[i], ui::protocol_tag(rec.decoded.protocol));
        lv_obj_set_style_text_color(tag_labels_[i], ui::protocol_color(rec.decoded.protocol, colors), 0);
        lv_label_set_text(row_labels_[i], ui::packet_row(rec).c_str());
        lv_obj_set_style_text_color(row_labels_[i], colors.text, 0);
        lv_obj_set_style_text_opa(row_labels_[i], rec.rx.crc_error ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
}

void PacketsScreen::open_detail() {
    const auto& packets = view_model().packets();
    const int sel = selected_index();
    if (sel < 0) {
        return;
    }
    const auto& rec = packets[static_cast<std::size_t>(sel)];
    char buf[160];
    std::string text;
    std::snprintf(buf, sizeof(buf), "#%llu  %s  %s\n", static_cast<unsigned long long>(rec.seq),
                  decoder::protocol_name(rec.decoded.protocol), ui::time_hms(rec.time).c_str());
    text += buf;
    std::snprintf(buf, sizeof(buf), "%s  CR4/%u  sync %02X%s\n", ui::modem_short(rec.modem).c_str(),
                  rec.modem.coding_rate, rec.modem.sync_word, rec.modem.iq_inverted ? "  IQinv" : "");
    text += buf;
    std::snprintf(buf, sizeof(buf), "RSSI %.1f  SNR %.2f  %zu B  %s\n", rec.rx.rssi_dbm, rec.rx.snr_db,
                  rec.rx.data.size(), rec.rx.crc_error ? "CRC ERROR" : "CRC ok");
    text += buf;
    text += "via " + rec.source + "\n\n";
    for (const auto& f : rec.decoded.fields) {
        text += f.key + ": " + f.value + "\n";
    }
    text += "\n" + hex_dump(rec.rx.data);
    lv_label_set_text(detail_label_, text.c_str());
    lv_obj_scroll_to_y(detail_, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(detail_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(detail_);
}

void PacketsScreen::close_detail() {
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);
}

bool PacketsScreen::handle_key(uint32_t key, bool long_pressed) {
    auto& vm = view_model();
    const bool detail_open = !lv_obj_has_flag(detail_, LV_OBJ_FLAG_HIDDEN);
    if (detail_open) {
        switch (key) {
            case LV_KEY_UP: lv_obj_scroll_by_bounded(detail_, 0, long_pressed ? 90 : 30, LV_ANIM_ON); return true;
            case LV_KEY_DOWN: lv_obj_scroll_by_bounded(detail_, 0, long_pressed ? -90 : -30, LV_ANIM_ON); return true;
            case LV_KEY_ESC:
            case LV_KEY_ENTER:
            case LV_KEY_BACKSPACE:
                if (!long_pressed) close_detail();
                return !long_pressed;  // let long ESC reach exit handling
            default: return false;
        }
    }

    const auto& packets = vm.packets();
    const int sel = selected_index();
    const int step = long_pressed ? 5 : 1;
    switch (key) {
        case LV_KEY_UP:
            if (sel > 0) selected_seq_ = packets[static_cast<std::size_t>(std::max(0, sel - step))].seq;
            follow_newest_ = selected_index() == 0;
            refresh();
            return true;
        case LV_KEY_DOWN:
            if (sel >= 0 && sel + 1 < static_cast<int>(packets.size()))
                selected_seq_ = packets[static_cast<std::size_t>(std::min<int>(sel + step, static_cast<int>(packets.size()) - 1))].seq;
            follow_newest_ = selected_index() == 0;
            refresh();
            return true;
        default:
            break;
    }
    if (long_pressed) {
        return false;
    }
    switch (key) {
        case LV_KEY_ENTER:
            open_detail();
            return true;
        case 'e':
        case 'E':
            vm.clear_packets();
            selected_seq_ = 0;
            top_ = 0;
            follow_newest_ = true;
            return true;
        case 'l':
        case 'L':
            if (sel >= 0) {
                vm.set_listen_modem(packets[static_cast<std::size_t>(sel)].modem);
                vm.show_page(model::AppPage::Listen);
            }
            return true;
        default:
            return false;
    }
}

void PacketsScreen::refresh_cb(lv_observer_t* observer, lv_subject_t*) {
    auto* self = static_cast<PacketsScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->refresh();
    }
}

} // namespace screen
