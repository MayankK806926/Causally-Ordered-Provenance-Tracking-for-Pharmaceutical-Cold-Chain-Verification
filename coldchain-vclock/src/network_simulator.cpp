/// @file network_simulator.cpp
/// @brief Discrete-event network simulator implementation.
///
/// Generates telemetry, handles custody transfers, injects blackouts,
/// applies transmission jitter, and feeds events to both pipelines.

#include "coldchain/network_simulator.hpp"
#include "coldchain/causal_merge_engine.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace coldchain {

// ══════════════════════════════════════════════════════════════
//  Construction
// ══════════════════════════════════════════════════════════════

NetworkSimulator::NetworkSimulator(const SimConfig& cfg)
    : cfg_(cfg), rng_(cfg.seed) {
    // Create one device, physical clock, and ledger node per entity.
    // Devices start with shipment_id=0 (placeholder) — actual shipments
    // are set up via add_shipment().
    for (size_t i = 0; i < cfg_.num_nodes; ++i) {
        devices_.emplace_back(static_cast<uint8_t>(i), cfg_.num_nodes, /*shipment*/ 0);

        // Alternate positive/negative drift to stress the naive baseline
        double mean_ppm = (i % 2 == 0) ? cfg_.drift_ppm_mean : -cfg_.drift_ppm_mean;
        phys_clocks_.emplace_back(mean_ppm, cfg_.drift_ppm_stddev, cfg_.seed + i * 100);

        vclock_nodes_.emplace_back(static_cast<uint8_t>(i), cfg_.num_nodes);
        naive_nodes_.emplace_back(static_cast<uint8_t>(i), cfg_.num_nodes);
    }
}

// ══════════════════════════════════════════════════════════════
//  Shipment management
// ══════════════════════════════════════════════════════════════

void NetworkSimulator::add_shipment(uint32_t shipment_id, std::vector<uint8_t> route) {
    assert(!route.empty());

    Shipment ship;
    ship.shipment_id    = shipment_id;
    ship.route          = std::move(route);
    ship.current_leg    = 0;
    ship.next_sample_ms = sim_clock_ms_;  // start sampling immediately

    // Duration per leg: evenly split total sim time across legs
    double total_ms = cfg_.duration_hours * 3600.0 * 1000.0;
    double per_leg  = total_ms / static_cast<double>(ship.route.size());
    ship.leg_end_ms = sim_clock_ms_ + per_leg;

    shipments_.push_back(ship);
}

// ══════════════════════════════════════════════════════════════
//  Blackout injection
// ══════════════════════════════════════════════════════════════

void NetworkSimulator::inject_blackout(uint8_t node_id, double duration_ms) {
    BlackoutEvent be;
    be.start_ms = sim_clock_ms_;
    be.end_ms   = sim_clock_ms_ + duration_ms;
    be.node_id  = node_id;
    blackouts_.push_back(be);
    ++total_blackouts_;
}

void NetworkSimulator::generate_random_blackouts() {
    if (cfg_.blackout_rate_per_hour <= 0.0 || cfg_.duration_hours <= 0.0) {
        return;
    }

    // Model blackout arrivals as Poisson process per node
    double total_ms = cfg_.duration_hours * 3600.0 * 1000.0;
    double rate_per_ms = cfg_.blackout_rate_per_hour / (3600.0 * 1000.0);

    std::exponential_distribution<double> inter_arrival(rate_per_ms);
    bool use_dist = (cfg_.blackout_mean_hours > 0.0 && cfg_.blackout_stddev_hours > 0.0);
    std::lognormal_distribution<double>   duration_dist(
        use_dist ? std::log(cfg_.blackout_mean_hours * 3600.0 * 1000.0) : 0.0,
        use_dist ? (cfg_.blackout_stddev_hours / cfg_.blackout_mean_hours) : 1.0
    );

    for (size_t node = 0; node < cfg_.num_nodes; ++node) {
        double t = inter_arrival(rng_);
        while (t < total_ms) {
            double dur = use_dist ? std::abs(duration_dist(rng_))
                                  : (cfg_.blackout_mean_hours * 3600.0 * 1000.0);
            // Clamp duration to reasonable range
            dur = std::min(dur, 48.0 * 3600.0 * 1000.0);
            dur = std::max(dur, 1000.0);  // at least 1 second

            BlackoutEvent be;
            be.start_ms = t;
            be.end_ms   = t + dur;
            be.node_id  = static_cast<uint8_t>(node);
            blackouts_.push_back(be);
            ++total_blackouts_;

            t += dur + inter_arrival(rng_);  // next blackout after this one ends
        }
    }

    // Sort by start time for efficient processing
    std::sort(blackouts_.begin(), blackouts_.end(),
              [](const BlackoutEvent& a, const BlackoutEvent& b) {
                  return a.start_ms < b.start_ms;
              });
}

