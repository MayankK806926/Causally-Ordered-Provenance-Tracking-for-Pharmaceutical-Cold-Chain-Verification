# Code Plan: Causally-Ordered Provenance Tracking for Pharmaceutical Cold-Chain Verification

**Project:** Resilient Provenance Tracking for IoT Cold Chains via Logical Clocks
**Student:** Mayank Kumar — CS6666: Blockchain and Distributed Ledger Technologies
**Language/Toolchain:** C++20, CMake, (optional) Python 3 for post-processing/plots

This document turns the report into a buildable engineering plan: repo layout, class-by-class
API, algorithms with pseudocode, the simulation harness, the metrics pipeline, a test plan, and
a week-by-week task list mapped directly to files and functions.

---

## 1. Repository Layout

```
coldchain-vclock/
├── CMakeLists.txt
├── README.md
├── include/
│   └── coldchain/
│       ├── vector_clock.hpp
│       ├── matrix_clock.hpp
│       ├── events.hpp              # SensorEvent, CustodyEvent
│       ├── hashing.hpp             # SHA-256 wrapper
│       ├── ledger_block.hpp
│       ├── iot_device.hpp
│       ├── causal_merge_engine.hpp
│       ├── ledger_node.hpp
│       ├── pruning_engine.hpp
│       ├── physical_clock.hpp      # naive baseline
│       ├── network_simulator.hpp
│       ├── metrics.hpp
│       └── config.hpp
├── src/
│   ├── vector_clock.cpp
│   ├── matrix_clock.cpp
│   ├── hashing.cpp
│   ├── ledger_block.cpp
│   ├── iot_device.cpp
│   ├── causal_merge_engine.cpp
│   ├── ledger_node.cpp
│   ├── pruning_engine.cpp
│   ├── physical_clock.cpp
│   ├── network_simulator.cpp
│   ├── metrics.cpp
│   └── main_sim.cpp                # CLI entry point for a single run
├── experiments/
│   ├── sweep_runner.cpp            # CLI entry point for parameter sweeps -> CSV
│   ├── run_all_sweeps.sh
│   └── plot_results.py             # matplotlib: Rerr, Fliab, Sprune curves
├── tests/
│   ├── test_vector_clock.cpp
│   ├── test_matrix_clock.cpp
│   ├── test_causal_merge.cpp
│   ├── test_pruning_safety.cpp
│   ├── test_ledger_integrity.cpp
│   └── test_network_simulator.cpp
├── data/
│   └── sweeps/                     # CSV output from experiments (gitignored except .gitkeep)
├── docs/
│   └── design_notes.md
└── third_party/
    └── (vendored single-header deps: catch2 or doctest, a SHA-256 header)
```

Use **CMake + FetchContent** (or a vendored single header) for:
- A tiny SHA-256 implementation (a public-domain single-header, e.g. `picosha2.h`) — avoids
  pulling in all of OpenSSL just for block hashing.
- **doctest** or **Catch2** (header-only) for unit tests.
- No blockchain framework, no external consensus library — everything is hand-rolled per the
  scope boundaries in the report.

### `CMakeLists.txt` skeleton

```cmake
cmake_minimum_required(VERSION 3.20)
project(coldchain_vclock CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -O2)

add_library(coldchain_core
    src/vector_clock.cpp
    src/matrix_clock.cpp
    src/hashing.cpp
    src/ledger_block.cpp
    src/iot_device.cpp
    src/causal_merge_engine.cpp
    src/ledger_node.cpp
    src/pruning_engine.cpp
    src/physical_clock.cpp
    src/network_simulator.cpp
    src/metrics.cpp
)
target_include_directories(coldchain_core PUBLIC include)

add_executable(sim src/main_sim.cpp)
target_link_libraries(sim coldchain_core)

add_executable(sweep_runner experiments/sweep_runner.cpp)
target_link_libraries(sweep_runner coldchain_core)

enable_testing()
add_subdirectory(tests)
```

---

## 2. Core Data Structures

### 2.1 `VectorClock` (`vector_clock.hpp`)

```cpp
class VectorClock {
public:
    explicit VectorClock(size_t n);                 // zero-initialized, size N=4

    void tick(size_t node_idx);                      // V[i]++ , local event
    void merge(const VectorClock& other);             // element-wise max

    bool precedes(const VectorClock& other) const;     // strict happens-before e_a -> e_b
    bool is_concurrent(const VectorClock& other) const; // neither precedes the other
    bool equals(const VectorClock& other) const;

    uint32_t operator[](size_t idx) const;
    size_t size() const;

    std::string to_string() const;                    // for logging / block hashing
    // needed so VectorClock can key a std::map deterministically if ever required:
    bool operator<(const VectorClock& other) const;    // lexicographic, NOT causal order

private:
    std::vector<uint32_t> clock_;
};
```

