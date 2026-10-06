/// @file ledger_node.cpp
/// @brief LedgerNode implementation — ingestion, sealing, and state store management.

#include "coldchain/ledger_node.hpp"
#include "coldchain/causal_merge_engine.hpp"

#include <cassert>

namespace coldchain {

LedgerNode::LedgerNode(uint8_t node_id, size_t n_nodes)
    : node_id_(node_id), n_nodes_(n_nodes), mc_(n_nodes) {}

bool LedgerNode::ingest_batch(std::vector<LedgerEvent> sorted_batch) {
    // Defensive causal validity check
    bool valid = CausalMergeEngine::is_causally_valid(sorted_batch);

    for (auto& ev : sorted_batch) {
        all_events_.push_back(ev);
        pending_.push_back(ev);

        // If it's a SensorEvent, also insert into the active state store
        if (auto* se = std::get_if<SensorEvent>(&ev)) {
            active_state_store_[next_state_key_++] = *se;
        }
    }

    // Update own row in matrix clock to reflect latest knowledge
    if (!sorted_batch.empty()) {
        // Merge all incoming VCs into a summary VC for this batch
        VectorClock summary(n_nodes_);
        for (const auto& ev : sorted_batch)
            summary.merge(event_vc(ev));
        // Update our own VC knowledge to at least the batch summary
        mc_.set_own_row(summary, node_id_);
    }

    return valid;
}

LedgerBlock LedgerNode::seal_block() {
    assert(!pending_.empty() && "seal_block: no pending events");

    Hash256 prev = chain_.empty() ? genesis_hash() : chain_.back().this_hash;
    auto blk = coldchain::seal_block(chain_.size(), std::move(pending_), prev, n_nodes_);
    pending_.clear();
    chain_.push_back(blk);
    return blk;
}

size_t LedgerNode::auto_seal(size_t threshold) {
    size_t sealed = 0;
    while (pending_.size() >= threshold) {
        std::vector<LedgerEvent> batch(pending_.begin(), pending_.begin() + threshold);
        pending_.erase(pending_.begin(), pending_.begin() + threshold);

        Hash256 prev = chain_.empty() ? genesis_hash() : chain_.back().this_hash;
        auto blk = coldchain::seal_block(chain_.size(), std::move(batch), prev, n_nodes_);
        chain_.push_back(blk);
        ++sealed;
    }
    return sealed;
}

const std::vector<LedgerBlock>& LedgerNode::chain() const     { return chain_; }
size_t LedgerNode::pending_count() const                       { return pending_.size(); }
const std::vector<LedgerEvent>& LedgerNode::all_events() const { return all_events_; }

const std::map<uint64_t, SensorEvent>& LedgerNode::active_state_store() const {
    return active_state_store_;
}
std::map<uint64_t, SensorEvent>& LedgerNode::active_state_store_mut() {
    return active_state_store_;
}

MatrixClock&       LedgerNode::matrix_clock()       { return mc_; }
const MatrixClock& LedgerNode::matrix_clock() const { return mc_; }

uint8_t LedgerNode::node_id() const { return node_id_; }

} // namespace coldchain
