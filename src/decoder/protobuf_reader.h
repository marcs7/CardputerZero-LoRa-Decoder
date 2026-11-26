/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tiny schema-less protobuf wire-format reader, enough to walk Meshtastic
 * messages without pulling in nanopb/libprotobuf.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace decoder {

enum class WireType : uint8_t {
    Varint = 0,
    Fixed64 = 1,
    LengthDelimited = 2,
    Fixed32 = 5,
};

struct ProtoField {
    uint32_t number{0};
    WireType type{WireType::Varint};
    uint64_t varint{0};      // Varint, Fixed32, Fixed64 values
    const uint8_t* data{nullptr};  // LengthDelimited payload
    std::size_t size{0};

    float as_float() const;
    int32_t as_sint32_fixed() const { return static_cast<int32_t>(static_cast<uint32_t>(varint)); }
    int64_t as_int64() const { return static_cast<int64_t>(varint); }
    std::string as_string() const { return std::string(reinterpret_cast<const char*>(data), size); }
};

class ProtoReader {
public:
    ProtoReader(const uint8_t* data, std::size_t size) : cur_(data), end_(data + size) {}

    // Returns false at clean end of buffer or on malformed input (check error()).
    bool next(ProtoField& field);
    bool error() const { return error_; }
    bool at_end() const { return cur_ == end_; }

private:
    bool read_varint(uint64_t& value);

    const uint8_t* cur_;
    const uint8_t* end_;
    bool error_{false};
};

// True when the whole buffer parses as protobuf wire format with sane field numbers.
bool looks_like_protobuf(const uint8_t* data, std::size_t size);

} // namespace decoder
