/// @file causal_merge_engine.cpp
/// @brief Implementation of Kahn's topological sort over vector clock partial orders.

#include "coldchain/causal_merge_engine.hpp"
#include "coldchain/hashing.hpp"

#include <cassert>
#include <queue>

namespace coldchain {

namespace {

struct TieBreaker {
    const std::vector<LedgerEvent>* events;

    bool operator()(size_t a, size_t b) const {
        uint8_t orig_a = event_origin((*events)[a]);
        uint8_t orig_b = event_origin((*events)[b]);
        if (orig_a != orig_b) return orig_a > orig_b;

        uint64_t cnt_a = event_counter((*events)[a]);
        uint64_t cnt_b = event_counter((*events)[b]);
        if (cnt_a != cnt_b) return cnt_a > cnt_b;

        return a > b;
    }
};

} // namespace

std::vector<LedgerEvent> CausalMergeEngine::topo_sort(std::vector<LedgerEvent> incoming) {
    const size_t n = incoming.size();
    if (n <= 1) return incoming;

    // Pre-extract vector clocks to avoid std::visit in O(n^2) loop
    std::vector<const VectorClock*> vcs(n);
    for (size_t i = 0; i < n; ++i) {
        vcs[i] = &event_vc(incoming[i]);
    }

    // 1. Build adjacency list for strict happens-before relation: e_i -> e_j
    std::vector<uint32_t> sums(n);
    for (size_t i = 0; i < n; ++i) {
        sums[i] = vcs[i]->sum();
    }

    std::vector<std::vector<size_t>> adj(n);
    std::vector<size_t> in_degree(n, 0);

    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = *vcs[i];
        uint32_t sum_i = sums[i];
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            // Prune: vc_i can only precede vc_j if sum_i < sums[j]
            if (sum_i < sums[j] && vc_i.precedes(*vcs[j])) {
                adj[i].push_back(j);
                ++in_degree[j];
            }
        }
    }

    // 2. Priority queue for in-degree-0 nodes with deterministic tie-breaker
    TieBreaker comp{&incoming};
    std::priority_queue<size_t, std::vector<size_t>, TieBreaker> pq(comp);

    for (size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) {
            pq.push(i);
        }
    }

    // 3. Process nodes via Kahn's algorithm
    std::vector<LedgerEvent> sorted;
    sorted.reserve(n);

    while (!pq.empty()) {
        size_t curr = pq.top();
        pq.pop();
        sorted.push_back(incoming[curr]);

        for (size_t succ : adj[curr]) {
            --in_degree[succ];
            if (in_degree[succ] == 0) {
                pq.push(succ);
            }
        }
    }

    // Cycle check (provably impossible under valid vector clock partial order)
    assert(sorted.size() == n && "CausalMergeEngine: cycle detected in vector clock DAG!");

    return sorted;
}

bool CausalMergeEngine::verify_signatures(const std::vector<LedgerEvent>& batch,
                                          const std::function<std::string(uint8_t)>& secret_fn) {
    for (const auto& ev : batch) {
        uint8_t origin = event_origin(ev);
        std::string secret = secret_fn ? secret_fn(origin) : default_node_secret(origin);

        bool valid = std::visit([&](const auto& e) -> bool {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, SensorEvent>) {
                return verify_signature(e.serialize(), secret, e.signature);
            } else {
                return verify_signature(e.serialize(), secret, e.transfer_signature);
            }
        }, ev);

        if (!valid) return false;
    }
    return true;
}

bool CausalMergeEngine::is_causally_valid(const std::vector<LedgerEvent>& batch) {
    const size_t n = batch.size();
    std::vector<const VectorClock*> vcs(n);
    std::vector<uint32_t> sums(n);
    for (size_t i = 0; i < n; ++i) {
        vcs[i] = &event_vc(batch[i]);
        sums[i] = vcs[i]->sum();
    }
    for (size_t i = 0; i < n; ++i) {
        const auto& vc_i = *vcs[i];
        uint32_t sum_i = sums[i];
        for (size_t j = i + 1; j < n; ++j) {
            if (sums[j] < sum_i && vcs[j]->precedes(vc_i)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace coldchain
