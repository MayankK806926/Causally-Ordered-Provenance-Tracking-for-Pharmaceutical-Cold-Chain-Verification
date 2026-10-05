#pragma once
/// @file causal_merge_engine.hpp
/// @brief Causal merge and topological sort engine for out-of-order batches.
///
/// Implements Kahn's algorithm over the vector-clock happens-before DAG with
/// a deterministic static tie-breaker for concurrent events:
/// (origin_entity_id, monotonic_counter).
/// Guarantees zero causal inversions.

#include <functional>
#include <string>
#include <vector>

#include "coldchain/events.hpp"

namespace coldchain {

class CausalMergeEngine {
public:
    /// @brief Topologically sort incoming events according to happens-before.
    ///
    /// If e_a -> e_b (i.e. e_a.vc.precedes(e_b.vc)), e_a appears before e_b.
    /// Concurrent events are deterministically tie-broken by
    /// (origin_entity_id, monotonic_counter).
    static std::vector<LedgerEvent> topo_sort(std::vector<LedgerEvent> incoming);

    /// @brief Verify cryptographic/MAC signatures on each event in the batch.
    ///
    /// Uses default_node_secret(origin) unless a custom secret_fn is provided.
    static bool verify_signatures(const std::vector<LedgerEvent>& batch,
                                  const std::function<std::string(uint8_t)>& secret_fn = {});

    /// @brief Defensive invariant check: returns true iff no pair (i, j) with
    ///        i < j has event j strictly preceding event i.
    static bool is_causally_valid(const std::vector<LedgerEvent>& batch);
};

} // namespace coldchain
