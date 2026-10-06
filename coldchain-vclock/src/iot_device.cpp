/// @file iot_device.cpp
/// @brief IoTDevice implementation — telemetry generation with blackout buffering.

#include "coldchain/iot_device.hpp"

namespace coldchain {

IoTDevice::IoTDevice(uint8_t entity_id, size_t n_nodes, uint32_t shipment_id)
    : entity_id_(entity_id)
    , shipment_id_(shipment_id)
    , local_vc_(n_nodes) {}

void IoTDevice::set_online(bool online) { is_online_ = online; }
bool IoTDevice::is_online() const       { return is_online_; }

SensorEvent IoTDevice::generate_telemetry(float temperature, float lat, float lon,
                                          uint64_t sim_time_ns,
                                          const DriftingPhysicalClock* phys_clock) {
    // 1. Tick local vector clock (causal ordering)
    local_vc_.tick(entity_id_);
    ++monotonic_counter_;

    // 2. Build event
    SensorEvent se;
    se.shipment_id            = shipment_id_;
    se.origin_entity_id       = entity_id_;
    se.temperature_celsius    = temperature;
    se.latitude               = lat;
    se.longitude              = lon;
    se.local_monotonic_counter = monotonic_counter_;
    se.vc                     = local_vc_;  // snapshot

    // Physical timestamp for naive baseline comparison
    if (phys_clock)
        se.physical_timestamp_ns = phys_clock->now_ns(sim_time_ns);
    else
        se.physical_timestamp_ns = sim_time_ns;

    // MAC-style signature
    se.signature = sign_event(se.serialize(), default_node_secret(entity_id_));

    // 3. If offline, buffer; otherwise caller transmits immediately
    if (!is_online_)
        buffer_.push_back(se);

    return se;
}

std::vector<SensorEvent> IoTDevice::flush_on_reconnect() {
    std::vector<SensorEvent> flushed;
    flushed.swap(buffer_);
    return flushed;
}

size_t IoTDevice::buffered_count() const { return buffer_.size(); }

const VectorClock& IoTDevice::local_clock() const    { return local_vc_; }
VectorClock&       IoTDevice::local_clock_mut()      { return local_vc_; }

uint8_t  IoTDevice::entity_id()   const { return entity_id_; }
uint32_t IoTDevice::shipment_id() const { return shipment_id_; }

} // namespace coldchain
