/// @file main_sim.cpp
/// @brief CLI simulation runner — Week 3: uses NetworkSimulator with discrete-event
///        simulation, blackout injection, matrix clocks, and dual pipeline comparison.

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>

#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/config.hpp"
#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/matrix_clock.hpp"
#include "coldchain/network_simulator.hpp"
#include "coldchain/physical_clock.hpp"
#include "coldchain/vector_clock.hpp"

using namespace coldchain;

void print_banner() {
    std::cout << "========================================================================\n"
              << " Resilient Provenance Tracking for IoT Cold Chains via Logical Clocks\n"
              << " CS6666 Distributed Ledger Technologies — Simulation Harness (Week 3)\n"
              << "========================================================================\n\n";
}

int main(int argc, char* argv[]) {
    SimConfig cfg;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--shipments" && i + 1 < argc) {
            cfg.num_shipments = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-hours" && i + 1 < argc) {
            cfg.duration_hours = std::stod(argv[++i]);
        } else if (arg == "--blackout-mean-hours" && i + 1 < argc) {
            cfg.blackout_mean_hours = std::stod(argv[++i]);
        } else if (arg == "--sampling-interval-s" && i + 1 < argc) {
            cfg.sampling_interval_s = std::stod(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            cfg.seed = std::stoull(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ./sim [options]\n"
                      << "Options:\n"
                      << "  --shipments N              Number of shipments (default: 5)\n"
                      << "  --duration-hours H         Simulation duration in hours (default: 72)\n"
                      << "  --blackout-mean-hours H    Mean blackout duration (default: 4)\n"
                      << "  --sampling-interval-s S    Telemetry interval in seconds (default: 60)\n"
                      << "  --seed S                   Random seed (default: 42)\n"
                      << "  --verbose, -v              Print detailed logs\n"
                      << "  --help, -h                 Show this message\n";
            return 0;
        }
    }

    print_banner();
    std::cout << "[Config]\n"
              << "  Shipments:          " << cfg.num_shipments << "\n"
              << "  Duration:           " << cfg.duration_hours << " hours\n"
              << "  Sampling interval:  " << cfg.sampling_interval_s << " seconds\n"
              << "  Blackout mean:      " << cfg.blackout_mean_hours << " hours\n"
              << "  Blackout rate:      " << cfg.blackout_rate_per_hour << " /hour/node\n"
              << "  Clock drift:        ±" << cfg.drift_ppm_mean << " PPM\n"
              << "  TX jitter mean:     " << cfg.tx_jitter_mean_ms << " ms\n"
              << "  Seed:               " << cfg.seed << "\n"
              << "  Entities:           " << cfg.num_nodes
              << " (Manufacturer, Distributor, Cold Storage, Pharmacy)\n\n";

    // ── Run discrete-event simulation ────────────────────────────
    std::cout << "[Simulation] Running discrete-event network simulation...\n";
    NetworkSimulator sim(cfg);
    sim.run_full();

    std::cout << "  Generated:   " << sim.total_events_generated() << " events\n"
              << "  Blackouts:   " << sim.total_blackouts_injected() << " injected\n"
              << "  Sim clock:   " << std::fixed << std::setprecision(1)
              << sim.sim_clock_ms() / (3600.0 * 1000.0) << " hours\n\n";

    // ── Pipeline 1: Vector Clock — collect all events from all nodes ─────
    std::cout << "--- Pipeline 1: Vector Clock + Causal Merge Engine ---\n";

    std::vector<LedgerEvent> vclock_all_events;
    size_t vclock_total_blocks = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        const auto& events = sim.vclock_node(i).all_events();
        vclock_all_events.insert(vclock_all_events.end(), events.begin(), events.end());
        vclock_total_blocks += sim.vclock_node(i).chain().size();
    }

    // Topo-sort all collected events
    auto vclock_sorted = CausalMergeEngine::topo_sort(vclock_all_events);
    size_t vclock_inversions = count_causal_inversions(vclock_sorted);
    bool vclock_causal_ok = CausalMergeEngine::is_causally_valid(vclock_sorted);

    std::cout << "  Events received: " << vclock_all_events.size() << "\n"
              << "  Blocks sealed:   " << vclock_total_blocks << "\n"
              << "  Causal inversions (R_err): " << vclock_inversions << " (must be 0)\n"
              << "  Causal invariant:          " << (vclock_causal_ok ? "PASS" : "FAIL") << "\n";

    // Verify all chains
    bool vclock_chains_valid = true;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        if (!verify_chain(sim.vclock_node(i).chain())) {
            vclock_chains_valid = false;
            break;
        }
    }
    std::cout << "  Chain integrity:           " << (vclock_chains_valid ? "VALID" : "CORRUPT") << "\n";

    // Matrix clock diagnostics
    if (verbose) {
        std::cout << "\n  Matrix clock states:\n";
        for (size_t i = 0; i < sim.num_nodes(); ++i) {
            const auto& mc = sim.vclock_node(i).matrix_clock();
            std::cout << "    Node " << i << ":\n";
            for (size_t j = 0; j < mc.n(); ++j) {
                std::cout << "      [";
                for (size_t k = 0; k < mc.n(); ++k) {
                    if (k) std::cout << ", ";
                    std::cout << mc.at(j, k);
                }
                std::cout << "]\n";
            }
            VectorClock cmin = mc.column_min();
            std::cout << "      column_min = " << cmin.to_string() << "\n";
        }
    }
    std::cout << "\n";

    // ── Pipeline 2: Naive Physical-Timestamp Baseline ────────────
    std::cout << "--- Pipeline 2: Naive Physical Timestamp Baseline ---\n";

    std::vector<LedgerEvent> naive_all_events;
    size_t naive_total_blocks = 0;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        const auto& events = sim.naive_node(i).all_events();
        naive_all_events.insert(naive_all_events.end(), events.begin(), events.end());
        naive_total_blocks += sim.naive_node(i).chain().size();
    }

    auto naive_sorted = naive_physical_order(naive_all_events);
    size_t naive_inversions = count_causal_inversions(naive_sorted);
    bool naive_causal_ok = CausalMergeEngine::is_causally_valid(naive_sorted);

    std::cout << "  Events received: " << naive_all_events.size() << "\n"
              << "  Blocks sealed:   " << naive_total_blocks << "\n"
              << "  Causal inversions (R_err): " << naive_inversions << " (violations due to drift!)\n"
              << "  Causal invariant:          " << (naive_causal_ok ? "PASS" : "VIOLATION DETECTED") << "\n";

    bool naive_chains_valid = true;
    for (size_t i = 0; i < sim.num_nodes(); ++i) {
        if (!verify_chain(sim.naive_node(i).chain())) {
            naive_chains_valid = false;
            break;
        }
    }
    std::cout << "  Chain integrity:           " << (naive_chains_valid ? "VALID" : "CORRUPT") << "\n\n";

    // ── Summary Comparison ──────────────────────────────────────
    std::cout << "========================================================================\n"
              << " COMPARISON RESULTS\n"
              << "========================================================================\n"
              << " Metric                       | Vector Clock Pipeline | Naive Baseline\n"
              << "------------------------------+-----------------------+-----------------\n"
              << " Causal Inversions (R_err)    | " << std::setw(21) << vclock_inversions << " | "
              << std::setw(15) << naive_inversions << "\n"
              << " Causal Invariant Preserved?  | " << std::setw(21) << (vclock_causal_ok ? "YES (Guaranteed)" : "NO") << " | "
              << std::setw(15) << (naive_causal_ok ? "YES" : "NO (Violated)") << "\n"
              << " Chain Integrity Verified?    | " << std::setw(21) << (vclock_chains_valid ? "YES" : "NO") << " | "
              << std::setw(15) << (naive_chains_valid ? "YES" : "NO") << "\n"
              << " Total Events Generated       | " << std::setw(21) << vclock_all_events.size() << " | "
              << std::setw(15) << naive_all_events.size() << "\n"
              << " Blocks Sealed                | " << std::setw(21) << vclock_total_blocks << " | "
              << std::setw(15) << naive_total_blocks << "\n"
              << "========================================================================\n";

    if (vclock_inversions == 0 && vclock_causal_ok) {
        std::cout << "\n[SUCCESS] Vector clock pipeline guarantees zero causal inversions.\n";
        if (naive_inversions > 0) {
            std::cout << "          Naive baseline produced " << naive_inversions
                      << " causal violation(s) — project thesis validated.\n";
        }
    }

    return 0;
}
