#pragma once
/// @file pruning_engine.hpp
/// @brief Matrix-clock-driven garbage collection (pruning) of stable records.
///
/// Implements the column-min pruning invariant from report §3.3:
///   An event e is safe to prune iff  min_j M_i[j,k] >= V(e)[k]  for all k,
///   i.e. every node in the network has observed the event.
///
/// PruningEngine operates on a LedgerNode's active_state_store, removing
/// SensorEvents whose vector-clock timestamps are dominated by the global
/// knowledge horizon (MatrixClock::column_min()).

#include <cstddef>
#include <vector>

#include "coldchain/vector_clock.hpp"

namespace coldchain {

// Forward-declare to avoid circular include
class LedgerNode;

class PruningEngine {
public:
    // ── Core pruning operation ────────────────────────────────

    /// Remove all records from @p node's active_state_store whose vector-clock
    /// timestamps are dominated by the node's matrix-clock column-min horizon.
    ///
    /// @return Number of records pruned.
    static size_t prune_stable_records(LedgerNode& node);

    // ── Safety predicate (public for testing) ─────────────────

    /// Returns true iff every slot of @p event_vc is <= the corresponding
    /// slot of @p horizon — i.e. all nodes have observed this event.
    ///
    /// Formally:  ∀k : horizon[k] >= event_vc[k]
    static bool is_safe_to_prune(const VectorClock& event_vc,
                                 const VectorClock& horizon);

    // ── Diagnostics ───────────────────────────────────────────

    /// Count how many records in @p node's active_state_store are currently
    /// safe to prune, WITHOUT actually removing them.
    static size_t count_prunable(const LedgerNode& node);

    /// Compute the current pruning horizon for @p node (convenience wrapper
    /// around node.matrix_clock().column_min()).
    static VectorClock pruning_horizon(const LedgerNode& node);
};

} // namespace coldchain
