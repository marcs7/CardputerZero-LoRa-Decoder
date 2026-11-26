/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal AES-128/256 block encryption plus CTR mode.
 * Only the forward cipher is needed: Meshtastic payloads use AES-CTR.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace decoder {

class Aes {
public:
    // key_len must be 16 (AES-128) or 32 (AES-256). Returns false otherwise.
    bool set_key(const uint8_t* key, std::size_t key_len);
    void encrypt_block(const uint8_t in[16], uint8_t out[16]) const;

private:
    std::array<uint8_t, 240> round_keys_{};
    int rounds_{0};
};

// AES-CTR with a 16-byte initial counter block incremented big-endian
// across the whole block (mbedTLS/ESP-IDF semantics used by Meshtastic).
// Encryption and decryption are the same operation.
bool aes_ctr_xcrypt(const uint8_t* key,
                    std::size_t key_len,
                    const uint8_t nonce[16],
                    const uint8_t* in,
                    std::size_t len,
                    uint8_t* out);

} // namespace decoder
