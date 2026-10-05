/// @file events.cpp
/// @brief Serialization + accessor helpers for SensorEvent / CustodyEvent.

#include "coldchain/events.hpp"

#include <cstring>
#include <sstream>

namespace coldchain {

// ── SensorEvent ──────────────────────────────────────────────

std::string SensorEvent::serialize() const {
    std::ostringstream os;
    os << "SE|" << shipment_id
       << '|'   << static_cast<unsigned>(origin_entity_id)
       << '|'   << temperature_celsius
       << '|'   << latitude
       << '|'   << longitude
       << '|'   << local_monotonic_counter
       << '|'   << vc.to_string();
    return os.str();
}

// ── CustodyEvent ─────────────────────────────────────────────

std::string CustodyEvent::serialize() const {
    std::ostringstream os;
    os << "CE|" << shipment_id
       << '|'   << static_cast<unsigned>(from_entity_id)
       << '|'   << static_cast<unsigned>(to_entity_id)
       << '|'   << custody_transfer_seq
       << '|'   << vc.to_string();
    return os.str();
}

// ── Variant helpers ──────────────────────────────────────────

const VectorClock& event_vc(const LedgerEvent& ev) {
    return std::visit([](const auto& e) -> const VectorClock& { return e.vc; }, ev);
}

uint8_t event_origin(const LedgerEvent& ev) {
    return std::visit([](const auto& e) -> uint8_t {
        if constexpr (std::is_same_v<std::decay_t<decltype(e)>, SensorEvent>)
            return e.origin_entity_id;
        else
            return e.from_entity_id;
    }, ev);
}

uint64_t event_counter(const LedgerEvent& ev) {
    return std::visit([](const auto& e) -> uint64_t {
        if constexpr (std::is_same_v<std::decay_t<decltype(e)>, SensorEvent>)
            return e.local_monotonic_counter;
        else
            return e.custody_transfer_seq;
    }, ev);
}

uint64_t event_physical_time(const LedgerEvent& ev) {
    return std::visit([](const auto& e) -> uint64_t {
        if (e.physical_timestamp_ns != 0)
            return e.physical_timestamp_ns;
        if constexpr (std::is_same_v<std::decay_t<decltype(e)>, SensorEvent>)
            return e.local_monotonic_counter;
        else
            return e.custody_transfer_seq;
    }, ev);
}

std::string event_serialize(const LedgerEvent& ev) {
    return std::visit([](const auto& e) { return e.serialize(); }, ev);
}

} // namespace coldchain
