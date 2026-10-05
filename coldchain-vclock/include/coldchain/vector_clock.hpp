#pragma once
/// @file vector_clock.hpp
/// @brief Fidge-Mattern vector clock for N-node causal ordering.
///
/// IMPORTANT: operator< is LEXICOGRAPHIC — used ONLY for deterministic
/// container ordering (map keys, tie-breaking). It does NOT represent
/// causal order. Use precedes() / is_concurrent() for happens-before.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace coldchain {

class VectorClock {
public:
    /// Construct a zero-initialized clock of dimension @p n.
    explicit VectorClock(size_t n = 4);

    // ── Mutation ──────────────────────────────────────────────
    /// Increment this node's own slot.  Asserts idx < size().
    void tick(size_t node_idx);

    /// Element-wise max merge with @p other (must be same dimension).
    void merge(const VectorClock& other);

    // ── Causal queries ────────────────────────────────────────
    /// True iff *this strictly happens-before @p other.
    bool precedes(const VectorClock& other) const;

    /// True iff neither clock precedes the other (and they differ).
    bool is_concurrent(const VectorClock& other) const;

    /// True iff all slots equal.
    bool equals(const VectorClock& other) const;

    // ── Accessors ─────────────────────────────────────────────
    uint32_t  operator[](size_t idx) const;
    size_t    size() const;

    /// Human-readable "[1,0,3,2]" form — also used for block hashing input.
    std::string to_string() const;

    /// Lexicographic ordering for deterministic containers / tie-breaking.
    /// *** NOT causal order — never confuse with precedes(). ***
    bool operator<(const VectorClock& other) const;
    bool operator==(const VectorClock& other) const;
    bool operator!=(const VectorClock& other) const;

private:
    std::vector<uint32_t> clock_;
};

} // namespace coldchain
