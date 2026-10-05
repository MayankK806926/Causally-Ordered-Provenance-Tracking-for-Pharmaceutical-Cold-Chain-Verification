/// @file main_sim.cpp
/// @brief CLI simulation runner demonstrating Week 1 & Week 2 deliverables:
///        Vector Clock causal ordering vs. Naive physical-clock baseline.

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <random>
#include <algorithm>

#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/config.hpp"
#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

using namespace coldchain;

struct SimOptions {
    uint32_t shipments = 3;
    uint64_t seed = 42;
    bool verbose = false;
};

void print_banner() {
    std::cout << "========================================================================\n"
              << " Resilient Provenance Tracking for IoT Cold Chains via Logical Clocks\n"
              << " CS6666 Distributed Ledger Technologies -- Simulation Harness (Week 2)\n"
              << "========================================================================\n\n";
}

int main(int argc, char* argv[]) {
    SimOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--shipments" && i + 1 < argc) {
            opts.shipments = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--seed" && i + 1 < argc) {
            opts.seed = std::stoull(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            opts.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./sim [options]\n"
                      << "Options:\n"
                      << "  --shipments N   Number of shipments to simulate (default: 3)\n"
                      << "  --seed S        Random seed (default: 42)\n"
                      << "  --verbose, -v   Print detailed event logs\n"
                      << "  --help, -h      Show this message\n";
            return 0;
        }
    }

    print_banner();
    std::cout << "[Config] Shipments: " << opts.shipments
              << ", Seed: " << opts.seed
              << ", Entities: 4 (Manufacturer, Distributor, Cold Storage, Pharmacy)\n\n";

    std::mt19937_64 rng(opts.seed);
    const size_t num_entities = kDefaultNumNodes;

    // Initialize physical clocks for each entity with distinct drift rates
    std::vector<DriftingPhysicalClock> phys_clocks;
    for (size_t i = 0; i < num_entities; ++i) {
        // Alternate positive and negative drift to simulate real thermal-cycle oscillator drift
        double mean_ppm = (i % 2 == 0) ? 60.0 : -60.0;
        phys_clocks.emplace_back(mean_ppm, 15.0, opts.seed + i * 100);
        if (opts.verbose) {
            std::cout << "  Entity " << i << " clock drift: "
                      << std::fixed << std::setprecision(2)
                      << phys_clocks.back().drift_rate_ppm() << " PPM\n";
        }
    }

    std::vector<LedgerEvent> generated_events;
    uint64_t monotonic_counter = 0;

    // Simulate shipments traveling through supply chain:
    // Route: Manufacturer (0) -> Distributor (1) -> Cold Storage (2) -> Pharmacy (3)
    for (uint32_t s = 1; s <= opts.shipments; ++s) {
        VectorClock shipment_vc(num_entities);
        uint64_t sim_time_ns = s * 3600ULL * 1'000'000'000ULL; // each shipment starts 1 hour apart

        for (uint8_t entity = 0; entity < num_entities; ++entity) {
            // Generate 3 telemetry readings per entity stage
            for (int r = 0; r < 3; ++r) {
                shipment_vc.tick(entity);
                ++monotonic_counter;
                sim_time_ns += 300ULL * 1'000'000'000ULL; // 5 min interval

                uint64_t phys_time = phys_clocks[entity].now_ns(sim_time_ns);

                // Normal range 2 - 8 C, occasional excursion
                float temp = 4.0f + static_cast<float>(r) * 0.5f;
                if (entity == kDistributor && r == 1) {
                    temp = 9.8f; // simulated temperature excursion during transit!
                }

                SensorEvent se;
                se.shipment_id = s;
                se.origin_entity_id = entity;
                se.temperature_celsius = temp;
                se.latitude = 12.97f + 0.1f * entity;
                se.longitude = 77.59f + 0.1f * entity;
                se.local_monotonic_counter = monotonic_counter;
                se.vc = shipment_vc;
                se.physical_timestamp_ns = phys_time;
                se.signature = sign_event(se.serialize(), default_node_secret(entity));

                generated_events.push_back(se);
            }

            // Custody handoff to next entity
            if (entity + 1 < num_entities) {
                shipment_vc.tick(entity);
                sim_time_ns += 600ULL * 1'000'000'000ULL; // 10 min handoff delay
                uint64_t phys_time = phys_clocks[entity].now_ns(sim_time_ns);

                CustodyEvent ce;
                ce.shipment_id = s;
                ce.from_entity_id = entity;
                ce.to_entity_id = entity + 1;
                ce.custody_transfer_seq = ++monotonic_counter;
                ce.vc = shipment_vc;
                ce.physical_timestamp_ns = phys_time;
                ce.transfer_signature = sign_event(ce.serialize(), default_node_secret(entity));

                generated_events.push_back(ce);
            }
        }
    }

    std::cout << "[Simulation] Generated " << generated_events.size()
              << " total events across " << opts.shipments << " shipments.\n";

    // Simulate network transit delays & blackout buffer uploads by shuffling reception order
    std::vector<LedgerEvent> received_events = generated_events;
    std::shuffle(received_events.begin(), received_events.end(), rng);

    std::cout << "[Network] Simulated RF blackouts and out-of-order packet arrival.\n\n";

    // ── Pipeline 1: Vector Clock Causal Merge ───────────────────────────
    std::cout << "--- Pipeline 1: Vector Clock + Causal Merge Engine ---\n";
    bool vclock_sigs_valid = CausalMergeEngine::verify_signatures(received_events);
    std::cout << "  1. Signatures verified: " << (vclock_sigs_valid ? "PASS" : "FAIL") << "\n";

    auto vclock_sorted = CausalMergeEngine::topo_sort(received_events);
    size_t vclock_inversions = count_causal_inversions(vclock_sorted);
    bool vclock_causal_ok = CausalMergeEngine::is_causally_valid(vclock_sorted);

    std::cout << "  2. Topological sorting: COMPLETE\n";
    std::cout << "  3. Causal misordering count: " << vclock_inversions << " (must be 0)\n";
    std::cout << "  4. Invariant check: " << (vclock_causal_ok ? "PASS" : "FAIL") << "\n";

    // Seal blocks (32 events per block)
    std::vector<LedgerBlock> vclock_chain;
    Hash256 prev_hash = genesis_hash();
    for (size_t i = 0; i < vclock_sorted.size(); i += 32) {
        size_t chunk_size = std::min(size_t(32), vclock_sorted.size() - i);
        std::vector<LedgerEvent> batch(vclock_sorted.begin() + i, vclock_sorted.begin() + i + chunk_size);
        auto block = seal_block(vclock_chain.size(), batch, prev_hash, num_entities);
        prev_hash = block.this_hash;
        vclock_chain.push_back(block);
    }
    bool vclock_chain_valid = verify_chain(vclock_chain);
    std::cout << "  5. Sealed " << vclock_chain.size() << " blocks into hash chain.\n";
    std::cout << "  6. Cryptographic chain integrity check: "
              << (vclock_chain_valid ? "VALID" : "CORRUPT") << "\n\n";

    // ── Pipeline 2: Naive Physical-Timestamp Baseline ───────────────────
    std::cout << "--- Pipeline 2: Naive Physical Timestamp Baseline ---\n";
    auto naive_sorted = naive_physical_order(received_events);
    size_t naive_inversions = count_causal_inversions(naive_sorted);
    bool naive_causal_ok = CausalMergeEngine::is_causally_valid(naive_sorted);

    std::cout << "  1. Physical timestamp sorting: COMPLETE\n";
    std::cout << "  2. Causal misordering count: " << naive_inversions << " (violations due to drift/transit!)\n";
    std::cout << "  3. Invariant check: " << (naive_causal_ok ? "PASS" : "VIOLATION DETECTED") << "\n";

    std::vector<LedgerBlock> naive_chain;
    prev_hash = genesis_hash();
    for (size_t i = 0; i < naive_sorted.size(); i += 32) {
        size_t chunk_size = std::min(size_t(32), naive_sorted.size() - i);
        std::vector<LedgerEvent> batch(naive_sorted.begin() + i, naive_sorted.begin() + i + chunk_size);
        auto block = seal_block(naive_chain.size(), batch, prev_hash, num_entities);
        prev_hash = block.this_hash;
        naive_chain.push_back(block);
    }
    bool naive_chain_valid = verify_chain(naive_chain);
    std::cout << "  4. Sealed " << naive_chain.size() << " blocks into hash chain.\n";
    std::cout << "  5. Cryptographic chain integrity check: "
              << (naive_chain_valid ? "VALID" : "CORRUPT") << "\n\n";

    // ── Summary Comparison ──────────────────────────────────────────────
    std::cout << "========================================================================\n"
              << " COMPARISON RESULTS\n"
              << "========================================================================\n"
              << " Metric                       | Vector Clock Pipeline | Naive Baseline\n"
              << "------------------------------+-----------------------+-----------------\n"
              << " Causal Inversions (R_err)    | " << std::setw(21) << vclock_inversions << " | "
              << std::setw(15) << naive_inversions << "\n"
              << " Causal Invariant Preserved?  | " << std::setw(21) << (vclock_causal_ok ? "YES (Guaranteed)" : "NO") << " | "
              << std::setw(15) << (naive_causal_ok ? "YES" : "NO (Violated)") << "\n"
              << " Chain Integrity Verified?    | " << std::setw(21) << (vclock_chain_valid ? "YES" : "NO") << " | "
              << std::setw(15) << (naive_chain_valid ? "YES" : "NO") << "\n"
              << "========================================================================\n";

    if (vclock_inversions == 0 && naive_inversions > 0) {
        std::cout << "[SUCCESS] Project thesis validated: Logical clocks guarantee zero causal\n"
                  << "          inversions even under severe hardware oscillator drift and blackouts.\n";
    }

    return 0;
}
