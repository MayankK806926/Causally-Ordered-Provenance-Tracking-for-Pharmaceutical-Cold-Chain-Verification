#pragma once
/// @file ledger_node.hpp
/// @brief Per-entity ledger node that ingests sorted batches and seals blocks.
///
/// Each supply-chain entity runs one LedgerNode.  It maintains:
///   - A local hash-chained block ledger.
///   - A matrix clock for pruning-horizon tracking.
///   - An active state store for live sensor records (pruning target).

#include <cstdint>
#include <map>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/matrix_clock.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

class LedgerNode {
public:
    /// @param node_id   Index of this node in [0, n_nodes).
    /// @param n_nodes   Total number of entities (dimension of VectorClock / MatrixClock).
    LedgerNode(uint8_t node_id, size_t n_nodes);

    // ── Ingestion ─────────────────────────────────────────────

    /// Ingest a causally-sorted batch (already run through CausalMergeEngine).
    /// Returns true if the batch passed the defensive causal validity check.
    bool ingest_batch(std::vector<LedgerEvent> sorted_batch);

    /// Seal current pending events into a new block, chaining to the
    /// previous block's hash.  Returns the newly-sealed block.
    /// Pre: at least one pending event exists.
    LedgerBlock seal_block();

    /// Auto-seal: seal blocks once pending reaches @p threshold events.
    /// Seals as many complete blocks as possible.
    size_t auto_seal(size_t threshold = 32);

    // ── Chain access ──────────────────────────────────────────

    const std::vector<LedgerBlock>& chain() const;
    size_t pending_count() const;

    /// All events ever ingested (flattened from pending + sealed blocks),
    /// in ingestion order — useful for metrics computation.
    const std::vector<LedgerEvent>& all_events() const;

    // ── Active state store (for pruning) ──────────────────────

    const std::map<uint64_t, SensorEvent>& active_state_store() const;
    std::map<uint64_t, SensorEvent>& active_state_store_mut();

    // ── Matrix clock ──────────────────────────────────────────

    MatrixClock& matrix_clock();
    const MatrixClock& matrix_clock() const;

    uint8_t node_id() const;

private:
    uint8_t node_id_;
    size_t  n_nodes_;
    std::vector<LedgerBlock> chain_;
    std::vector<LedgerEvent> pending_;
    std::vector<LedgerEvent> all_events_;  ///< complete ingestion history
    std::map<uint64_t, SensorEvent> active_state_store_;
    MatrixClock mc_;
    uint64_t next_state_key_ = 0;          ///< monotonic key for active_state_store_
};

} // namespace coldchain