// ══════════════════════════════════════════════════════════════
//  Jitter / distribution sampling
// ══════════════════════════════════════════════════════════════

double NetworkSimulator::sample_tx_jitter() {
    if (cfg_.tx_jitter_mean_ms <= 0.0) return 0.0;
    if (cfg_.tx_jitter_stddev_ms <= 0.0) return cfg_.tx_jitter_mean_ms;
    std::lognormal_distribution<double> dist(
        std::log(cfg_.tx_jitter_mean_ms),
        cfg_.tx_jitter_stddev_ms / cfg_.tx_jitter_mean_ms
    );
    return std::abs(dist(rng_));
}

double NetworkSimulator::sample_blackout_duration() {
    double mean_ms = cfg_.blackout_mean_hours * 3600.0 * 1000.0;
    if (mean_ms <= 0.0) return 1000.0;
    if (cfg_.blackout_stddev_hours <= 0.0) return std::max(1000.0, mean_ms);
    std::lognormal_distribution<double> dist(
        std::log(mean_ms),
        cfg_.blackout_stddev_hours / cfg_.blackout_mean_hours
    );
    return std::max(1000.0, std::abs(dist(rng_)));
}

// ══════════════════════════════════════════════════════════════
//  Core simulation helpers
// ══════════════════════════════════════════════════════════════

namespace {

/// Check whether a given node is in a blackout at a given time.
bool is_in_blackout(const std::vector<BlackoutEvent>& blackouts,
                    uint8_t node_id, double time_ms) {
    for (const auto& bo : blackouts) {
        if (bo.node_id == node_id && time_ms >= bo.start_ms && time_ms < bo.end_ms)
            return true;
    }
    return false;
}

} // namespace

void NetworkSimulator::process_telemetry(Shipment& ship) {
    if (ship.finished || ship.current_leg >= ship.route.size()) return;

    uint8_t entity = ship.route[ship.current_leg];
    auto& device = devices_[entity];

    // Check blackout status
    bool blacked_out = is_in_blackout(blackouts_, entity, sim_clock_ms_);
    device.set_online(!blacked_out);

    // Convert sim clock to nanoseconds for physical timestamping
    uint64_t sim_time_ns = static_cast<uint64_t>(sim_clock_ms_ * 1e6);

    // Generate telemetry
    // Temperature: 2-8°C normal range with occasional excursion
    std::uniform_real_distribution<float> temp_dist(2.5f, 7.5f);
    float temperature = temp_dist(rng_);

    // Inject occasional temperature excursion (5% chance)
    std::uniform_real_distribution<float> excursion_chance(0.0f, 1.0f);
    if (excursion_chance(rng_) < 0.05f) {
        temperature = 10.0f + static_cast<float>(std::uniform_real_distribution<double>(0, 5)(rng_));
    }

    float lat = 12.97f + 0.1f * entity;
    float lon = 77.59f + 0.1f * entity;

    SensorEvent se = device.generate_telemetry(
        temperature, lat, lon, sim_time_ns, &phys_clocks_[entity]);

    // Record in ground truth (true generation order)
    ground_truth_.push_back(se);

    if (device.is_online()) {
        // Schedule delivery to the current entity's ledger node with jitter
        double jitter = sample_tx_jitter();

        ScheduledMessage vclock_msg;
        vclock_msg.fire_time_ms = sim_clock_ms_ + jitter;
        vclock_msg.from = entity;
        vclock_msg.to   = entity;
        vclock_msg.payload = se;
        vclock_queue_.push(vclock_msg);

        // Same event to naive pipeline (same jitter for fairness)
        ScheduledMessage naive_msg = vclock_msg;
        naive_queue_.push(naive_msg);
    }
    // If offline, event stays in device buffer — will be flushed on reconnect.
}

