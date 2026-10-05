/// @file ledger_block.cpp
/// @brief Block construction, hashing, and chain integrity verification.

#include "coldchain/ledger_block.hpp"
#include "coldchain/hashing.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace coldchain {

// ── Helpers ──────────────────────────────────────────────────

/// Deterministic serialization of a block's content (excluding this_hash).
static std::string serialize_block_content(const LedgerBlock& block) {
    std::ostringstream os;
    os << block.block_index << '|';

    // Serialize each event deterministically.
    for (const auto& ev : block.events)
        os << event_serialize(ev) << ';';

    // Append prev_hash as hex.
    for (uint8_t b : block.prev_hash) {
        static const char hex[] = "0123456789abcdef";
        os << hex[b >> 4] << hex[b & 0xF];
    }

    return os.str();
}

// ── Public API ───────────────────────────────────────────────

Hash256 compute_block_hash(const LedgerBlock& block) {
    return sha256_bytes(serialize_block_content(block));
}

LedgerBlock seal_block(uint64_t                 block_index,
                       std::vector<LedgerEvent> events,
                       const Hash256&           prev_hash,
                       size_t                   num_nodes) {
    LedgerBlock blk;
    blk.block_index = block_index;
    blk.events      = std::move(events);
    blk.prev_hash   = prev_hash;
    blk.block_vc    = VectorClock(num_nodes);

    // Build summary VC: element-wise max over all events.
    for (const auto& ev : blk.events)
        blk.block_vc.merge(event_vc(ev));

    blk.this_hash = compute_block_hash(blk);
    return blk;
}

bool verify_chain(const std::vector<LedgerBlock>& chain) {
    if (chain.empty()) return true;

    // Block 0 must chain from genesis.
    Hash256 gen = genesis_hash();
    if (chain[0].prev_hash != gen) return false;

    for (size_t i = 0; i < chain.size(); ++i) {
        // Recompute hash and compare.
        Hash256 recomputed = compute_block_hash(chain[i]);
        if (recomputed != chain[i].this_hash) return false;

        // Check linkage to previous block.
        if (i > 0 && chain[i].prev_hash != chain[i - 1].this_hash)
            return false;
    }
    return true;
}

Hash256 genesis_hash() {
    Hash256 h{};
    h.fill(0);
    return h;
}

} // namespace coldchain