**Key correctness rule:** `operator<` (lexicographic) is only used for deterministic container
ordering (e.g. tie-breaking, map keys) — it must **never** be mistaken for `precedes()`, which
implements the actual partial order. Document this loudly in the header comment; this is the
single most common bug source in vector-clock implementations.

`precedes(other)`:
```
for k in 0..n-1: if clock_[k] > other.clock_[k] return false
return clock_ != other.clock_        // i.e. at least one strictly less
```

`is_concurrent(other)`:
```
return !this->precedes(other) && !other.precedes(*this) && !this->equals(other)
```

### 2.2 `MatrixClock` (`matrix_clock.hpp`)

```cpp
class MatrixClock {
public:
    explicit MatrixClock(size_t n);

    // Row i is node i's own belief. M[j][k] = node j's latest known event index at node k,
    // as far as *this* node can tell.
    void set_own_row(const VectorClock& vc, size_t self_idx);

    // Apply an incoming matrix (piggybacked on a message from peer i) into this node's matrix.
    void merge_matrix(size_t sender_idx, const MatrixClock& incoming);

    uint32_t at(size_t j, size_t k) const;
    void set(size_t j, size_t k, uint32_t val);

    VectorClock column_min() const;    // min_j M[j][k] for each k -> "global knowledge horizon"

    size_t n() const;

private:
    std::vector<std::vector<uint32_t>> matrix_;  // n x n
};
```

`merge_matrix` implements the two update rules from §3.3 of the report:
```
// 1. incorporate sender's own row into what we believe sender knows
for k in 0..n-1:
    M[sender][k] = max(M[sender][k], incoming.M[sender][k])

// 2. element-wise max over the whole matrix (propagate transitively-known info)
for p in 0..n-1:
    for q in 0..n-1:
        M[p][q] = max(M[p][q], incoming.M[p][q])
```

`column_min()` returns the vector clock representing “the slowest node's knowledge” per
dimension — this is exactly what `PruningEngine` compares against an event's vector-clock
timestamp.

### 2.3 Events (`events.hpp`)

```cpp
struct SensorEvent {
    uint32_t shipment_id;
    uint8_t  origin_entity_id;        // which node's IoT device generated this
    float    temperature_celsius;
    float    latitude, longitude;
    uint64_t local_monotonic_counter; // for physical-baseline pipeline only
    VectorClock vc;
    std::array<uint8_t, 32> signature; // simulated signature (see §6 "Crypto scope")
};

struct CustodyEvent {
    uint32_t shipment_id;
    uint8_t  from_entity_id;
    uint8_t  to_entity_id;
    uint64_t custody_transfer_seq;
    VectorClock vc;
    std::array<uint8_t, 32> transfer_signature;
};

using LedgerEvent = std::variant<SensorEvent, CustodyEvent>;
```

**Crypto scope note:** the report doesn't require a full PKI. Implement `signature` as
`SHA256(serialize(event) || node_secret)` — a MAC-style stand-in that proves *which simulated
node* produced the event and that it wasn't tampered with in transit, without building real
asymmetric crypto. State this simplification explicitly in the final report's scope section.

### 2.4 `LedgerBlock` (`ledger_block.hpp`)

```cpp
struct LedgerBlock {
    uint64_t block_index;
    std::vector<LedgerEvent> events;   // causally-sorted batch
    std::array<uint8_t, 32> prev_hash;
    std::array<uint8_t, 32> this_hash;  // SHA256(block_index || events || prev_hash)
    VectorClock block_vc;                // merge of all contained events' VCs
};

std::array<uint8_t,32> compute_block_hash(const LedgerBlock& b);
bool verify_chain(const std::vector<LedgerBlock>& chain); // recompute + compare hashes
```

---

## 3. Simulation-Side Components

### 3.1 `IoTDevice` (`iot_device.hpp` / `.cpp`)

