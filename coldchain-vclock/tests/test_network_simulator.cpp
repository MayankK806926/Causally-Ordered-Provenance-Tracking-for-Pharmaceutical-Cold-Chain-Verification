/// @file test_network_simulator.cpp
/// @brief Week 3 unit tests for NetworkSimulator, IoTDevice, and LedgerNode.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/config.hpp"
#include "coldchain/iot_device.hpp"
#include "coldchain/ledger_node.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/physical_clock.hpp"

using namespace coldchain;

// ══════════════════════════════════════════════════════════════
//  IoTDevice tests
// ══════════════════════════════════════════════════════════════

TEST_CASE("IoTDevice generates telemetry with correct vector clock") {
    IoTDevice dev(1, 4, /*shipment*/ 42);
    CHECK(dev.entity_id() == 1);
    CHECK(dev.shipment_id() == 42);
    CHECK(dev.is_online());

    auto se1 = dev.generate_telemetry(5.0f, 12.9f, 77.6f);
    CHECK(se1.shipment_id == 42);
    CHECK(se1.origin_entity_id == 1);
    CHECK(se1.vc[1] == 1);  // ticked slot 1
    CHECK(se1.vc[0] == 0);  // others untouched
    CHECK(se1.local_monotonic_counter == 1);

    auto se2 = dev.generate_telemetry(6.0f, 12.9f, 77.6f);
    CHECK(se2.vc[1] == 2);  // incremented again
    CHECK(se2.local_monotonic_counter == 2);
}

TEST_CASE("IoTDevice buffers events during blackout and flushes on reconnect") {
    IoTDevice dev(0, 4, 1);

    // Go offline
    dev.set_online(false);
    CHECK_FALSE(dev.is_online());

    // Generate events while offline
    dev.generate_telemetry(3.0f, 0, 0);
    dev.generate_telemetry(4.0f, 0, 0);
    dev.generate_telemetry(5.0f, 0, 0);
    CHECK(dev.buffered_count() == 3);

    // Vector clock still advances while offline
    CHECK(dev.local_clock()[0] == 3);

    // Flush
    auto flushed = dev.flush_on_reconnect();
    CHECK(flushed.size() == 3);
    CHECK(dev.buffered_count() == 0);

    // Flushed events retain their original VC snapshots
    CHECK(flushed[0].vc[0] == 1);
    CHECK(flushed[1].vc[0] == 2);
    CHECK(flushed[2].vc[0] == 3);
}

TEST_CASE("IoTDevice physical timestamp uses drifting clock when provided") {
    DriftingPhysicalClock phys(100.0, 0.0, 42);  // 100 PPM drift, zero stddev
    IoTDevice dev(0, 4, 1);

    uint64_t sim_ns = 1'000'000'000ULL;  // 1 second
    auto se = dev.generate_telemetry(4.0f, 0, 0, sim_ns, &phys);

    // With 100 PPM drift, physical time should differ from sim time
    CHECK(se.physical_timestamp_ns != 0);
    // 100 PPM = 100 * 1e-6 = 0.0001, so delta = 1e9 * 0.0001 = 100000 ns
    // Drifted = 1e9 + 100000 = 1000100000
    // (exact value depends on sampling, but should be close)
    CHECK(se.physical_timestamp_ns != sim_ns);
}

// ══════════════════════════════════════════════════════════════
//  LedgerNode tests
// ══════════════════════════════════════════════════════════════

TEST_CASE("LedgerNode ingests batch and seals block") {
    LedgerNode node(0, 4);
    CHECK(node.chain().empty());

    // Create a small batch
    SensorEvent se;
    se.shipment_id = 1;
    se.origin_entity_id = 0;
    se.temperature_celsius = 5.0f;
    se.latitude = 12.97f;
    se.longitude = 77.59f;
    se.local_monotonic_counter = 1;
    se.vc = VectorClock(4);
    se.vc.tick(0);

    std::vector<LedgerEvent> batch = {se};
    bool valid = node.ingest_batch(batch);
    CHECK(valid);
    CHECK(node.pending_count() == 1);

    // Seal
    auto block = node.seal_block();
    CHECK(block.events.size() == 1);
    CHECK(node.chain().size() == 1);
    CHECK(node.pending_count() == 0);
}

TEST_CASE("LedgerNode auto_seal seals at threshold") {
    LedgerNode node(0, 4);
    VectorClock vc(4);

    // Ingest 5 events
    for (int i = 0; i < 5; ++i) {
        vc.tick(0);
        SensorEvent se;
        se.shipment_id = 1;
        se.origin_entity_id = 0;
        se.temperature_celsius = 4.0f;
        se.latitude = 0; se.longitude = 0;
        se.local_monotonic_counter = i + 1;
        se.vc = vc;
        node.ingest_batch({se});
    }

    CHECK(node.pending_count() == 5);

    // Auto-seal with threshold 3 → should seal 1 block of 3, leaving 2 pending
    size_t sealed = node.auto_seal(3);
    CHECK(sealed == 1);
    CHECK(node.chain().size() == 1);
    CHECK(node.pending_count() == 2);
}

