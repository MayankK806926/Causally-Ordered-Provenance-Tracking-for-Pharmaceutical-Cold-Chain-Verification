#pragma once
/// @file matrix_clock.hpp
/// @brief N×N matrix clock for tracking global knowledge across nodes.
///
/// Each node i maintains M_i[j][k] — its belief about what node j knows
/// about node k's event counter.  Row i is always equal to node i's own
/// vector clock.  merge_matrix() propagates transitive knowledge when
/// nodes exchange messages.
///
/// column_min() computes the "global knowledge horizon": the element-wise
/// minimum across all rows.  An event e is safe to prune iff
/// column_min()[k] >= e.vc[k] for all k — meaning every node has observed
/// it (report §3.3).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coldchain/vector_clock.hpp"

namespace coldchain {

class MatrixClock {
public:
    /// Construct an n×n zero-initialized matrix clock.
    explicit MatrixClock(size_t n = 4);

    // ── Row / cell access ─────────────────────────────────────

    /// Copy a vector clock into row @p self_idx (this node's own row).
    void set_own_row(const VectorClock& vc, size_t self_idx);

    /// Apply an incoming matrix clock piggybacked on a message from
    /// @p sender_idx, implementing the two update rules from §3.3:
    ///   1. Incorporate sender's own row.
    ///   2. Element-wise max over the whole matrix.
    void merge_matrix(size_t sender_idx, const MatrixClock& incoming);

    /// Read M[j][k].
    uint32_t at(size_t j, size_t k) const;

    /// Write M[j][k].
    void set(size_t j, size_t k, uint32_t val);

    // ── Pruning horizon ───────────────────────────────────────

    /// Compute min_j M[j][k] for each k — the slowest node's knowledge
    /// per dimension.  This is the pruning horizon for PruningEngine.
    VectorClock column_min() const;

    /// Return the row for node @p j as a VectorClock.
    VectorClock row(size_t j) const;

    // ── Utilities ─────────────────────────────────────────────

    /// Matrix dimension (number of nodes).
    size_t n() const;

    /// Human-readable multi-line dump for debugging.
    std::string to_string() const;

    bool operator==(const MatrixClock& other) const;

private:
    size_t n_;
    std::vector<std::vector<uint32_t>> matrix_;  // n_ × n_
};

} // namespace coldchain
