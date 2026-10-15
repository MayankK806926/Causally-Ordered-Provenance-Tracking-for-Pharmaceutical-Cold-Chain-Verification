/// @file pruning_engine.cpp
/// @brief PruningEngine implementation — matrix-clock-driven garbage collection.

#include "coldchain/pruning_engine.hpp"
#include "coldchain/ledger_node.hpp"

#include <cassert>

namespace coldchain {

// ══════════════════════════════════════════════════════════════
//  Safety predicate
// ══════════════════════════════════════════════════════════════

bool PruningEngine::is_safe_to_prune(const VectorClock& event_vc,
                                     const VectorClock& horizon) {
    assert(event_vc.size() == horizon.size());
    for (size_t k = 0; k < event_vc.size(); ++k) {
        if (horizon[k] < event_vc[k]) {
            return false;
        }
    }
    return true;
}

// ══════════════════════════════════════════════════════════════
//  Core pruning operation
// ══════════════════════════════════════════════════════════════

size_t PruningEngine::prune_stable_records(LedgerNode& node) {
    VectorClock horizon = node.matrix_clock().column_min();
    auto& store = node.active_state_store_mut();

    size_t removed = 0;
    for (auto it = store.begin(); it != store.end(); ) {
        if (is_safe_to_prune(it->second.vc, horizon)) {
            it = store.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    return removed;
}

// ══════════════════════════════════════════════════════════════
//  Diagnostics
// ══════════════════════════════════════════════════════════════

size_t PruningEngine::count_prunable(const LedgerNode& node) {
    VectorClock horizon = node.matrix_clock().column_min();
    const auto& store = node.active_state_store();

    size_t count = 0;
    for (const auto& [key, event] : store) {
        if (is_safe_to_prune(event.vc, horizon)) {
            ++count;
        }
    }
    return count;
}

VectorClock PruningEngine::pruning_horizon(const LedgerNode& node) {
    return node.matrix_clock().column_min();
}

} // namespace coldchain
