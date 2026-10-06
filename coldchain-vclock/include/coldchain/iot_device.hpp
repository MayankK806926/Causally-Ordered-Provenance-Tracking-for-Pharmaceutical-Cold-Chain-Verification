#pragma once
/// @file iot_device.hpp
/// @brief Simulated IoT sensor device attached to a shipment.
///
/// Each device belongs to one supply-chain entity and generates telemetry
/// readings with vector-clock timestamps.  During blackouts, events are
/// buffered locally and flushed on reconnect — preserving causal metadata
/// even though physical delivery is delayed.

#include <cstdint>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

class IoTDevice {
public:
    /// @param entity_id     Index in [0, n_nodes) — which supply-chain entity owns this device.
    /// @param n_nodes       Total number of entities (vector clock dimension).
    /// @param shipment_id   The shipment this device is tracking.
    IoTDevice(uint8_t entity_id, size_t n_nodes, uint32_t shipment_id);

    // ── Connectivity ──────────────────────────────────────────

    /// Set online / offline.  Offline ⇒ generated events are buffered.
    void set_online(bool online);
    bool is_online() const;

    // ── Telemetry generation ──────────────────────────────────

    /// Generate a sensor reading.  If offline, the event is buffered
    /// internally.  The returned event always has a valid vector-clock
    /// snapshot regardless of connectivity.
    ///
    /// @param temperature  Measured temperature in °C.
    /// @param lat, lon     GPS coordinates.
    /// @param sim_time_ns  Current simulation time (for physical baseline stamp).
    /// @param phys_clock   Optional drifting clock for physical timestamp.
    SensorEvent generate_telemetry(float temperature, float lat, float lon,
                                   uint64_t sim_time_ns = 0,
                                   const DriftingPhysicalClock* phys_clock = nullptr);

    // ── Reconnect flush ───────────────────────────────────────

    /// Return all buffered events accumulated during the blackout and
    /// clear the internal buffer.  Events retain their original
    /// vector-clock snapshots — they are NOT re-timestamped on flush.
    std::vector<SensorEvent> flush_on_reconnect();

    /// How many events are currently buffered (pending flush).
    size_t buffered_count() const;

    // ── Clock access ──────────────────────────────────────────

    /// Current vector clock of this device (const ref).
    const VectorClock& local_clock() const;

    /// Mutable reference — used by NetworkSimulator to merge incoming VCs
    /// (e.g. after receiving a message from another entity).
    VectorClock& local_clock_mut();

    uint8_t  entity_id()   const;
    uint32_t shipment_id() const;

private:
    uint8_t  entity_id_;
    uint32_t shipment_id_;
    VectorClock local_vc_;
    std::vector<SensorEvent> buffer_;
    bool is_online_ = true;
    uint64_t monotonic_counter_ = 0;
};

} // namespace coldchain