```cpp
class IoTDevice {
public:
    IoTDevice(uint8_t entity_id, size_t n_nodes, uint32_t shipment_id);

    void set_online(bool online);
    bool is_online() const;

    // Called by NetworkSimulator on each simulated tick while attached to a shipment.
    SensorEvent generate_telemetry(float temperature, float lat, float lon);

    // On reconnect: return buffered events (still causally tagged) and clear the buffer.
    std::vector<SensorEvent> flush_on_reconnect();

    const VectorClock& local_clock() const;

private:
    uint8_t entity_id_;
    uint32_t shipment_id_;
    VectorClock local_vc_;
    std::vector<SensorEvent> buffer_;
    bool is_online_ = true;
    uint64_t monotonic_counter_ = 0;
};
```

`generate_telemetry`:
```
local_vc_.tick(entity_id_)
monotonic_counter_++
event = SensorEvent{ shipment_id_, entity_id_, temp, lat, lon,
                      monotonic_counter_, local_vc_ /*snapshot*/, sign(...) }
if (!is_online_) buffer_.push_back(event)
else "transmit immediately" (handled by NetworkSimulator)
return event
```

### 3.2 `CausalMergeEngine` (`causal_merge_engine.hpp` / `.cpp`)

Responsible for taking a batch of events uploaded on reconnect (or arriving out of order over
the network) and inserting them into a `LedgerNode`'s pending queue in a valid topological order
w.r.t. the happens-before relation.

```cpp
class CausalMergeEngine {
public:
    // Topologically sort `incoming` so that for every pair with e_a -> e_b, e_a appears first.
    // Concurrent events are ordered deterministically by (entity_id, local_monotonic_counter)
    // as a static tie-breaker, matching the report's §3.2 "static priority tie-breaker".
    static std::vector<LedgerEvent> topo_sort(std::vector<LedgerEvent> incoming);

    // Verify signatures on each event before they are trusted for ordering.
    static bool verify_signatures(const std::vector<LedgerEvent>& batch);
};
```

