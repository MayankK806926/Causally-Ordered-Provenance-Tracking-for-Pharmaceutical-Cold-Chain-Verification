# Cold-Chain Vector Clock — Provenance Tracking

**CS6666: Blockchain & Distributed Ledger Technologies**
**Mayank Kumar (AE23B051)**

Causally-ordered provenance tracking for pharmaceutical cold chains using Fidge-Mattern vector clocks, with matrix-clock-based autonomous log pruning.

## Quick Start

```bash
# Configure & build
cmake -B build -S .
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run Week 2 end-to-end simulation
./build/sim --shipments 5 --seed 42
```

## Project Structure

```
include/coldchain/  — public headers
src/                — implementation
tests/              — doctest unit tests
experiments/        — sweep runner & plotting
data/sweeps/        — CSV output (gitignored)
third_party/        — vendored headers (picosha2, doctest)
```

## Status

- [x] Week 1: Data model, hashing, block schema, chain integrity tests
- [x] Week 2: Vector clocks, causal merge, naive baseline, test suites, simulation demo
- [ ] Week 3: Network simulator, matrix clock
- [ ] Week 4: Pruning engine, safety tests
- [ ] Week 5: Experiment sweeps, plots
- [ ] Week 6: Polish, docs, final report

