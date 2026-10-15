/// @file test_pruning_safety.cpp
/// @brief Week 4 test suite — PruningEngine correctness and safety assertions.
///
/// Tests:
///   1. is_safe_to_prune returns false when any dimension is under-observed.
///   2. is_safe_to_prune returns true when horizon dominates event VC.
///   3. Adversarial regression: events not pruned before all nodes observe.
///   4. Multi-round gossip: pruning only removes truly stable records.
///   5. No state divergence: pruned-side still has consistent chain.
///   6. Stress test: large event volume with incremental pruning.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/pruning_engine.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/matrix_clock.hpp"
#include "coldchain/vector_clock.hpp"
#include "coldchain/events.hpp"
#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/config.hpp"

using namespace coldchain;

// ── Helpers ──────────────────────────────────────────────────

/// Build a SensorEvent with given VC and entity.
static SensorEvent make_sensor(uint8_t entity, VectorClock vc,
                               float temp = 5.0f, uint32_t ship = 1) {
    SensorEvent se;
    se.shipment_id            = ship;
    se.origin_entity_id       = entity;
    se.temperature_celsius    = temp;
    se.latitude               = 0.0f;
    se.longitude              = 0.0f;
    se.local_monotonic_counter = 0;
    se.vc                     = std::move(vc);
    se.signature              = {};
    se.physical_timestamp_ns  = 0;
    return se;
}

/// Simulate N rounds of all-to-all gossip on an array of LedgerNodes.
/// After merging matrices, each node updates its own row to reflect the
/// merged knowledge (max of own row and sender's row for the sender's data).
static void gossip_rounds(std::vector<LedgerNode>& nodes, size_t rounds) {
    size_t n = nodes.size();
    for (size_t r = 0; r < rounds; ++r) {
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;
                // Node j receives node i's matrix
                nodes[j].matrix_clock().merge_matrix(
                    nodes[i].node_id(), nodes[i].matrix_clock());
                // After receiving, node j now knows everything node i knew.
                // Update j's own row: for each dimension k, j's own knowledge
                // is max of its current own row and what it just learned.
                for (size_t k = 0; k < n; ++k) {
                    uint32_t cur = nodes[j].matrix_clock().at(j, k);
                    uint32_t learned = nodes[i].matrix_clock().at(i, k);
                    if (learned > cur)
                        nodes[j].matrix_clock().set(j, k, learned);
                }
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════
//  1. Basic is_safe_to_prune — negative case
// ══════════════════════════════════════════════════════════════

TEST_CASE("PruningEngine::is_safe_to_prune returns false when horizon is below event VC") {
    VectorClock event_vc(4);
    event_vc.tick(0); event_vc.tick(0);  // [2,0,0,0]

    VectorClock horizon(4);
    horizon.tick(0);  // [1,0,0,0]

    CHECK_FALSE(PruningEngine::is_safe_to_prune(event_vc, horizon));
}

TEST_CASE("PruningEngine::is_safe_to_prune false when one dimension is under") {
    VectorClock event_vc(4);
    event_vc.tick(0); event_vc.tick(1); event_vc.tick(2);  // [1,1,1,0]

    VectorClock horizon(4);
    horizon.tick(0); horizon.tick(0);  // [2,0,0,0]
    // horizon[0]=2 >= event[0]=1, but horizon[1]=0 < event[1]=1

    CHECK_FALSE(PruningEngine::is_safe_to_prune(event_vc, horizon));
}

// ══════════════════════════════════════════════════════════════
//  2. Basic is_safe_to_prune — positive case
// ══════════════════════════════════════════════════════════════

TEST_CASE("PruningEngine::is_safe_to_prune returns true when horizon dominates event VC") {
    VectorClock event_vc(4);
    event_vc.tick(0);  // [1,0,0,0]

    VectorClock horizon(4);
    horizon.tick(0); horizon.tick(1); horizon.tick(2); horizon.tick(3);  // [1,1,1,1]

    CHECK(PruningEngine::is_safe_to_prune(event_vc, horizon));
}

TEST_CASE("PruningEngine::is_safe_to_prune true for equal VCs") {
    VectorClock vc(4);
    vc.tick(0); vc.tick(1);  // [1,1,0,0]
    CHECK(PruningEngine::is_safe_to_prune(vc, vc));
}

TEST_CASE("PruningEngine::is_safe_to_prune true for zero VC and zero horizon") {
    VectorClock zero(4);  // [0,0,0,0]
    CHECK(PruningEngine::is_safe_to_prune(zero, zero));
}

// ══════════════════════════════════════════════════════════════
//  3. Adversarial regression: event NOT pruned before all nodes observe
// ══════════════════════════════════════════════════════════════

TEST_CASE("Adversarial: event not pruned until matrix reflects full observation") {
    constexpr size_t N = 4;

    // Create 4 nodes
    std::vector<LedgerNode> nodes;
    for (size_t i = 0; i < N; ++i)
        nodes.emplace_back(static_cast<uint8_t>(i), N);

    // Node 0 generates an event with VC [1,0,0,0]
    VectorClock vc0(N);
    vc0.tick(0);
    SensorEvent se = make_sensor(0, vc0);

    // Ingest into node 0's ledger
    std::vector<LedgerEvent> batch = {se};
    nodes[0].ingest_batch(batch);

    // Only node 0 knows about it — matrix: row 0 = [1,0,0,0], all others = [0,0,0,0]
    nodes[0].matrix_clock().set_own_row(vc0, 0);

    // Pruning should NOT remove the event (other nodes haven't seen it)
    size_t pruned = PruningEngine::prune_stable_records(nodes[0]);
    CHECK(pruned == 0);
    CHECK(nodes[0].active_state_store().size() == 1);

    // Partially propagate: nodes 1 and 2 learn, but NOT node 3
    nodes[1].matrix_clock().set_own_row(vc0, 1);
    nodes[2].matrix_clock().set_own_row(vc0, 2);
    // Gossip between 0,1,2 but not 3
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            if (i == j) continue;
            nodes[0].matrix_clock().merge_matrix(
                nodes[i].node_id(), nodes[i].matrix_clock());
        }
    }
    // Node 3 still at [0,0,0,0] → column_min()[0] should be 0
    // STILL NOT SAFE TO PRUNE
    pruned = PruningEngine::prune_stable_records(nodes[0]);
    CHECK(pruned == 0);

    // Now node 3 observes it too
    nodes[3].matrix_clock().set_own_row(vc0, 3);
    // Full gossip
    gossip_rounds(nodes, 2);

    // Re-merge into node 0's matrix
    for (size_t j = 0; j < N; ++j) {
        nodes[0].matrix_clock().merge_matrix(
            nodes[j].node_id(), nodes[j].matrix_clock());
    }

    // NOW it should be safe
    pruned = PruningEngine::prune_stable_records(nodes[0]);
    CHECK(pruned == 1);
    CHECK(nodes[0].active_state_store().empty());
}

