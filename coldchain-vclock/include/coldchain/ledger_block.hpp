#pragma once
/// @file ledger_block.hpp
/// @brief Append-only, hash-chained ledger block for the cold-chain.
///
/// Each block contains a causally-sorted batch of LedgerEvents,
/// a back-link to the previous block's hash, and a summary VectorClock
/// (element-wise max of all contained events' clocks).

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "coldchain/events.hpp"
#include "coldchain/vector_clock.hpp"

namespace coldchain {

struct LedgerBlock {
    uint64_t                 block_index;
    std::vector<LedgerEvent> events;       ///< causally-sorted batch
    Hash256                  prev_hash;    ///< hash of the preceding block
    Hash256                  this_hash;    ///< SHA256(block_index || events || prev_hash)
    VectorClock              block_vc;     ///< element-wise max of all events' VCs
};

// ── Block operations ──────────────────────────────────────────

/// Compute the SHA-256 hash of a block (deterministic serialization).
Hash256 compute_block_hash(const LedgerBlock& block);

/// Build a block from a sorted event batch, chaining to @p prev_hash.
/// Automatically computes block_vc and this_hash.
LedgerBlock seal_block(uint64_t                        block_index,
                       std::vector<LedgerEvent>        events,
                       const Hash256&                  prev_hash,
                       size_t                          num_nodes);

/// Verify the hash chain of an entire ledger — recompute every hash and
/// check prev_hash linkage.  Returns true iff the chain is intact.
bool verify_chain(const std::vector<LedgerBlock>& chain);

/// Genesis hash (all zeros) used as prev_hash for the first block.
Hash256 genesis_hash();

} // namespace coldchain
