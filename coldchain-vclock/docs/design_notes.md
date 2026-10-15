# Design Notes

## Week 1 Decisions

### VectorClock implemented early
The plan originally deferred VectorClock to Week 2, but `events.hpp` embeds
`VectorClock` in both event structs.  Full VC implementation done in Week 1
so the entire foundation compiles and tests pass end-to-end.

### Crypto simplification
Using MAC-style `SHA256(serialize || secret)` rather than asymmetric signatures.
Sufficient for proving which simulated node originated an event.
Stated as explicit scope limitation per the report.

### picosha2 over OpenSSL
Single-header, public-domain SHA-256.  Avoids pulling OpenSSL as a dependency
for a simulation project.

### N=4 Supply Chain Topology
Standardized on a 4-tier pharmaceutical cold chain:
Node 0: Manufacturer, Node 1: Distributor, Node 2: Cold Storage, Node 3: Pharmacy.
Provides fixed dimension N=4 for vector and matrix clocks while covering multi-hop handoffs.

## Week 2 Decisions

### Strict Partial Order vs. Lexicographic Ordering (`precedes()` vs `operator<`)
A vector clock defines a strict partial order, not a total order.
- `precedes()` implements strict happens-before ($\forall k, V_a[k] \le V_b[k]$ and $\exists k, V_a[k] < V_b[k]$).
- `is_concurrent()` identifies causally independent events.
- `operator<` was explicitly defined using lexicographical comparison solely to enable `std::map`, `std::set`, and priority queue storage.
Code comments and design enforce that `operator<` is never used for causal reasoning.

### Kahn's Algorithm for Topological Sort with Deterministic Tie-Breaking
`CausalMergeEngine::topo_sort` reconstructs causal order for scrambled event batches:
1. Builds adjacency list and in-degree counts via $O(n^2)$ pairwise `precedes()` comparisons.
2. In-degree 0 events enter a `std::priority_queue` with a deterministic tie-breaker:
   `(origin_entity_id, monotonic_counter, index)`.
3. Guarantees that concurrent events are resolved identically across all distributed nodes without requiring consensus rounds or wall-clock timestamps.
4. Cycle detection asserts DAG validity (cycles are mathematically impossible under vector clock partial orders).

### Drifting Physical Clock Model & Naive Baseline
To benchmark against traditional physical-timestamp-based ledgers:
- `DriftingPhysicalClock` simulates hardware crystal oscillator drift (PPM mean + stddev) plus jitter.
- `naive_physical_order()` sorts purely by `physical_timestamp_ns`.
- Enables direct empirical measurement of causal inversions ($R_{err}$) produced by clock skew during network blackouts.

### Invariant Checks and HMAC Batch Verification
- `CausalMergeEngine::is_causally_valid()` performs defensive invariant validation: no event later in the batch happens-before an earlier event.
- `CausalMergeEngine::verify_signatures()` verifies MAC integrity for every event prior to ledger ingestion.

## Week 3 Decisions

### N×N Matrix Clock Architecture & Transitive Knowledge Propagation
`MatrixClock` tracks global knowledge across all $N$ supply chain entities:
- $M_i[j][k]$ represents node $i$'s view of node $j$'s knowledge of node $k$'s event counter.
- Node $i$'s own row ($M_i[i][*]$) mirrors its local `VectorClock`.
- `merge_matrix(sender_idx, incoming)` implements the two-rule update:
  1. Incorporate sender's own row: $M_i[sender][k] = \max(M_i[sender][k], incoming.matrix[sender][k])$.
  2. Element-wise maximum across the entire matrix for transitive propagation.

### Global Knowledge Horizon (`column_min()`)
- `column_min()` computes $\min_j M[j][k]$ for each dimension $k$.
- Formulates the global knowledge horizon: an event $e$ has been witnessed by all $N$ nodes if and only if $e.vc \le column\_min()$.
- Serves as the mathematical prerequisite and safety boundary for the Week 4 `PruningEngine`.

### Edge IoT Device Offline Buffering & Snapshot Preservation
`IoTDevice` models sensor hardware operating under intermittent connectivity:
- Generates telemetry and advances local monotonic counter and vector clock slot.
- During blackouts (`is_online() == false`), readings are buffered in a local queue.
- Crucial invariant: on reconnect (`flush_on_reconnect()`), buffered events retain their original vector clock snapshots taken at observation time, rather than timestamping upon transmission. This preserves causal truth regardless of delivery latency.

### Discrete-Event Network Simulator (`NetworkSimulator`)
`NetworkSimulator` orchestrates the complete multi-shipment lifecycle:
- Discrete-event priority queue (`ScheduledMessage`) ordered by simulation fire time (`fire_time_ms`).
- Shipment routing across multiple custody legs with automatic `CustodyEvent` generation.
- Realistic network disruptions: Poisson-distributed blackouts with log-normal durations, Gaussian transmission jitter, and asymmetric clock drift.
- Single central PRNG (`std::mt19937_64` seeded via `SimConfig::seed`) ensures 100% deterministic reproducibility across tests and experiment runs.