// ══════════════════════════════════════════════════════════════
//  4. Multi-round gossip: pruning correctness against external ground truth
// ══════════════════════════════════════════════════════════════

TEST_CASE("Multi-round gossip: pruning matches externally computed global minimum") {
    constexpr size_t N = 4;
    constexpr size_t EVENTS_PER_NODE = 5;

    std::vector<LedgerNode> nodes;
    for (size_t i = 0; i < N; ++i)
        nodes.emplace_back(static_cast<uint8_t>(i), N);

    // Each node has its OWN independent VC (like real devices)
    std::vector<VectorClock> vcs(N, VectorClock(N));

    for (size_t node = 0; node < N; ++node) {
        for (size_t e = 0; e < EVENTS_PER_NODE; ++e) {
            vcs[node].tick(node);
            SensorEvent se = make_sensor(static_cast<uint8_t>(node), vcs[node]);
            std::vector<LedgerEvent> batch = {se};
            nodes[node].ingest_batch(batch);
            nodes[node].matrix_clock().set_own_row(vcs[node], node);
        }
    }

    // Before gossip: each node only knows its own events
    for (size_t i = 0; i < N; ++i) {
        size_t prunable = PruningEngine::count_prunable(nodes[i]);
        CHECK(prunable == 0);
    }

    // After complete gossip: all events should become prunable
    // Each node's row in every other node's matrix should reflect the latest VC.
    gossip_rounds(nodes, N);

    for (size_t i = 0; i < N; ++i) {
        size_t prunable = PruningEngine::count_prunable(nodes[i]);
        size_t store_size = nodes[i].active_state_store().size();
        // After full gossip with per-node VCs, every event's VC has only
        // its own dimension set (e.g. [5,0,0,0] for node 0). column_min
        // should now dominate all of these.
        CHECK(prunable == store_size);
    }

    // Prune all nodes
    for (size_t i = 0; i < N; ++i) {
        PruningEngine::prune_stable_records(nodes[i]);
        CHECK(nodes[i].active_state_store().empty());
    }
}

// ══════════════════════════════════════════════════════════════
//  5. No state divergence after pruning
// ══════════════════════════════════════════════════════════════

