/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host integration test: RadioService + SimRadio + decoder, no LVGL.
 * Runs the hunt loop on EU868 for a while and checks packets of each
 * protocol were captured and decoded, then a spectrum sweep.
 */

#include "radio_service.h"
#include "sim_radio.h"

#include <cstdio>
#include <map>
#include <thread>

using namespace platform::radio;

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 25;
    RadioService service(std::make_unique<SimRadio>());
    std::string error;
    if (!service.start(error)) {
        std::printf("start failed: %s\n", error.c_str());
        return 1;
    }

    const auto* plan = decoder::find_band_plan("EU868");
    service.request_hunt(decoder::build_scan_targets(*plan, decoder::ScanProfile::All), {});
    std::map<std::string, int> by_proto;
    for (int i = 0; i < seconds * 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (auto& p : service.take_packets()) {
            by_proto[decoder::protocol_name(p.decoded.protocol)]++;
            std::printf("#%llu %-12s %s SF%u %6.1fdBm %5.1fdB | %s\n",
                        static_cast<unsigned long long>(p.seq), p.source.c_str(),
                        decoder::format_mhz(p.modem.freq_hz).c_str(), p.modem.spreading_factor,
                        p.rx.rssi_dbm, p.rx.snr_db, p.decoded.summary.c_str());
        }
    }
    auto st = service.status();
    std::printf("cycles=%u cad_hits=%u packets=%u crc=%u err='%s'\n", st.hunt_cycles, st.cad_hits,
                st.packets, st.crc_errors, st.error.c_str());

    const int hunt_packets = static_cast<int>(st.packets);

    // Listen on fixed channels: each protocol must be captured and decoded.
    const auto listen_for = [&](decoder::LoraModem m, const char* tag, int seconds_max) {
        service.request_listen(m);
        for (int i = 0; i < seconds_max * 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (auto& p : service.take_packets()) {
                if (p.source != "Listen") continue;  // drop in-flight hunt captures
                by_proto[std::string(tag) + "-" + decoder::protocol_name(p.decoded.protocol)]++;
                std::printf("%s %s\n", tag, p.decoded.summary.c_str());
                return;
            }
        }
    };
    decoder::LoraModem mt;  // Meshtastic EU LongFast
    mt.freq_hz = 869525000; mt.bandwidth_hz = 250000; mt.spreading_factor = 11;
    mt.sync_word = 0x2B; mt.preamble = 16;
    listen_for(mt, "mt", 25);
    decoder::LoraModem lw;  // LoRaWAN 868.1 SF7
    lw.freq_hz = 868100000; lw.spreading_factor = 7;
    listen_for(lw, "lw", 15);
    decoder::LoraModem prv = lw;  // private sensor 868.3 SF9 sync 0x12
    prv.freq_hz = 868300000; prv.spreading_factor = 9; prv.sync_word = 0x12;
    listen_for(prv, "prv", 15);

    service.request_spectrum({863000000, 870000000, 50000, 125000});
    SpectrumFrame frame;
    bool got = false;
    for (int i = 0; i < 50 && !got; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        got = service.take_spectrum(frame);
    }
    std::printf("spectrum: got=%d bins=%zu sweep=%ums\n", got, frame.dbm.size(), frame.sweep_ms);
    service.stop();

    for (auto& [k, v] : by_proto) std::printf("%s: %d\n", k.c_str(), v);
    const bool ok = got && hunt_packets > 0 && by_proto["mt-Meshtastic"] > 0 && by_proto["lw-LoRaWAN"] > 0 &&
                    by_proto["prv-Raw"] > 0;
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}
