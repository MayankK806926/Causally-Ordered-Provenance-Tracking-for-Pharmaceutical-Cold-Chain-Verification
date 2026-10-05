#pragma once
/// @file events.hpp
/// @brief Immutable event structures for the cold-chain ledger.
///
/// Two event kinds exist:
///   - SensorEvent  : telemetry reading from an IoT device
///   - CustodyEvent : handoff record between supply-chain entities
///
/// Both carry a VectorClock snapshot instead of a physical timestamp.
/// The `signature` field is a MAC-style stand-in:
///   SHA256(serialize(event) || node_secret)
/// — proves which simulated node produced the event without real PKI.

#include <array>
#include <cstdint>
#include <string>
#include <variant>

#include "coldchain/vector_clock.hpp"

namespace coldchain {

/// 32-byte hash / signature type alias.
using Hash256 = std::array<uint8_t, 32>;

// ═══════════════════════════════════════════════════════════════
//  SensorEvent
// ═══════════════════════════════════════════════════════════════

struct SensorEvent {
    uint32_t    shipment_id;
    uint8_t     origin_entity_id;       ///< which node's IoT device
    float       temperature_celsius;
    float       latitude;
    float       longitude;
    uint64_t    local_monotonic_counter; ///< for physical-baseline pipeline
    VectorClock vc;
    Hash256     signature{};            ///< MAC-style simulated signature
    uint64_t    physical_timestamp_ns = 0; ///< simulated physical timestamp (for naive baseline)

    /// Deterministic serialization for hashing / signing.
    std::string serialize() const;
};

// ═══════════════════════════════════════════════════════════════
//  CustodyEvent
// ═══════════════════════════════════════════════════════════════

struct CustodyEvent {
    uint32_t    shipment_id;
    uint8_t     from_entity_id;
    uint8_t     to_entity_id;
    uint64_t    custody_transfer_seq;
    VectorClock vc;
    Hash256     transfer_signature{};
    uint64_t    physical_timestamp_ns = 0; ///< simulated physical timestamp (for naive baseline)

    /// Deterministic serialization for hashing / signing.
    std::string serialize() const;
};

// ═══════════════════════════════════════════════════════════════
//  LedgerEvent — tagged union of the two event kinds
// ═══════════════════════════════════════════════════════════════

using LedgerEvent = std::variant<SensorEvent, CustodyEvent>;

/// Extract the vector clock from any event variant.
const VectorClock& event_vc(const LedgerEvent& ev);

/// Extract entity_id that originated the event (for tie-breaking).
uint8_t event_origin(const LedgerEvent& ev);

/// Extract monotonic counter (for deterministic tie-breaking of concurrent events).
uint64_t event_counter(const LedgerEvent& ev);

/// Extract physical timestamp (or falls back to monotonic counter).
uint64_t event_physical_time(const LedgerEvent& ev);

/// Deterministic serialization of any event variant.
std::string event_serialize(const LedgerEvent& ev);

} // namespace coldchain