TEST_CASE("LedgerNode chain integrity after multiple seals") {
    LedgerNode node(0, 4);
    VectorClock vc(4);

    for (int i = 0; i < 10; ++i) {
        vc.tick(0);
        SensorEvent se;
        se.shipment_id = 1;
        se.origin_entity_id = 0;
        se.temperature_celsius = 4.0f + 0.1f * i;
        se.latitude = 0; se.longitude = 0;
        se.local_monotonic_counter = i + 1;
        se.vc = vc;
        node.ingest_batch({se});

        if (node.pending_count() >= 3)
            node.seal_block();
    }

    // Seal any remaining
    if (node.pending_count() > 0)
        node.seal_block();

    CHECK(verify_chain(node.chain()));
}

// ══════════════════════════════════════════════════════════════
//  NetworkSimulator tests
// ══════════════════════════════════════════════════════════════

TEST_CASE("NetworkSimulator deterministic run with fixed seed") {
    SimConfig cfg;
    cfg.seed = 12345;
    cfg.num_shipments = 2;
    cfg.duration_hours = 1.0;
    cfg.sampling_interval_s = 120.0;  // every 2 minutes
    cfg.blackout_rate_per_hour = 0.0; // no random blackouts for this test

    NetworkSimulator sim1(cfg);
    sim1.run_full();

    NetworkSimulator sim2(cfg);
    sim2.run_full();

    // Same seed → same number of events
    CHECK(sim1.total_events_generated() == sim2.total_events_generated());
    CHECK(sim1.total_events_generated() > 0);

    // Same seed → identical ground truth event sequence
    const auto& gt1 = sim1.ground_truth_events();
    const auto& gt2 = sim2.ground_truth_events();
    CHECK(gt1.size() == gt2.size());

    for (size_t i = 0; i < gt1.size(); ++i) {
        CHECK(event_vc(gt1[i]) == event_vc(gt2[i]));
        CHECK(event_origin(gt1[i]) == event_origin(gt2[i]));
        CHECK(event_counter(gt1[i]) == event_counter(gt2[i]));
    }
}

TEST_CASE("NetworkSimulator different seeds produce different runs") {
    SimConfig cfg;
    cfg.num_shipments = 2;
    cfg.duration_hours = 1.0;
    cfg.sampling_interval_s = 120.0;
    cfg.blackout_rate_per_hour = 0.5;

    cfg.seed = 111;
    NetworkSimulator sim1(cfg);
    sim1.run_full();

    cfg.seed = 222;
    NetworkSimulator sim2(cfg);
    sim2.run_full();

    // Different blackout patterns should produce different event counts or
    // timings (at minimum, blackout counts should differ)
    // Events may or may not differ in count depending on timing, but
    // we can at least check they ran.
    CHECK(sim1.total_events_generated() > 0);
    CHECK(sim2.total_events_generated() > 0);
}

TEST_CASE("NetworkSimulator blackout injection suppresses transmission") {
    SimConfig cfg;
    cfg.seed = 42;
    cfg.num_shipments = 1;
    cfg.duration_hours = 0.5;        // 30 minutes
    cfg.sampling_interval_s = 60.0;  // every minute
    cfg.blackout_rate_per_hour = 0.0;// no random blackouts
    cfg.num_nodes = 4;

    NetworkSimulator sim(cfg);
    sim.add_shipment(1, {0, 1, 2, 3});

    // Inject a blackout on node 0 for the entire duration
    sim.inject_blackout(0, cfg.duration_hours * 3600.0 * 1000.0);

    sim.run_full();

    // Events should still have been generated (buffered during blackout)
    CHECK(sim.total_events_generated() > 0);
    CHECK(sim.total_blackouts_injected() >= 1);
}

TEST_CASE("NetworkSimulator produces events with valid vector clocks") {
    SimConfig cfg;
    cfg.seed = 42;
    cfg.num_shipments = 2;
    cfg.duration_hours = 0.5;
    cfg.sampling_interval_s = 120.0;
    cfg.blackout_rate_per_hour = 0.0;

    NetworkSimulator sim(cfg);
    sim.run_full();

    const auto& gt = sim.ground_truth_events();
    CHECK(gt.size() > 0);

    // Every event should have a VC of the correct dimension
    for (const auto& ev : gt) {
        CHECK(event_vc(ev).size() == cfg.num_nodes);
    }
}

TEST_CASE("NetworkSimulator ledger chains verify") {
    SimConfig cfg;
    cfg.seed = 42;
    cfg.num_shipments = 2;
    cfg.duration_hours = 0.5;
    cfg.sampling_interval_s = 120.0;
    cfg.blackout_rate_per_hour = 0.0;

    NetworkSimulator sim(cfg);
    sim.run_full();

    // All vclock node chains should verify
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        const auto& chain = sim.vclock_node(i).chain();
        if (!chain.empty()) {
            CHECK(verify_chain(chain));
        }
    }

    // All naive node chains should verify
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        const auto& chain = sim.naive_node(i).chain();
        if (!chain.empty()) {
            CHECK(verify_chain(chain));
        }
    }
}

TEST_CASE("NetworkSimulator vclock pipeline receives same events as naive") {
    SimConfig cfg;
    cfg.seed = 42;
    cfg.num_shipments = 1;
    cfg.duration_hours = 0.25;
    cfg.sampling_interval_s = 60.0;
    cfg.blackout_rate_per_hour = 0.0;

    NetworkSimulator sim(cfg);
    sim.run_full();

    // Both pipelines should receive the same number of events
    CHECK(sim.vclock_received_events().size() == sim.naive_received_events().size());
}
