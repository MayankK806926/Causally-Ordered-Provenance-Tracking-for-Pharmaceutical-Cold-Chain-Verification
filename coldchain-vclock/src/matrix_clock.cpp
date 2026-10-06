/// @file matrix_clock.cpp
/// @brief MatrixClock implementation — merge rules from report §3.3.

#include "coldchain/matrix_clock.hpp"

#include <algorithm>
#include <cassert>
#include <sstream>

namespace coldchain {

MatrixClock::MatrixClock(size_t n)
    : n_(n), matrix_(n, std::vector<uint32_t>(n, 0)) {}

void MatrixClock::set_own_row(const VectorClock& vc, size_t self_idx) {
    assert(self_idx < n_);
    assert(vc.size() == n_);
    for (size_t k = 0; k < n_; ++k)
        matrix_[self_idx][k] = vc[k];
}

void MatrixClock::merge_matrix(size_t sender_idx, const MatrixClock& incoming) {
    assert(sender_idx < n_);
    assert(incoming.n_ == n_);

    // Rule 1: incorporate sender's own row — what sender directly knows.
    for (size_t k = 0; k < n_; ++k)
        matrix_[sender_idx][k] = std::max(matrix_[sender_idx][k],
                                          incoming.matrix_[sender_idx][k]);

    // Rule 2: element-wise max over the whole matrix — transitive propagation.
    for (size_t p = 0; p < n_; ++p)
        for (size_t q = 0; q < n_; ++q)
            matrix_[p][q] = std::max(matrix_[p][q], incoming.matrix_[p][q]);
}

uint32_t MatrixClock::at(size_t j, size_t k) const {
    assert(j < n_ && k < n_);
    return matrix_[j][k];
}

void MatrixClock::set(size_t j, size_t k, uint32_t val) {
    assert(j < n_ && k < n_);
    matrix_[j][k] = val;
}

VectorClock MatrixClock::column_min() const {
    VectorClock result(n_);
    for (size_t k = 0; k < n_; ++k) {
        uint32_t mn = UINT32_MAX;
        for (size_t j = 0; j < n_; ++j)
            mn = std::min(mn, matrix_[j][k]);
        // VectorClock doesn't expose direct slot writes, so we tick to the
        // required value.  Since we start from zero, tick mn times.
        for (uint32_t t = 0; t < mn; ++t)
            result.tick(k);
    }
    return result;
}

VectorClock MatrixClock::row(size_t j) const {
    assert(j < n_);
    VectorClock vc(n_);
    for (size_t k = 0; k < n_; ++k)
        for (uint32_t t = 0; t < matrix_[j][k]; ++t)
            vc.tick(k);
    return vc;
}

size_t MatrixClock::n() const { return n_; }

std::string MatrixClock::to_string() const {
    std::ostringstream os;
    os << "MatrixClock(" << n_ << "):\n";
    for (size_t j = 0; j < n_; ++j) {
        os << "  [";
        for (size_t k = 0; k < n_; ++k) {
            if (k) os << ", ";
            os << matrix_[j][k];
        }
        os << "]\n";
    }
    return os.str();
}

bool MatrixClock::operator==(const MatrixClock& other) const {
    return n_ == other.n_ && matrix_ == other.matrix_;
}

} // namespace coldchain