void NetworkSimulator::process_custody_transfer(Shipment& ship) {
    if (ship.finished) return;
    if (ship.current_leg + 1 >= ship.route.size()) {
        ship.finished = true;
        return;
    }

    uint8_t from_entity = ship.route[ship.current_leg];
    uint8_t to_entity   = ship.route[ship.current_leg + 1];

    // Flush any buffered events from the departing device
    auto& from_device = devices_[from_entity];
    if (!from_device.is_online()) {
        from_device.set_online(true);
    }
    auto buffered = from_device.flush_on_reconnect();
    for (auto& se : buffered) {
        double jitter = sample_tx_jitter();
        ScheduledMessage msg;
        msg.fire_time_ms = sim_clock_ms_ + jitter;
        msg.from = from_entity;
        msg.to   = from_entity;
        msg.payload = se;
        vclock_queue_.push(msg);

        ScheduledMessage naive_msg = msg;
        naive_queue_.push(naive_msg);
    }

    // Create custody transfer event
    auto& from_vc = devices_[from_entity].local_clock_mut();
    from_vc.tick(from_entity);

    uint64_t sim_time_ns = static_cast<uint64_t>(sim_clock_ms_ * 1e6);

    CustodyEvent ce;
    ce.shipment_id         = ship.shipment_id;
    ce.from_entity_id      = from_entity;
    ce.to_entity_id        = to_entity;
    ce.custody_transfer_seq = ++custody_seq_;
    ce.vc                  = from_vc;
    ce.physical_timestamp_ns = phys_clocks_[from_entity].now_ns(sim_time_ns);
    ce.transfer_signature  = sign_event(ce.serialize(), default_node_secret(from_entity));

    ground_truth_.push_back(ce);

    // Deliver to both pipelines
    double jitter = sample_tx_jitter();
    ScheduledMessage msg;
    msg.fire_time_ms = sim_clock_ms_ + jitter;
    msg.from = from_entity;
    msg.to   = to_entity;
    msg.payload = ce;
    vclock_queue_.push(msg);

    ScheduledMessage naive_msg = msg;
    naive_queue_.push(naive_msg);

    // Merge VC into the receiving device (causal link across entities)
    devices_[to_entity].local_clock_mut().merge(from_vc);

    // Advance to next leg
    ship.current_leg++;
    double total_ms = cfg_.duration_hours * 3600.0 * 1000.0;
    double per_leg  = total_ms / static_cast<double>(ship.route.size());
    ship.leg_end_ms = sim_clock_ms_ + per_leg;
}

void NetworkSimulator::deliver_pending_messages() {
    // Deliver all messages whose fire_time <= sim_clock
    while (!vclock_queue_.empty() && vclock_queue_.top().fire_time_ms <= sim_clock_ms_) {
        auto msg = vclock_queue_.top();
        vclock_queue_.pop();

        vclock_received_.push_back(msg.payload);

        // Feed into vclock pipeline: topo-sort single-event "batch"
        std::vector<LedgerEvent> batch = {msg.payload};
        auto sorted = CausalMergeEngine::topo_sort(batch);
        vclock_nodes_[msg.to].ingest_batch(sorted);
    }

    while (!naive_queue_.empty() && naive_queue_.top().fire_time_ms <= sim_clock_ms_) {
        auto msg = naive_queue_.top();
        naive_queue_.pop();

        naive_received_.push_back(msg.payload);

        // Naive pipeline: no causal sorting, just ingest as-is
        std::vector<LedgerEvent> batch = {msg.payload};
        naive_nodes_[msg.to].ingest_batch(batch);
    }
}

