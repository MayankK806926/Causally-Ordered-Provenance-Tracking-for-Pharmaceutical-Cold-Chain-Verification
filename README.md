# Causally-Ordered Provenance Tracking for Pharmaceutical Cold-Chain Verification

**CS6666: Blockchain and Distributed Ledger Technologies**  
**Student:** Mayank Kumar (AE23B051)  
**Implementation:** C++20, CMake, Python 3 (Matplotlib)

---

## Executive Summary

Cold-chain pharmaceutical tracking relies on continuous environmental sensing across multi-party custody transfers. However, physical quartz clocks in edge IoT sensor devices suffer oscillator drift (±20 to ±100 PPM) and network partitions (blackouts lasting hours to days). Under traditional timestamping, drifting physical clocks cause **causal inversions** ($R_{err} > 0$), misordering temperature excursions and falsely attributing liability across custody handoffs.

This project implements a fully hand-rolled, zero-external-dependency distributed ledger in **C++20** using:
1. **Fidge-Mattern Vector Clocks:** Strict happens-before partial orders with deterministic topological sorting (Kahn's algorithm) ensuring **$R_{err} \equiv 0$** under arbitrary blackout durations and clock skews.
2. **Matrix Clocks & Autonomous Log Pruning:** Global knowledge horizon tracking ($\min_j M_i[j,k]$) enabling safe garbage collection of live state stores ($S_{prune} \approx 100\%$) without compromising block chain tamper-evidence.
3. **Dual-Pipeline Benchmarking Harness:** Discrete-event network simulator exposing parallel vector-clock and naive-timestamp pipelines to identical network conditions, jitter, and blackouts.

---

## Repository Structure

```
├── coldchain-vclock/
│   ├── CMakeLists.txt              # Top-level build configuration (C++20)
│   ├── include/coldchain/          # Doxygen-annotated public C++20 headers
│   │   ├── vector_clock.hpp        # Fidge-Mattern vector clock
│   │   ├── matrix_clock.hpp        # N×N matrix clock with column_min()
│   │   ├── events.hpp              # SensorEvent and CustodyEvent definitions
│   │   ├── hashing.hpp             # SHA-256 wrapper and simulated HMAC signing
│   │   ├── ledger_block.hpp        # Block schema and cryptographic hash-chaining
│   │   ├── iot_device.hpp          # Edge IoT device with offline buffering
│   │   ├── causal_merge_engine.hpp # Topological sorting DAG & deterministic tie-breaking
│   │   ├── ledger_node.hpp         # Ledger node with active state store
│   │   ├── pruning_engine.hpp      # Causal-safe state store garbage collection
│   │   ├── physical_clock.hpp      # Drifting physical clock baseline
│   │   ├── network_simulator.hpp   # Discrete-event simulation core
│   │   ├── metrics.hpp             # R_err, F_liab, S_prune calculation pipeline
│   │   └── config.hpp              # Central simulation configuration
│   ├── src/                        # Core C++ implementation files
│   ├── tests/                      # 6 comprehensive test suites (doctest)
│   ├── experiments/                # Parameter sweep runner & Python plot scripts
│   ├── data/sweeps/                # Output CSVs and publication-ready figures
│   ├── docs/design_notes.md        # Architectural decisions and design trade-offs
│   └── third_party/                # Single-header dependencies (picosha2, doctest)
├── Cold_Chain_Vector_Clock_Code_Plan.md  # Engineering blueprint & milestone schedule
└── Project_Idea_Report_AE23B051.pdf      # Original theoretical project proposal
```

---

## Quick Start & Verification

### 1. Build from Source
```bash
cd coldchain-vclock
cmake -B build -S .
cmake --build build
```

### 2. Run Test Suite (100% Pass)
```bash
ctest --test-dir build --output-on-failure
```
Tests pass all 6 suites:
- `test_ledger_integrity`: SHA-256 block hash chaining & tamper detection
- `test_vector_clock`: Strict happens-before, concurrency, and monotonic tick properties
- `test_causal_merge`: Kahn's topological sort, reversed batch recovery, deterministic tie-breaking
- `test_matrix_clock`: Monotonic matrix propagation and column-min horizon convergence
- `test_network_simulator`: Discrete-event delivery, jitter, blackout buffering & reconnect flush
- `test_pruning_safety`: Adversarial safety checks, zero state divergence, post-prune chain validity

### 3. Run Comparative Simulation Demo
```bash
./build/sim --shipments 5 --duration-hours 24 --seed 42
```
Outputs side-by-side comparison proving $R_{err} = 0$ for vector clocks while showing naive physical baseline violations.

### 4. Run Parameter Sweeps & Generate Plots
```bash
# Execute Monte Carlo parameter sweep across 0.1h to 48h blackout durations
./build/sweep_runner --blackout-hours 0.1,0.5,1,2,4,8,16,24,48 --runs-per-point 5 --duration-hours 12 --out data/sweeps/results.csv

# Render publication-quality PNG figures
python experiments/plot_results.py --input data/sweeps/results.csv --output-dir data/sweeps/
```

---

## Core Results & Guarantees

| Metric / Evaluation Criterion | Vector Clock Pipeline | Naive Physical Baseline | Engineering Impact |
|---|:---:|:---:|:---|
| **Causal Inversion Rate ($R_{err}$)** | **0.0000 (Guaranteed)** | > 0.0000 (Violations under drift) | Eliminates out-of-order provenance corruption |
| **False Liability Attribution ($F_{liab}$)** | **0.00%** | Up to 15%+ around handoffs | Prevents wrongful penalties for cold-chain excursions |
| **Storage Reduction Ratio ($S_{prune}$)** | **Up to 100%** | 0% (Unbounded growth) | Enables sustained operation on constrained IoT edge storage |
| **Cryptographic Tamper-Evidence** | **100% Valid** | 100% Valid | Immutable block chain decoupled from prunable working state |
| **Replica Convergence** | **Deterministic** | Divergent under skew | Consensus-free replica synchronization via static tie-breaking |
