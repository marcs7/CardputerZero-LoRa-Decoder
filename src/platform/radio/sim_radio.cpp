/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sim_radio.h"

#include "meshtastic.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace platform::radio {
namespace {

using decoder::LoraModem;

LoraModem modem(uint32_t freq, uint32_t bw, uint8_t sf, uint8_t cr, uint8_t sync,
                uint16_t preamble, bool iq_inv = false) {
    LoraModem m;
    m.freq_hz = freq;
    m.bandwidth_hz = bw;
    m.spreading_factor = sf;
    m.coding_rate = cr;
    m.sync_word = sync;
    m.preamble = preamble;
    m.iq_inverted = iq_inv;
    m.crc_on = !iq_inv;
    return m;
}

void put_varint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

void put_bytes_field(std::vector<uint8_t>& out, uint32_t field, const std::vector<uint8_t>& bytes) {
    put_varint(out, (field << 3) | 2);
    put_varint(out, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void put_string_field(std::vector<uint8_t>& out, uint32_t field, const std::string& s) {
    put_bytes_field(out, field, std::vector<uint8_t>(s.begin(), s.end()));
}

void put_fixed32_field(std::vector<uint8_t>& out, uint32_t field, uint32_t v) {
    put_varint(out, (field << 3) | 5);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put_float_field(std::vector<uint8_t>& out, uint32_t field, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    put_fixed32_field(out, field, bits);
}

void put_varint_field(std::vector<uint8_t>& out, uint32_t field, uint64_t v) {
    put_varint(out, field << 3);
    put_varint(out, v);
}

std::vector<uint8_t> meshtastic_frame(uint32_t from, uint32_t to, uint32_t id,
                                      const std::string& channel, uint32_t portnum,
                                      const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> data;
    put_varint_field(data, 1, portnum);
    put_bytes_field(data, 2, payload);

    decoder::meshtastic::ChannelKey key{channel, decoder::meshtastic::default_key()};
    std::vector<uint8_t> frame(16);
    for (int i = 0; i < 4; ++i) {
        frame[i] = static_cast<uint8_t>(to >> (8 * i));
        frame[4 + i] = static_cast<uint8_t>(from >> (8 * i));
        frame[8 + i] = static_cast<uint8_t>(id >> (8 * i));
    }
    frame[12] = static_cast<uint8_t>((3 << 5) | 3);
    frame[13] = key.hash();
    decoder::meshtastic::Header header;
    decoder::meshtastic::parse_header(frame.data(), frame.size(), header);
    std::vector<uint8_t> cipher;
    decoder::meshtastic::decrypt(header, key.key, data.data(), data.size(), cipher);
    frame.insert(frame.end(), cipher.begin(), cipher.end());
    return frame;
}

std::vector<uint8_t> lorawan_uplink(uint32_t dev_addr, uint16_t fcnt, uint8_t fport,
                                    std::size_t payload_len, std::mt19937& rng, bool confirmed) {
    std::vector<uint8_t> f;
    f.push_back(confirmed ? 0x80 : 0x40);
    for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(dev_addr >> (8 * i)));
    f.push_back(0x80);  // ADR
    f.push_back(static_cast<uint8_t>(fcnt));
    f.push_back(static_cast<uint8_t>(fcnt >> 8));
    f.push_back(fport);
    for (std::size_t i = 0; i < payload_len + 4; ++i) f.push_back(static_cast<uint8_t>(rng()));
    return f;
}

} // namespace

SimRadio::SimRadio() : rng_(0xC0FFEE) {}

bool SimRadio::begin(std::string& error) {
    error.clear();
    transmitters_.clear();
    const uint8_t mt_sync = decoder::meshtastic::kSyncWord;

    // --- Meshtastic node on EU868 LongFast (869.525 MHz) ---
    {
        auto counter = std::make_shared<uint32_t>(0);
        auto rng = &rng_;
        transmitters_.push_back({modem(869525000, 250000, 11, 5, mt_sync, 16), 9000, -92.0f, 6.5f,
                                 [counter, rng]() {
                                     const uint32_t n = (*counter)++;
                                     const uint32_t from = 0xa1b2c3d4;
                                     const uint32_t id = 0x1000 + (*rng)() % 0xFFFF;
                                     std::vector<uint8_t> payload;
                                     switch (n % 4) {
                                         case 0: {
                                             const std::string text = "ciao dal Cardputer #" + std::to_string(n);
                                             payload.assign(text.begin(), text.end());
                                             return meshtastic_frame(from, 0xFFFFFFFF, id, "LongFast", 1, payload);
                                         }
                                         case 1:
                                             put_fixed32_field(payload, 1, static_cast<uint32_t>(454642040));
                                             put_fixed32_field(payload, 2, static_cast<uint32_t>(91899820));
                                             put_varint_field(payload, 3, 122);
                                             put_varint_field(payload, 19, 9);
                                             return meshtastic_frame(from, 0xFFFFFFFF, id, "LongFast", 3, payload);
                                         case 2:
                                             put_string_field(payload, 1, "!a1b2c3d4");
                                             put_string_field(payload, 2, "Milano Base");
                                             put_string_field(payload, 3, "MIB");
                                             put_varint_field(payload, 5, 43);
                                             return meshtastic_frame(from, 0xFFFFFFFF, id, "LongFast", 4, payload);
                                         default: {
                                             std::vector<uint8_t> metrics;
                                             put_varint_field(metrics, 1, 87);
                                             put_float_field(metrics, 2, 4.02f);
                                             put_float_field(metrics, 3, 7.5f);
                                             put_bytes_field(payload, 2, metrics);
                                             return meshtastic_frame(from, 0xFFFFFFFF, id, "LongFast", 67, payload);
                                         }
                                     }
                                 }});
    }
    // --- Meshtastic node on US LongFast (906.875 MHz) ---
    {
        auto counter = std::make_shared<uint32_t>(0);
        transmitters_.push_back({modem(906875000, 250000, 11, 5, mt_sync, 16), 11000, -101.0f, 2.0f,
                                 [counter]() {
                                     const std::string text = "hello from US mesh " + std::to_string((*counter)++);
                                     return meshtastic_frame(0x5e5e1234, 0xFFFFFFFF, 0xABC0 + *counter,
                                                             "LongFast", 1,
                                                             std::vector<uint8_t>(text.begin(), text.end()));
                                 }});
    }
    // --- LoRaWAN end devices (EU868) ---
    const uint32_t eu_channels[] = {868100000, 868300000, 868500000};
    const uint8_t eu_sfs[] = {7, 9, 10};
    for (int i = 0; i < 3; ++i) {
        auto fcnt = std::make_shared<uint16_t>(static_cast<uint16_t>(100 * (i + 1)));
        auto rng = &rng_;
        const uint32_t dev_addr = 0x260B1200U + static_cast<uint32_t>(i);
        transmitters_.push_back({modem(eu_channels[i], 125000, eu_sfs[i], 5, decoder::kSyncLoRaWANPublic, 8),
                                 static_cast<uint32_t>(5000 + 2500 * i), -85.0f - 8.0f * i, 7.0f - 4.0f * i,
                                 [fcnt, rng, dev_addr, i]() {
                                     return lorawan_uplink(dev_addr, (*fcnt)++, static_cast<uint8_t>(1 + i),
                                                           static_cast<std::size_t>(6 + 3 * i), *rng, i == 2);
                                 }});
    }
    // --- LoRaWAN join request (EU868, SF12) ---
    transmitters_.push_back({modem(867100000, 125000, 12, 5, decoder::kSyncLoRaWANPublic, 8), 30000,
                             -110.0f, -9.5f, [this]() {
                                 std::vector<uint8_t> f = {0x00};
                                 const uint8_t join_eui[8] = {0xDC, 0x00, 0x00, 0xD0, 0x7E, 0xD5, 0xB3, 0x70};
                                 const uint8_t dev_eui[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
                                 f.insert(f.end(), join_eui, join_eui + 8);
                                 f.insert(f.end(), dev_eui, dev_eui + 8);
                                 f.push_back(static_cast<uint8_t>(rng_()));
                                 f.push_back(static_cast<uint8_t>(rng_()));
                                 for (int i = 0; i < 4; ++i) f.push_back(static_cast<uint8_t>(rng_()));
                                 return f;
                             }});
    // --- LoRaWAN US915 sub-band 2 device ---
    {
        auto fcnt = std::make_shared<uint16_t>(42);
        auto rng = &rng_;
        transmitters_.push_back({modem(904300000, 125000, 9, 5, decoder::kSyncLoRaWANPublic, 8), 7000,
                                 -97.0f, 3.0f, [fcnt, rng]() {
                                     return lorawan_uplink(0x00ABCDEF, (*fcnt)++, 2, 11, *rng, false);
                                 }});
    }
    // --- Private-network sensor, plain ASCII, sync 0x12 ---
    {
        auto n = std::make_shared<int>(0);
        transmitters_.push_back({modem(868300000, 125000, 9, 5, decoder::kSyncPrivate, 8), 6000, -99.0f,
                                 1.5f, [n]() {
                                     const std::string s = "T=21." + std::to_string((*n)++ % 10) + "C H=48%";
                                     return std::vector<uint8_t>(s.begin(), s.end());
                                 }});
    }

    const auto now = Clock::now();
    for (auto& tx : transmitters_) {
        schedule(tx, now + std::chrono::milliseconds(rng_() % tx.period_ms));
    }
    return true;
}

void SimRadio::schedule(Transmitter& tx, Clock::time_point at) {
    tx.frame = tx.generate();
    tx.start = at;
    const double toa = decoder::time_on_air_ms(tx.modem, static_cast<uint32_t>(tx.frame.size()));
    tx.end = at + std::chrono::microseconds(static_cast<int64_t>(toa * 1000.0));
}

void SimRadio::advance(Clock::time_point now) {
    for (std::size_t i = 0; i < transmitters_.size(); ++i) {
        auto& tx = transmitters_[i];
        if (static_cast<int>(i) == locked_tx_ || now <= tx.end) {
            continue;
        }
        const auto jitter = static_cast<int64_t>(rng_() % (tx.period_ms / 4 + 1));
        schedule(tx, tx.end + std::chrono::milliseconds(tx.period_ms - tx.period_ms / 8 + jitter));
    }
}

bool SimRadio::matches(const Transmitter& tx) const {
    const int64_t df = static_cast<int64_t>(tx.modem.freq_hz) - static_cast<int64_t>(modem_.freq_hz);
    return std::llabs(df) < static_cast<int64_t>(modem_.bandwidth_hz / 4) &&
           tx.modem.bandwidth_hz == modem_.bandwidth_hz &&
           tx.modem.spreading_factor == modem_.spreading_factor &&
           tx.modem.sync_word == modem_.sync_word && tx.modem.iq_inverted == modem_.iq_inverted;
}

RxPacket SimRadio::make_packet(const Transmitter& tx) {
    RxPacket p;
    p.data = tx.frame;
    std::normal_distribution<float> noise(0.0f, 1.5f);
    p.rssi_dbm = std::round((tx.rssi_dbm + noise(rng_)) * 2.0f) / 2.0f;
    p.snr_db = std::round((tx.snr_db + noise(rng_) * 0.5f) * 4.0f) / 4.0f;
    p.signal_rssi_dbm = p.rssi_dbm + 1.0f;
    p.crc_error = (rng_() % 40) == 0;
    return p;
}

bool SimRadio::configure(const LoraModem& m, std::string& error) {
    error.clear();
    modem_ = m;
    mode_ = Mode::Standby;
    locked_tx_ = -1;
    return true;
}

bool SimRadio::set_frequency(uint32_t freq_hz, std::string& error) {
    error.clear();
    modem_.freq_hz = freq_hz;
    return true;
}

bool SimRadio::start_rx(uint32_t timeout_ms, std::string& error) {
    error.clear();
    mode_ = Mode::Rx;
    mode_since_ = Clock::now();
    rx_has_deadline_ = timeout_ms != 0;
    rx_deadline_ = mode_since_ + std::chrono::milliseconds(timeout_ms);
    locked_tx_ = -1;
    return true;
}

bool SimRadio::start_cad(uint32_t rx_timeout_ms, std::string& error) {
    error.clear();
    mode_ = Mode::Cad;
    mode_since_ = Clock::now();
    const double symbol_ms = std::ldexp(1.0, modem_.spreading_factor) / modem_.bandwidth_hz * 1000.0;
    cad_done_at_ = mode_since_ + std::chrono::microseconds(static_cast<int64_t>((2 * symbol_ms + 0.3) * 1000));
    rx_deadline_ = cad_done_at_ + std::chrono::milliseconds(rx_timeout_ms);
    rx_has_deadline_ = true;
    locked_tx_ = -1;
    return true;
}

bool SimRadio::standby(std::string& error) {
    error.clear();
    mode_ = Mode::Standby;
    locked_tx_ = -1;
    return true;
}

RadioEvent SimRadio::poll(RxPacket& packet, std::string& error) {
    error.clear();
    const auto now = Clock::now();
    RadioEvent event = RadioEvent::None;

    if (mode_ == Mode::Cad) {
        if (now >= cad_done_at_) {
            for (std::size_t i = 0; i < transmitters_.size(); ++i) {
                const auto& tx = transmitters_[i];
                if (matches(tx) && tx.start <= now && now < tx.end) {
                    locked_tx_ = static_cast<int>(i);
                    break;
                }
            }
            if (locked_tx_ >= 0) {
                mode_ = Mode::Rx;
                event = RadioEvent::CadBusy;
            } else {
                mode_ = Mode::Standby;
                event = RadioEvent::CadClear;
            }
        }
    } else if (mode_ == Mode::Rx) {
        if (locked_tx_ < 0) {
            for (std::size_t i = 0; i < transmitters_.size(); ++i) {
                const auto& tx = transmitters_[i];
                if (matches(tx) && tx.start >= mode_since_ && tx.start <= now) {
                    locked_tx_ = static_cast<int>(i);
                    break;
                }
            }
        }
        if (locked_tx_ >= 0) {
            const auto& tx = transmitters_[static_cast<std::size_t>(locked_tx_)];
            if (now >= tx.end) {
                packet = make_packet(tx);
                locked_tx_ = -1;
                if (rx_has_deadline_) {
                    mode_ = Mode::Standby;
                }
                mode_since_ = now;
                event = RadioEvent::RxDone;
            }
        } else if (rx_has_deadline_ && now >= rx_deadline_) {
            mode_ = Mode::Standby;
            event = RadioEvent::Timeout;
        }
    }

    advance(now);
    return event;
}

bool SimRadio::read_rssi(float& dbm, std::string& error) {
    error.clear();
    std::uniform_real_distribution<float> noise(-2.5f, 2.5f);
    float level = -121.0f + noise(rng_);
    const auto now = Clock::now();
    for (const auto& tx : transmitters_) {
        if (now < tx.start || now >= tx.end) {
            continue;
        }
        const double offset = std::fabs(static_cast<double>(tx.modem.freq_hz) - modem_.freq_hz);
        const double half = (tx.modem.bandwidth_hz + modem_.bandwidth_hz) / 2.0;
        if (offset < half) {
            const float attenuation = static_cast<float>(offset / half * 12.0);
            level = std::max(level, tx.rssi_dbm - attenuation + noise(rng_));
        }
    }
    dbm = level;
    return true;
}

} // namespace platform::radio
