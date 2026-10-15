# Cold-Chain Vector Clock — Provenance Tracking

**CS6666: Blockchain & Distributed Ledger Technologies**
**Mayank Kumar (AE23B051)**

Causally-ordered provenance tracking for pharmaceutical cold chains using Fidge-Mattern vector clocks, with matrix-clock-based autonomous log pruning.

## Quick Start

```bash
# Configure & build
cmake -B build -S .
cmake --build build

# Run unit & integration tests (100% passing)
ctest --test-dir build --output-on-failure

# Run end-to-end simulation comparing Vector Clock vs. Naive Physical Baseline
./build/sim --shipments 5 --duration-hours 24 --seed 42

# Run parameter sweep across blackout durations (0.1h to 48h)
./build/sweep_runner --blackout-hours 0.1,0.5,1,2,4,8,16,24,48 --runs-per-point 5 --duration-hours 12 --out data/sweeps/results.csv

# Generate publication-quality figures
python experiments/plot_results.py --input data/sweeps/results.csv --output-dir data/sweeps/
```

## Project Structure

```
include/coldchain/  — public C++20 headers (Doxygen documented)
src/                — core implementation & engines
tests/              — doctest unit & adversarial safety suites
experiments/        — parameter sweep runner & Python plot scripts
data/sweeps/        — benchmark CSV traces and generated PNG plots
docs/               — design decisions & architectural notes
third_party/        — vendored single-header deps (picosha2, doctest)
```

## Status & Milestones

- [x] **Week 1:** Data model (`SensorEvent`, `CustodyEvent`), cryptographic block hashing, chain integrity tests (`test_ledger_integrity`).
- [x] **Week 2:** Fidge-Mattern `VectorClock`, topological sort (`CausalMergeEngine`), drifting physical clock baseline, unit test suites.
- [x] **Week 3:** Discrete-event `NetworkSimulator` with Poisson blackouts, log-normal durations, `IoTDevice` edge buffering, `MatrixClock` with transitive knowledge horizon (`column_min`).
- [x] **Week 4:** Autonomous `PruningEngine` using global knowledge horizons, state store garbage collection, adversarial safety suite (`test_pruning_safety`).
- [x] **Week 5:** Metrics pipeline (`R_err`, `F_liab`, `S_prune`), parameter sweep CLI (`sweep_runner`), matplotlib visualization (`plot_results.py`).
- [x] **Week 6:** Full Doxygen API comments, performance optimizations (clock sum pruning), empirical trace verification, and final submission packaging.

## Core Results & Guarantees

| Metric / Property | Vector Clock Pipeline | Naive Physical Baseline | Benefit |
|---|:---:|:---:|:---|
| **Causal Inversions ($R_{err}$)** | **0 (Guaranteed)** | > 0 (violations under drift) | 100% causal correctness |
| **False Liability ($F_{liab}$)** | **0%** | > 0% (wrong entity blamed) | Accurate excursion attribution |
| **Storage Reduction ($S_{prune}$)** | **Up to 100%** | N/A (unbounded growth) | Bounded IoT/node memory footprint |
| **Chain Integrity** | **VALID (100%)** | VALID | Cryptographically tamper-evident |
| **Deterministic Convergence** | **YES** | NO (skew-dependent) | Replicas converge identically |

