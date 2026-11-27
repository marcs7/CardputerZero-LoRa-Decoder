/*
 * SPDX-FileCopyrightText: 2026 MarcS7 - IS0LTD
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SHA-256 and HMAC-SHA256 (FIPS 180-4 / RFC 2104), used by the MeshCore decoder.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace decoder {

void sha256(const uint8_t* data, std::size_t size, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, std::size_t key_size, const uint8_t* data, std::size_t size,
                 uint8_t out[32]);

} // namespace decoder