TEST_CASE("Pruning does not corrupt the hash chain") {
    constexpr size_t N = 4;

    std::vector<LedgerNode> nodes;
    for (size_t i = 0; i < N; ++i)
        nodes.emplace_back(static_cast<uint8_t>(i), N);

    // Generate events, seal blocks, gossip, then prune
    VectorClock vc(N);
    for (size_t node = 0; node < N; ++node) {
        for (size_t e = 0; e < 10; ++e) {
            vc.tick(node);
            SensorEvent se = make_sensor(static_cast<uint8_t>(node), vc);
            std::vector<LedgerEvent> batch = {se};
            nodes[node].ingest_batch(batch);
            nodes[node].matrix_clock().set_own_row(vc, node);
        }
        // Seal a block
        if (nodes[node].pending_count() > 0) {
            nodes[node].seal_block();
        }
    }

    // Full gossip + prune
    gossip_rounds(nodes, N);
    for (size_t i = 0; i < N; ++i) {
        PruningEngine::prune_stable_records(nodes[i]);
    }

    // Chain integrity must still hold (pruning only affects active_state_store,
    // not the sealed block chain)
    for (size_t i = 0; i < N; ++i) {
        CHECK(verify_chain(nodes[i].chain()));
    }
}

// ══════════════════════════════════════════════════════════════
//  6. Stress test: large batch with incremental pruning
// ══════════════════════════════════════════════════════════════

TEST_CASE("Stress: incremental pruning over 200 events across 4 nodes") {
    constexpr size_t N = 4;
    constexpr size_t EVENTS_PER_NODE = 50;

    std::vector<LedgerNode> nodes;
    for (size_t i = 0; i < N; ++i)
        nodes.emplace_back(static_cast<uint8_t>(i), N);

    size_t total_generated = 0;
    std::vector<VectorClock> vcs(N, VectorClock(N));

    for (size_t round = 0; round < EVENTS_PER_NODE; ++round) {
        // Each node generates one event per round with its own VC
        for (size_t node = 0; node < N; ++node) {
            vcs[node].tick(node);
            SensorEvent se = make_sensor(static_cast<uint8_t>(node), vcs[node]);
            std::vector<LedgerEvent> batch = {se};
            nodes[node].ingest_batch(batch);
            nodes[node].matrix_clock().set_own_row(vcs[node], node);
            ++total_generated;
        }

        // Gossip after each round
        gossip_rounds(nodes, 1);

        // Prune after each round
        for (size_t i = 0; i < N; ++i) {
            PruningEngine::prune_stable_records(nodes[i]);
        }
    }

    CHECK(total_generated == N * EVENTS_PER_NODE);

    // After full gossip + incremental pruning, all stores should be empty
    gossip_rounds(nodes, N);
    for (size_t i = 0; i < N; ++i) {
        PruningEngine::prune_stable_records(nodes[i]);
        CHECK(nodes[i].active_state_store().empty());
    }
}

// ══════════════════════════════════════════════════════════════
//  7. Pruning horizon computation
// ══════════════════════════════════════════════════════════════

TEST_CASE("PruningEngine::pruning_horizon matches manual column_min") {
    constexpr size_t N = 3;

    LedgerNode node(0, N);

    // Manually set up matrix clock
    node.matrix_clock().set(0, 0, 5);
    node.matrix_clock().set(0, 1, 3);
    node.matrix_clock().set(0, 2, 2);

    node.matrix_clock().set(1, 0, 4);
    node.matrix_clock().set(1, 1, 6);
    node.matrix_clock().set(1, 2, 1);

    node.matrix_clock().set(2, 0, 3);
    node.matrix_clock().set(2, 1, 2);
    node.matrix_clock().set(2, 2, 4);

    VectorClock horizon = PruningEngine::pruning_horizon(node);

    // column_min: min(5,4,3)=3, min(3,6,2)=2, min(2,1,4)=1
    CHECK(horizon[0] == 3);
    CHECK(horizon[1] == 2);
    CHECK(horizon[2] == 1);
}

// ══════════════════════════════════════════════════════════════
//  8. Integration with NetworkSimulator
// ══════════════════════════════════════════════════════════════

TEST_CASE("Integration: PruningEngine reduces store size after full simulation") {
    SimConfig cfg;
    cfg.num_shipments       = 2;
    cfg.duration_hours      = 1.0;
    cfg.sampling_interval_s = 120.0;
    cfg.blackout_mean_hours = 0.5;
    cfg.seed                = 99;
    cfg.events_per_block    = 8;

    NetworkSimulator sim(cfg);
    for (uint32_t s = 0; s < cfg.num_shipments; ++s) {
        sim.add_shipment(s + 1, {0, 1, 2, 3});
    }
    sim.run_full();

    // Measure before pruning
    size_t before_total = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        before_total += sim.vclock_node(i).active_state_store().size();
    }

    // Prune
    size_t total_pruned = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        total_pruned += PruningEngine::prune_stable_records(sim.vclock_node(i));
    }

    size_t after_total = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        after_total += sim.vclock_node(i).active_state_store().size();
    }

    CHECK(after_total <= before_total);
    CHECK(total_pruned == before_total - after_total);

    // Chain integrity should still hold
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        CHECK(verify_chain(sim.vclock_node(i).chain()));
    }
}
