/// @file sweep_runner.cpp
/// @brief Week 5: Parameter sweep runner — varies blackout duration, collects
///        R_err / F_liab / S_prune across N repetitions, outputs CSV.
///
/// Usage:
///   ./sweep_runner --blackout-hours 0.1,1,2,4,8,16,24,48 --runs-per-point 20 \
///                  --out data/sweeps/results.csv
///
/// For each blackout-duration setting, runs N repetitions with different seeds,
/// computes RunMetrics, and appends rows to the output CSV.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "coldchain/config.hpp"
#include "coldchain/metrics.hpp"

using namespace coldchain;

// ── Parse comma-separated doubles ────────────────────────────

static std::vector<double> parse_doubles(const std::string& s) {
    std::vector<double> result;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) {
        result.push_back(std::stod(token));
    }
    return result;
}

// ── Main ─────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Default sweep parameters
    std::vector<double> blackout_hours = {0.1, 1.0, 2.0, 4.0, 8.0, 16.0, 24.0, 48.0};
    size_t runs_per_point = 20;
    std::string output_path = "data/sweeps/results.csv";
    SimConfig base_cfg;  // defaults from config.hpp

    // Optional secondary sweep dimensions
    std::vector<double> sampling_intervals;  // if provided, sweep this too
    bool sweep_sampling = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--blackout-hours" && i + 1 < argc) {
            blackout_hours = parse_doubles(argv[++i]);
        } else if (arg == "--runs-per-point" && i + 1 < argc) {
            runs_per_point = std::stoul(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--shipments" && i + 1 < argc) {
            base_cfg.num_shipments = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-hours" && i + 1 < argc) {
            base_cfg.duration_hours = std::stod(argv[++i]);
        } else if (arg == "--sampling-intervals" && i + 1 < argc) {
            sampling_intervals = parse_doubles(argv[++i]);
            sweep_sampling = true;
        } else if (arg == "--gossip-rounds" && i + 1 < argc) {
            base_cfg.gossip_rounds = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--base-seed" && i + 1 < argc) {
            base_cfg.seed = std::stoull(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: ./sweep_runner [options]\n"
                << "Options:\n"
                << "  --blackout-hours H1,H2,...   Blackout durations to sweep (default: 0.1,...,48)\n"
                << "  --runs-per-point N           Repetitions per setting (default: 20)\n"
                << "  --out PATH                   CSV output path (default: data/sweeps/results.csv)\n"
                << "  --shipments N                Number of shipments (default: 5)\n"
                << "  --duration-hours H           Simulation duration (default: 72)\n"
                << "  --sampling-intervals S1,S2   Secondary sweep: telemetry intervals in seconds\n"
                << "  --gossip-rounds N            Matrix clock gossip rounds (default: 3)\n"
                << "  --base-seed S                Base random seed (default: 42)\n"
                << "  --help, -h                   Show this message\n";
            return 0;
        }
    }

    // Default sampling intervals if secondary sweep requested but none given
    if (!sweep_sampling) {
        sampling_intervals = {base_cfg.sampling_interval_s};
    }

    // Open output file
    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        std::cerr << "ERROR: cannot open output file: " << output_path << "\n";
        return 1;
    }

    // Write CSV header (extended with sampling_interval_s column)
    ofs << "sampling_interval_s," << metrics_csv_header() << "\n";

    // Banner
    std::cout << "========================================================================\n"
              << " Sweep Runner — Causally-Ordered Provenance Tracking\n"
              << "========================================================================\n"
              << " Blackout hours:     ";
    for (auto h : blackout_hours) std::cout << h << " ";
    std::cout << "\n Sampling intervals: ";
    for (auto s : sampling_intervals) std::cout << s << " ";
    std::cout << "\n Runs per point:     " << runs_per_point
              << "\n Shipments:          " << base_cfg.num_shipments
              << "\n Duration:           " << base_cfg.duration_hours << " hours"
              << "\n Output:             " << output_path
              << "\n========================================================================\n\n";

    size_t total_runs = blackout_hours.size() * sampling_intervals.size() * runs_per_point;
    size_t completed = 0;

    for (double samp_int : sampling_intervals) {
        for (double bh : blackout_hours) {
            // Accumulators for summary statistics
            double sum_rerr = 0.0, sum_fliab = 0.0, sum_sprune = 0.0;
            size_t sum_naive_inv = 0, sum_vc_inv = 0;

            for (size_t run = 0; run < runs_per_point; ++run) {
                SimConfig cfg = base_cfg;
                cfg.blackout_mean_hours = bh;
                cfg.sampling_interval_s = samp_int;
                cfg.seed = base_cfg.seed + run * 1000 + static_cast<uint64_t>(bh * 100);

                RunMetrics m = run_single_experiment(cfg);

                // Write CSV row
                ofs << std::fixed << std::setprecision(1) << samp_int << ","
                    << metrics_to_csv_row(m, run) << "\n";
                ofs.flush();

                sum_rerr      += m.causal_misordering_rate;
                sum_fliab     += m.false_liability_rate;
                sum_sprune    += m.storage_reduction_ratio;
                sum_naive_inv += m.naive_inversions;
                sum_vc_inv    += m.vclock_inversions;

                ++completed;
            }

            // Progress report
            double avg_rerr  = sum_rerr  / runs_per_point;
            double avg_fliab = sum_fliab / runs_per_point;
            double avg_sprune = sum_sprune / runs_per_point;

            std::cout << std::fixed << std::setprecision(2)
                      << " [" << completed << "/" << total_runs << "] "
                      << "samp=" << samp_int << "s, "
                      << "blackout=" << bh << "h: "
                      << "R_err=" << std::setprecision(4) << avg_rerr
                      << "  F_liab=" << avg_fliab
                      << "  S_prune=" << avg_sprune
                      << "  naive_inv_total=" << sum_naive_inv
                      << "  vc_inv_total=" << sum_vc_inv
                      << std::endl;
        }
    }

    ofs.close();

    std::cout << "\n========================================================================\n"
              << " Sweep complete. " << completed << " runs written to: " << output_path << "\n"
              << "========================================================================\n";

    return 0;
}