// ══════════════════════════════════════════════════════════════
//  Simulation control
// ══════════════════════════════════════════════════════════════

void NetworkSimulator::step(double delta_time_ms) {
    double target = sim_clock_ms_ + delta_time_ms;
    double step_resolution = cfg_.sampling_interval_s * 1000.0;  // one tick per sampling interval

    while (sim_clock_ms_ < target) {
        sim_clock_ms_ = std::min(sim_clock_ms_ + step_resolution, target);

        // Process telemetry for each active shipment
        for (auto& ship : shipments_) {
            if (ship.finished) continue;

            // Time for a custody transfer?
            if (sim_clock_ms_ >= ship.leg_end_ms) {
                process_custody_transfer(ship);
                continue;
            }

            // Time for a telemetry reading?
            if (sim_clock_ms_ >= ship.next_sample_ms) {
                process_telemetry(ship);
                ship.next_sample_ms = sim_clock_ms_ + cfg_.sampling_interval_s * 1000.0;
            }
        }

        // Deliver messages that have arrived
        deliver_pending_messages();
    }
}

void NetworkSimulator::run_until(double end_time_ms) {
    if (sim_clock_ms_ >= end_time_ms) return;
    step(end_time_ms - sim_clock_ms_);
}

void NetworkSimulator::run_full() {
    // Generate random blackouts before running
    generate_random_blackouts();

    // Add default shipments if none were added
    if (shipments_.empty()) {
        std::vector<uint8_t> default_route;
        for (uint8_t i = 0; i < static_cast<uint8_t>(cfg_.num_nodes); ++i)
            default_route.push_back(i);

        for (uint32_t s = 1; s <= cfg_.num_shipments; ++s)
            add_shipment(s, default_route);
    }

    double total_ms = cfg_.duration_hours * 3600.0 * 1000.0;
    run_until(total_ms);

    // Flush any remaining buffered events from all devices
    for (size_t i = 0; i < devices_.size(); ++i) {
        auto& device = devices_[i];
        device.set_online(true);
        auto buffered = device.flush_on_reconnect();
        for (auto& se : buffered) {
            ScheduledMessage msg;
            msg.fire_time_ms = sim_clock_ms_;
            msg.from = static_cast<uint8_t>(i);
            msg.to   = static_cast<uint8_t>(i);
            msg.payload = se;
            vclock_queue_.push(msg);

            ScheduledMessage naive_msg = msg;
            naive_queue_.push(naive_msg);
        }
    }
    deliver_pending_messages();

    // Auto-seal remaining pending events on all nodes
    for (auto& node : vclock_nodes_) {
        if (node.pending_count() > 0)
            node.seal_block();
    }
    for (auto& node : naive_nodes_) {
        if (node.pending_count() > 0)
            node.seal_block();
    }
}

// ══════════════════════════════════════════════════════════════
//  Accessors
// ══════════════════════════════════════════════════════════════

LedgerNode& NetworkSimulator::vclock_node(size_t idx)             { return vclock_nodes_[idx]; }
const LedgerNode& NetworkSimulator::vclock_node(size_t idx) const { return vclock_nodes_[idx]; }
LedgerNode& NetworkSimulator::naive_node(size_t idx)              { return naive_nodes_[idx]; }
const LedgerNode& NetworkSimulator::naive_node(size_t idx) const  { return naive_nodes_[idx]; }
size_t NetworkSimulator::num_nodes() const                        { return cfg_.num_nodes; }
double NetworkSimulator::sim_clock_ms() const                     { return sim_clock_ms_; }

const std::vector<LedgerEvent>& NetworkSimulator::ground_truth_events() const {
    return ground_truth_;
}
const std::vector<LedgerEvent>& NetworkSimulator::vclock_received_events() const {
    return vclock_received_;
}
const std::vector<LedgerEvent>& NetworkSimulator::naive_received_events() const {
    return naive_received_;
}

size_t NetworkSimulator::total_events_generated() const  { return ground_truth_.size(); }
size_t NetworkSimulator::total_blackouts_injected() const { return total_blackouts_; }

} // namespace coldchain
