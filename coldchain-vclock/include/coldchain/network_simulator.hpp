#pragma once
/// @file network_simulator.hpp
/// @brief Discrete-event network simulator for the cold-chain.
///
/// Drives the end-to-end simulation: generates telemetry via IoTDevices,
/// injects blackout periods, adds transmission jitter, handles custody
/// transfers along each shipment's route, and feeds events into both
/// the vector-clock pipeline and the naive physical-timestamp baseline.
///
/// All randomness flows through a single seeded RNG for reproducibility.

#include <cstdint>
#include <functional>
#include <queue>
#include <random>
#include <string>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/events.hpp"
#include "coldchain/iot_device.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

// ═══════════════════════════════════════════════════════════════
//  ScheduledMessage — entries in the discrete-event priority queue
// ═══════════════════════════════════════════════════════════════

struct ScheduledMessage {
    double   fire_time_ms;       ///< when to deliver this message
    uint8_t  from;               ///< originating entity
    uint8_t  to;                 ///< destination entity
    LedgerEvent payload;         ///< the event being transmitted

    bool operator>(const ScheduledMessage& o) const {
        return fire_time_ms > o.fire_time_ms;
    }
};

// ═══════════════════════════════════════════════════════════════
//  Shipment — describes a shipment's route and tracking state
// ═══════════════════════════════════════════════════════════════

struct Shipment {
    uint32_t shipment_id;
    std::vector<uint8_t> route;   ///< entity indices, e.g. {0,1,2,3}
    size_t current_leg = 0;       ///< index into route
    double next_sample_ms = 0.0;  ///< when next telemetry is due
    double leg_end_ms = 0.0;      ///< when current custody leg ends (handoff time)
    bool   finished = false;
};

// ═══════════════════════════════════════════════════════════════
//  BlackoutEvent — scheduled blackout for a node
// ═══════════════════════════════════════════════════════════════

struct BlackoutEvent {
    double start_ms;
    double end_ms;
    uint8_t node_id;
};

// ═══════════════════════════════════════════════════════════════
//  NetworkSimulator
// ═══════════════════════════════════════════════════════════════

class NetworkSimulator {
public:
    /// Construct from a SimConfig.  All randomness seeded from cfg.seed.
    explicit NetworkSimulator(const SimConfig& cfg);

    // ── Shipment management ───────────────────────────────────

    /// Add a shipment with a given route, e.g. {0, 1, 2, 3} for M→D→C→P.
    void add_shipment(uint32_t shipment_id, std::vector<uint8_t> route);

    // ── Blackout injection ────────────────────────────────────

    /// Manually inject a blackout on @p node_id starting now, lasting @p duration_ms.
    void inject_blackout(uint8_t node_id, double duration_ms);

    // ── Simulation control ────────────────────────────────────

    /// Advance the simulation by @p delta_time_ms.
    void step(double delta_time_ms);

    /// Run until sim clock reaches @p end_time_ms.
    void run_until(double end_time_ms);

    /// Run the full simulation (cfg.duration_hours).
    void run_full();

    // ── Pipeline access ───────────────────────────────────────

    LedgerNode& vclock_node(size_t idx);
    const LedgerNode& vclock_node(size_t idx) const;

    LedgerNode& naive_node(size_t idx);
    const LedgerNode& naive_node(size_t idx) const;

    /// Number of nodes.
    size_t num_nodes() const;

    /// Current simulation time in milliseconds.
    double sim_clock_ms() const;

    // ── Ground-truth event log ────────────────────────────────

    /// All events generated during the run, in true generation order
    /// (ground truth causal order), before network shuffling.
    const std::vector<LedgerEvent>& ground_truth_events() const;

    /// All events as received by the vclock pipeline (post-network delivery).
    const std::vector<LedgerEvent>& vclock_received_events() const;

    /// All events as received by the naive pipeline (same events, same order).
    const std::vector<LedgerEvent>& naive_received_events() const;

    // ── Statistics ─────────────────────────────────────────────

    size_t total_events_generated() const;
    size_t total_blackouts_injected() const;

private:
    // ── Internal helpers ──────────────────────────────────────

    void generate_random_blackouts();
    void process_telemetry(Shipment& ship);
    void process_custody_transfer(Shipment& ship);
    void deliver_pending_messages();
    double sample_tx_jitter();
    double sample_blackout_duration();

    // ── State ─────────────────────────────────────────────────

    SimConfig cfg_;
    std::mt19937_64 rng_;
    double sim_clock_ms_ = 0.0;

    // Devices and nodes (one per entity)
    std::vector<IoTDevice> devices_;
    std::vector<DriftingPhysicalClock> phys_clocks_;
    std::vector<LedgerNode> vclock_nodes_;
    std::vector<LedgerNode> naive_nodes_;

    // Shipments
    std::vector<Shipment> shipments_;

    // Message queue (discrete-event)
    using MsgQueue = std::priority_queue<ScheduledMessage,
                                         std::vector<ScheduledMessage>,
                                         std::greater<ScheduledMessage>>;
    MsgQueue vclock_queue_;
    MsgQueue naive_queue_;

    // Blackout schedule
    std::vector<BlackoutEvent> blackouts_;

    // Event logs
    std::vector<LedgerEvent> ground_truth_;      ///< true generation order
    std::vector<LedgerEvent> vclock_received_;    ///< vclock pipeline receipt order
    std::vector<LedgerEvent> naive_received_;     ///< naive pipeline receipt order

    // Counters
    size_t total_blackouts_ = 0;
    uint64_t custody_seq_ = 0;
};

} // namespace coldchain
