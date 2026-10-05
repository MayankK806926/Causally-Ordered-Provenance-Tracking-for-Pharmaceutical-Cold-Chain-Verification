/// @file hashing.cpp
/// @brief SHA-256 wrapper using picosha2 single-header library.

#include "coldchain/hashing.hpp"

#include "picosha2.h"

#include <string>

namespace coldchain {

Hash256 sha256_bytes(std::string_view data) {
    Hash256 digest{};
    picosha2::hash256(data.begin(), data.end(), digest.begin(), digest.end());
    return digest;
}

std::string sha256_hex(std::string_view data) {
    return picosha2::hash256_hex_string(data.begin(), data.end());
}

Hash256 sign_event(std::string_view serialized_event,
                   std::string_view node_secret) {
    // MAC stand-in: SHA256(serialized || secret)
    std::string combined;
    combined.reserve(serialized_event.size() + node_secret.size());
    combined.append(serialized_event);
    combined.append(node_secret);
    return sha256_bytes(combined);
}

bool verify_signature(std::string_view serialized_event,
                      std::string_view node_secret,
                      const Hash256& expected_sig) {
    Hash256 computed = sign_event(serialized_event, node_secret);
    return computed == expected_sig;
}

std::string default_node_secret(uint8_t entity_id) {
    return "secret_node_" + std::to_string(static_cast<unsigned>(entity_id));
}

} // namespace coldchain
