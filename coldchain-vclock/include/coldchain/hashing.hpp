#pragma once
/// @file hashing.hpp
/// @brief Thin SHA-256 wrapper around picosha2.
///
/// Provides:
///   sha256_bytes(data) → Hash256   (raw 32-byte digest)
///   sha256_hex(data)   → string    (64-char hex string)
///   sign_event(serialized, node_secret) → Hash256  (MAC stand-in)

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "coldchain/events.hpp"   // Hash256

namespace coldchain {

/// Compute SHA-256 of arbitrary byte string, return 32 raw bytes.
Hash256 sha256_bytes(std::string_view data);

/// Compute SHA-256 of arbitrary byte string, return 64-char hex.
std::string sha256_hex(std::string_view data);

/// MAC-style simulated signature: SHA256(serialized_event || node_secret).
/// Not real asymmetric crypto — sufficient for proving event origin in simulation.
Hash256 sign_event(std::string_view serialized_event,
                   std::string_view node_secret);

/// Verify a MAC-style signature.
bool verify_signature(std::string_view serialized_event,
                      std::string_view node_secret,
                      const Hash256& expected_sig);

/// Default simulated node secret: "secret_node_<id>".
std::string default_node_secret(uint8_t entity_id);

} // namespace coldchain
