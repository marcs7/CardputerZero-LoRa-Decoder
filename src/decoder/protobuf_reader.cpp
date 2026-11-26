/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "protobuf_reader.h"

#include <cstring>

namespace decoder {

float ProtoField::as_float() const {
    const auto bits = static_cast<uint32_t>(varint);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool ProtoReader::read_varint(uint64_t& value) {
    value = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (cur_ >= end_) {
            return false;
        }
        const uint8_t byte = *cur_++;
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
    }
    return false;
}

bool ProtoReader::next(ProtoField& field) {
    if (error_ || cur_ >= end_) {
        return false;
    }

    uint64_t key = 0;
    if (!read_varint(key)) {
        error_ = true;
        return false;
    }
    field = ProtoField{};
    field.number = static_cast<uint32_t>(key >> 3);
    const auto type = static_cast<uint8_t>(key & 0x07);
    if (field.number == 0 || field.number > 536870911U) {
        error_ = true;
        return false;
    }

    switch (type) {
        case 0:
            field.type = WireType::Varint;
            if (!read_varint(field.varint)) {
                error_ = true;
                return false;
            }
            return true;
        case 1:
            field.type = WireType::Fixed64;
            if (end_ - cur_ < 8) {
                error_ = true;
                return false;
            }
            for (int i = 7; i >= 0; --i) {
                field.varint = (field.varint << 8) | cur_[i];
            }
            cur_ += 8;
            return true;
        case 2: {
            field.type = WireType::LengthDelimited;
            uint64_t len = 0;
            if (!read_varint(len) || len > static_cast<uint64_t>(end_ - cur_)) {
                error_ = true;
                return false;
            }
            field.data = cur_;
            field.size = static_cast<std::size_t>(len);
            cur_ += len;
            return true;
        }
        case 5:
            field.type = WireType::Fixed32;
            if (end_ - cur_ < 4) {
                error_ = true;
                return false;
            }
            field.varint = static_cast<uint32_t>(cur_[0]) | (static_cast<uint32_t>(cur_[1]) << 8) |
                           (static_cast<uint32_t>(cur_[2]) << 16) |
                           (static_cast<uint32_t>(cur_[3]) << 24);
            cur_ += 4;
            return true;
        default:
            error_ = true;
            return false;
    }
}

bool looks_like_protobuf(const uint8_t* data, std::size_t size) {
    if (size == 0) {
        return false;
    }
    ProtoReader reader(data, size);
    ProtoField field;
    int count = 0;
    while (reader.next(field)) {
        if (field.number > 1000) {
            return false;
        }
        ++count;
    }
    return !reader.error() && reader.at_end() && count > 0;
}

} // namespace decoder