**Algorithm (Kahn's algorithm over the happens-before DAG):**
```
1. Build a comparability graph: for every pair (e_a, e_b) in incoming,
   if vc(e_a).precedes(vc(e_b)): add edge e_a -> e_b
2. Compute in-degree for each node.
3. Push all in-degree-0 nodes into a priority queue ordered by the
   deterministic tie-breaker (entity_id, monotonic_counter).
4. Repeatedly pop the lowest-priority zero-in-degree node, append to output,
   decrement in-degrees of its successors, push newly-zero nodes.
5. If output.size() != incoming.size() -> cycle detected -> assertion failure
   (should be provably impossible under vector clocks; use as a correctness
   invariant check in tests, see §5.4).
```
Complexity: O(E²) naively for the pairwise `precedes` comparisons (E = batch size), which is
fine — batches are small (bounded by blackout duration × sensor sampling rate).

### 3.3 `LedgerNode` (`ledger_node.hpp` / `.cpp`)

```cpp
class LedgerNode {
public:
    LedgerNode(uint8_t node_id, size_t n_nodes);

    // Ingest a causally-sorted batch (already run through CausalMergeEngine).
    bool ingest_batch(std::vector<LedgerEvent> sorted_batch);

    // Seal current pending events into a new block, chained to the previous hash.
    LedgerBlock seal_block();

    const std::vector<LedgerBlock>& chain() const;
    const std::map<uint32_t, SensorEvent>& active_state_store() const; // keyed by a stable id

    MatrixClock& matrix_clock();  // exposed so NetworkSimulator can piggyback exchanges

private:
    uint8_t node_id_;
    std::vector<LedgerBlock> chain_;
    std::vector<LedgerEvent> pending_;
    std::map<uint32_t, SensorEvent> active_state_store_; // for pruning demo
    MatrixClock mc_;
};
```

`ingest_batch` re-validates that the batch is already internally causally consistent
(defensive check: re-run `precedes` checks and reject/flag any inversion — this is what feeds
the naive-baseline comparison's `Rerr` counter when the *naive* pipeline is used instead).

### 3.4 `PruningEngine` (`pruning_engine.hpp` / `.cpp`)

```cpp
class PruningEngine {
public:
    // Implements the column-min pruning invariant from report §3.3 / §3.2.
    static size_t prune_stable_records(LedgerNode& node);

private:
    static bool is_safe_to_prune(const VectorClock& event_vc, const VectorClock& horizon);
};
```

```
is_safe_to_prune(event_vc, horizon):
    for k in 0..n-1: if horizon[k] < event_vc[k]: return false
    return true

prune_stable_records(node):
    horizon = node.matrix_clock().column_min()
    removed = 0
    for (id, event) in node.active_state_store():
        if is_safe_to_prune(event.vc, horizon):
            erase it; removed++
    return removed
```

This directly implements the report's condition
`min_j M_i[j,k] >= V(e_sensor)[k] for all k`.

### 3.5 `PhysicalClock` baseline (`physical_clock.hpp` / `.cpp`)

A parallel, independent pipeline used **only** to generate the comparison metrics — it must not
share any code path with the vector-clock pipeline, so the comparison is fair.

```cpp
class DriftingPhysicalClock {
public:
    DriftingPhysicalClock(double drift_ppm_mean, double drift_ppm_stddev, uint64_t seed);
    uint64_t now_ns(uint64_t sim_time_ns) const; // sim_time distorted by accumulated drift
private:
    double drift_rate_; // e.g. sampled once per device to be consistent over a run
};

// Orders a batch purely by `local_monotonic_counter`'s drifted physical-time stamp,
// with no causal awareness at all.
std::vector<LedgerEvent> naive_physical_order(std::vector<LedgerEvent> incoming);
```

Drift model per report §5.1: sample a per-device drift rate uniformly (or normally) such that
accumulated drift ranges **±100 ms/hr up to ±10 s/day**, plus an added transmission-latency
jitter term (e.g. exponential or log-normal distribution) applied at upload time.

### 3.6 `NetworkSimulator` (`network_simulator.hpp` / `.cpp`)

Discrete-event simulation core.

```cpp
struct ScheduledMessage {
    double fire_time_ms;
    uint8_t from, to;
    LedgerEvent payload;
    bool operator>(const ScheduledMessage& o) const { return fire_time_ms > o.fire_time_ms; }
};

class NetworkSimulator {
public:
    NetworkSimulator(size_t n_nodes, uint64_t seed);

    void add_shipment(uint32_t shipment_id, std::vector<uint8_t> route); // e.g. [M,D,C,P]

    // Blackout injection: node goes offline for `duration_ms` starting now.
    void inject_blackout(size_t node_id, double duration_ms);

    // Advance simulation by delta_time_ms, generating telemetry, firing scheduled
    // messages whose fire_time has arrived, and handling reconnect flush events.
    void step(double delta_time_ms);

    void run_until(double end_time_ms);

    // Access to both pipelines' resulting ledgers, for metrics computation.
    LedgerNode& vclock_node(size_t idx);
    LedgerNode& naive_node(size_t idx);

private:
    std::priority_queue<ScheduledMessage, std::vector<ScheduledMessage>, std::greater<>> queue_;
    std::vector<IoTDevice> devices_;
    std::vector<LedgerNode> vclock_nodes_;
    std::vector<LedgerNode> naive_nodes_;
    std::default_random_engine rng_;
    double sim_clock_ms_ = 0.0;
    // blackout distribution parameters, sampling rate, transit topology (M->D->C->P), etc.
};
```

Configurable distributions (put in `config.hpp` as a `SimConfig` struct so sweeps can vary
them): sampling interval (e.g. every 60s of sim-time), blackout duration distribution (e.g.
log-normal, mean tunable from 0.1h to 48h per report §5.2), blackout frequency (Poisson
process), transmission latency jitter, clock-drift parameters.

---

## 4. Metrics Pipeline (`metrics.hpp` / `.cpp`)

```cpp
struct RunMetrics {
    double causal_misordering_rate;     // R_err
    double false_liability_rate;        // F_liab
    double storage_reduction_ratio;     // S_prune at end of run
    double avg_blackout_hours;          // independent variable for plotting
};

// Compare the vector-clock pipeline's final ledger order against ground truth
// happens-before relation (which is *known* in simulation, since we generated the events).
double compute_causal_misordering_rate(const std::vector<LedgerEvent>& ordered_naive,
                                        const std::vector<LedgerEvent>& ground_truth_causal_order);

double compute_false_liability_rate(const std::vector<LedgerBlock>& naive_chain,
                                     const std::vector<TemperatureExcursionGroundTruth>& truth);

double compute_storage_reduction(size_t bytes_with_pruning, size_t bytes_unbounded);
```

Because the simulator itself generates events with known vector-clock timestamps, **ground
truth happens-before order is always available** — this is what makes `R_err` measurable: run
the naive pipeline's chosen order against the ground-truth partial order and count inverted
pairs.

`False Liability Attribution`: track, per shipment, which node had physical custody when a
simulated temperature excursion (temperature outside 2–8°C or below −70°C threshold) occurred;
then check whether the *naive* pipeline's ledger — reconstructed by whichever node appears to
have custody at that ledger-position — attributes it to the wrong (adjacent) node due to
inversion around a transfer boundary. Count mismatches / total excursions.

---

## 5. Test Plan (`tests/`, doctest or Catch2)

### 5.1 `test_vector_clock.cpp`
- `tick` increments only the caller's index.
- `merge` is idempotent and commutative for the max operation.
- `precedes` matches hand-worked examples (including the classic 3-process diagram from
  Fidge/Mattern papers).
- `is_concurrent` correctly flags non-comparable clocks.
- Property test (optional, via random generation): if `a.precedes(b)` then
  `!b.precedes(a)` and `!a.is_concurrent(b)` — antisymmetry check.

### 5.2 `test_matrix_clock.cpp`
- `merge_matrix` never decreases any entry (monotonicity).
- `column_min()` matches a hand-computed example from the report's formal definition.
- After all N nodes have pairwise-gossiped k times (k ≥ diameter of the gossip graph), every
  node's matrix converges to the same values for entries that stopped changing — simulate a
  small all-to-all gossip round and assert convergence.

### 5.3 `test_causal_merge.cpp`
- Feed a hand-built batch with known causal edges out of physical-arrival order; assert
  `topo_sort` produces an order consistent with all `precedes` edges.
- Feed a batch containing concurrent events; assert deterministic tie-break ordering is
  reproducible across repeated runs.
- Feed a batch with a corrupted signature; assert `verify_signatures` rejects it.

### 5.4 `test_pruning_safety.cpp` (the most important correctness test)
- **Adversarial regression test:** construct a scenario where an event is pruned *before* all
  nodes have observed it, and assert the invariant catches it (i.e. the test asserts
  `is_safe_to_prune` returns false until the matrix genuinely reflects full observation).
- Run a multi-round gossip simulation and assert that pruning only ever removes events whose
  vector-clock timestamp is ≤ the true global column-min at the time of pruning — cross-check
  against a naively-computed "ground truth" global minimum kept outside the class under test.
- Assert **no state divergence**: after pruning on node *i*, no other node ever ingests a
  reference to a pruned event (simulate a late/duplicate message referencing an old event and
  confirm the system still behaves correctly, e.g. via a checksum/summary rather than needing
  the full record).

### 5.5 `test_ledger_integrity.cpp`
- `verify_chain` detects a single tampered byte in any historical block.
- Hash chain recomputation matches after a full simulated run (no drift/corruption under
  normal operation).

### 5.6 `test_network_simulator.cpp`
- Deterministic run with a fixed seed reproduces identical event sequences (crucial for
  reproducible experiments/plots).
- Blackout injection actually suppresses transmission for the configured duration and buffered
  events are flushed with correct vector-clock snapshots on reconnect.

Run everything via `ctest` from the build directory; wire into a simple `make test` / CI script.

---

## 6. Experiments (`experiments/`)

### 6.1 `sweep_runner.cpp`
CLI: `./sweep_runner --blackout-hours 0.1,1,2,4,8,16,24,48 --runs-per-point 20 --out data/sweeps/results.csv`

For each blackout-duration setting, run N repetitions with different seeds, compute
`RunMetrics`, and append rows to a CSV: `blackout_hours, run_id, r_err, f_liab, s_prune`.

### 6.2 `plot_results.py`
Small pandas + matplotlib script:
- Load `results.csv`.
- Plot `R_err` vs blackout duration (naive baseline) with the vector-clock line pinned at 0 —
  reproduces the left chart in report §5.2.
- Plot storage overhead over simulated weeks (pruned vs. unbounded) — reproduces the right
  chart.
- Export both as PNG/SVG for the final report.

### 6.3 `run_all_sweeps.sh`
Wraps a full sweep across blackout duration **and** a secondary sweep across shipment volume /
sampling rate, to additionally characterize how pruning's storage savings scale with event
throughput (strengthens Objective 5 in the extended proposal).

---

## 7. Week-by-Week Plan (mapped to files)

| Week | Deliverables | Primary files touched |
|---|---|---|
| **1** | `VectorClock`? No — data model, hashing, block schema, in-memory append log | `events.hpp`, `hashing.{hpp,cpp}`, `ledger_block.{hpp,cpp}`, `test_ledger_integrity.cpp` |
| **2** | `VectorClock` class + naive baseline pipeline + causal merge logic | `vector_clock.{hpp,cpp}`, `physical_clock.{hpp,cpp}`, `causal_merge_engine.{hpp,cpp}`, `test_vector_clock.cpp`, `test_causal_merge.cpp` |
| **3** | Discrete-event `NetworkSimulator` (blackout generator, drift model) + `MatrixClock` | `network_simulator.{hpp,cpp}`, `matrix_clock.{hpp,cpp}`, `test_matrix_clock.cpp`, `test_network_simulator.cpp` |
| **4** | `PruningEngine` column-min GC + safety assertion suite + stress tests | `pruning_engine.{hpp,cpp}`, `test_pruning_safety.cpp` |
| **5** | Ablation sweeps (0.1h–48h blackout), `R_err`/`S_prune` plots | `experiments/sweep_runner.cpp`, `plot_results.py`, `run_all_sweeps.sh` |
| **6** | Cleanup, Doxygen comments, finalize `docs/design_notes.md`, compile benchmark traces into the final report | all headers (Doxygen `///` comments), `README.md` |

Suggested internal checkpoints inside each week:
- **Day 1–2:** write the header/API for the week's class(es) first, stub bodies, get it
  compiling.
- **Day 3–4:** implement + write unit tests alongside (don't defer testing to week 6).
- **Day 5:** integrate into `main_sim.cpp` so there's always a runnable end-to-end simulation,
  even if minimal, from week 2 onward.

---

## 8. Edge Cases & Known Risk Areas to Handle Explicitly in Code

1. **Concurrent event tie-breaking determinism** — must be identical across all nodes
   (deterministic function of `(entity_id, monotonic_counter)`, never wall-clock or
   insertion-order-dependent), or replicas will diverge. Cover with a dedicated test.
2. **Vector clock overflow** — `uint32_t` per slot is generous for a simulated 6-week run, but
   assert/guard against overflow in `tick()` regardless (defensive coding, cheap insurance).
3. **Late-arriving pruned references** — if a delayed/duplicated message references an event
   already pruned on the receiver, the receiver must handle this gracefully (e.g., recognize via
   a retained "tombstone"/summary hash rather than crashing or silently corrupting state).
4. **Matrix clock convergence under partial gossip topologies** — if not every node talks to
   every other node directly every round, verify `column_min` still converges through
   transitive propagation (the `merge_matrix` full element-wise max step handles this, but write
   a topology-limited gossip test to be sure).
5. **Fairness of the naive-vs-vclock comparison** — the two pipelines must consume the *exact
   same* underlying event stream and only differ in the ordering logic; structure
   `NetworkSimulator` so both `vclock_nodes_` and `naive_nodes_` receive identical inputs,
   generated once.
6. **Reproducibility** — a single seed must deterministically reproduce an entire run (event
   generation, blackout timing, drift sampling, tie-breaks) for grading/demo purposes; centralize
   all randomness behind the `NetworkSimulator`'s single `rng_`.

---

## 9. Suggested `main_sim.cpp` CLI (minimal, for demos)

```
Usage: ./sim --shipments 5 --duration-hours 72 --blackout-mean-hours 4 \
             --sampling-interval-s 60 --seed 42 [--verbose]

Prints, at the end of the run:
  - Number of causal inversions in the naive pipeline (should be > 0)
  - Number of causal inversions in the vector-clock pipeline (must be exactly 0 — the
    core correctness claim of the whole project)
  - Storage bytes retained: unbounded vs. matrix-clock-pruned
  - Chain integrity check result (verify_chain == true)
```

This single command is the fastest way to demo the project's central claim in a viva/demo: run
it once, show `naive_inversions > 0` and `vclock_inversions == 0` side by side.

---

## 10. Deliverable Checklist for the Final Submission

- [ ] All six weekly milestones compiling and tested (`ctest` green).
- [ ] `sweep_runner` output committed to `data/sweeps/results.csv` (or regenerable via a
      documented one-line command).
- [ ] `plot_results.py` output figures matching the shape of the report's §5.2 sketch charts,
      generated from real simulation data (not illustrative placeholders).
- [ ] Doxygen-style comments on all public class APIs; optionally generate HTML docs.
- [ ] `docs/design_notes.md` capturing any deviations from this plan and why.
- [ ] Final report updated with the *actual* measured `R_err`, `F_liab`, and `S_prune` numbers
      and plots (replacing the illustrative ASCII sketches in the proposal).
- [ ] A short README "Quick Start" so the project is buildable and demoable in under 5 minutes.