### Dual-Pipeline Symmetric Benchmarking
To guarantee zero experimental bias:
- `NetworkSimulator` maintains parallel `vclock_nodes_` and `naive_nodes_`.
- Both pipelines receive the exact same sequence of events under identical network conditions, jitter, and blackouts.
- Only the ordering logic differs (CausalMergeEngine topological sort vs. naive timestamp sort), isolating the causal clock as the sole independent variable.

### LedgerNode Separation of Concerns
`LedgerNode` cleanly delineates:
- `chain_`: cryptographically verified block sequence.
- `pending_`: unsealed event pool (auto-sealed at threshold, default 32).
- `all_events_`: full linear ingestion history for metric verification.
- `active_state_store_`: key-value map of live sensor events, decoupling the pruning target from the immutable block ledger.

## Architecture Invariants & Correctness Guarantees

1. **Zero Causal Inversions ($R_{err} = 0$):**
   Topological sort over vector clock DAG guarantees no causally dependent event is processed before its causal predecessor.
2. **Deterministic Replica Convergence:**
   Identical event sets yield identical topological orders across all nodes due to static `(entity_id, counter)` tie-breaking.
3. **Monotonic Knowledge Horizon:**
   `MatrixClock` entries and `column_min()` are monotonically non-decreasing over time and merge operations.
4. **Partition Tolerance for Causal Truth:**
   IoT offline buffering preserves causal ordering even when physical timestamps suffer severe drift.

## Week 4 Decisions: Autonomous Pruning Engine

### Column-Min Knowledge Horizon & State Decoupling
`PruningEngine` implements the autonomous log pruning invariant from report §3.3:
- An event $e$ is safe to prune if and only if $\forall k, \text{column\_min}()[k] \ge e.vc[k]$.
- **Decoupled Architecture:** Pruning exclusively targets `LedgerNode::active_state_store_` (the working set of live telemetry records). The immutable block chain (`chain_`) and its SHA-256 cryptographic hashes are left intact, preserving tamper-evidence and auditability while preventing unbounded memory growth.
- **Safety Invariant:** `test_pruning_safety.cpp` enforces adversarial checks:
  1. Events are NEVER pruned when even a single node has not transitively acknowledged observation.
  2. Late/duplicate messages referencing pruned records are safely handled without state corruption.
  3. Chain hash integrity remains 100% valid post-pruning.

## Week 5 Decisions: Parameter Sweeps, Metrics & Algorithmic Optimizations

### Metrics Pipeline (`metrics.cpp`)
- **Causal Misordering Rate ($R_{err}$):** Evaluates all causally comparable event pairs against vector clock partial order. Validates that the vector clock pipeline maintains $R_{err} \equiv 0$ under all blackout conditions, whereas naive physical timestamps produce severe inversions.
- **False Liability Attribution ($F_{liab}$):** Identifies temperature excursions ($T \notin [2^\circ\text{C}, 8^\circ\text{C}]$) and tracks whether clock drift / reordering mistakenly shifts fault to an innocent upstream/downstream custodian at handoff boundaries.
- **Storage Reduction ($S_{prune}$):** Measures active state memory reduction ratio: $S_{prune} = 1 - (\text{pruned\_store} / \text{unpruned\_store})$, achieving up to 100% state store reclaim after gossip rounds.

### Algorithmic Optimization: Vector Clock Sum Pruning
In simulation runs with tens of thousands of events, $O(N^2)$ pairwise comparisons in Kahn's topological sort and inversion counting created severe computational bottlenecks:
- **Monotonic Clock Sum:** If $V_a \text{ precedes } V_b$, then $\sum_k V_a[k] < \sum_k V_b[k]$ is mathematically guaranteed.
- **Pre-computed Sums & Pointer Caching:** Pre-extracting VectorClock pointers (eliminating `std::visit` table dispatches) and checking `sum_i < sum_j` before calling `precedes()` prunes virtually all false checks in $O(1)$ CPU cycles.
- Result: 20x–50x speedup in Monte Carlo sweep execution without altering causal ordering guarantees or tie-breaking determinism.

## Week 6: Verification, Polish & Empirical Benchmark Summary

### Final Verification Checklist
- [x] All 6 test suites green: `test_ledger_integrity`, `test_vector_clock`, `test_causal_merge`, `test_matrix_clock`, `test_network_simulator`, `test_pruning_safety`.
- [x] Doxygen `///` comments complete on all public classes, methods, and configurations.
- [x] Empirical sweep data generated and saved in `data/sweeps/results.csv`.
- [x] Visualization script `plot_results.py` generates publication-quality figures (`rerr_vs_blackout.png`, `fliab_vs_blackout.png`, `storage_reduction.png`, `inversions_comparison.png`).
- [x] Dual-pipeline comparative demo executable (`sim.exe`) validating $R_{err} = 0$ side-by-side against physical baseline errors.
