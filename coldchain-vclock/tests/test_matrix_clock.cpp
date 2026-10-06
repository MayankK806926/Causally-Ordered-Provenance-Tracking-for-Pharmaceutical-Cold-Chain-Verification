/// @file test_matrix_clock.cpp
/// @brief Week 3 unit tests for MatrixClock.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/matrix_clock.hpp"
#include "coldchain/vector_clock.hpp"

using namespace coldchain;

TEST_CASE("MatrixClock initialization is all zeros") {
    MatrixClock mc(4);
    CHECK(mc.n() == 4);
    for (size_t j = 0; j < 4; ++j)
        for (size_t k = 0; k < 4; ++k)
            CHECK(mc.at(j, k) == 0);
}

TEST_CASE("MatrixClock set_own_row copies vector clock into correct row") {
    MatrixClock mc(4);
    VectorClock vc(4);
    vc.tick(0); vc.tick(0); // [2,0,0,0]
    vc.tick(1);             // [2,1,0,0]
    vc.tick(3);             // [2,1,0,1]

    mc.set_own_row(vc, 1);

    CHECK(mc.at(1, 0) == 2);
    CHECK(mc.at(1, 1) == 1);
    CHECK(mc.at(1, 2) == 0);
    CHECK(mc.at(1, 3) == 1);

    // Other rows unchanged
    for (size_t k = 0; k < 4; ++k) {
        CHECK(mc.at(0, k) == 0);
        CHECK(mc.at(2, k) == 0);
        CHECK(mc.at(3, k) == 0);
    }
}

TEST_CASE("MatrixClock merge_matrix never decreases any entry (monotonicity)") {
    MatrixClock mc_a(3);
    mc_a.set(0, 0, 5);
    mc_a.set(0, 1, 3);
    mc_a.set(1, 2, 7);

    MatrixClock mc_b(3);
    mc_b.set(0, 0, 3);  // lower — should not overwrite
    mc_b.set(0, 1, 6);  // higher — should overwrite
    mc_b.set(2, 2, 4);

    // Save pre-merge values
    uint32_t pre_00 = mc_a.at(0, 0);
    uint32_t pre_12 = mc_a.at(1, 2);

    mc_a.merge_matrix(1, mc_b);

    // Monotonicity: no entry decreased
    CHECK(mc_a.at(0, 0) >= pre_00);
    CHECK(mc_a.at(1, 2) >= pre_12);

    // Specific checks
    CHECK(mc_a.at(0, 0) == 5);  // kept original (was higher)
    CHECK(mc_a.at(0, 1) == 6);  // took incoming (was higher)
    CHECK(mc_a.at(1, 2) == 7);  // kept original
    CHECK(mc_a.at(2, 2) == 4);  // took incoming
}

TEST_CASE("MatrixClock column_min hand-computed example") {
    // 3-node matrix clock:
    //   Node 0's view: [5, 3, 2]
    //   Node 1's view: [4, 6, 1]
    //   Node 2's view: [3, 4, 7]
    //
    // column_min = [min(5,4,3), min(3,6,4), min(2,1,7)] = [3, 3, 1]

    MatrixClock mc(3);
    mc.set(0, 0, 5); mc.set(0, 1, 3); mc.set(0, 2, 2);
    mc.set(1, 0, 4); mc.set(1, 1, 6); mc.set(1, 2, 1);
    mc.set(2, 0, 3); mc.set(2, 1, 4); mc.set(2, 2, 7);

    VectorClock cmin = mc.column_min();
    CHECK(cmin.size() == 3);
    CHECK(cmin[0] == 3);
    CHECK(cmin[1] == 3);
    CHECK(cmin[2] == 1);
}

TEST_CASE("MatrixClock column_min all zeros when matrix is zero") {
    MatrixClock mc(4);
    VectorClock cmin = mc.column_min();
    for (size_t k = 0; k < 4; ++k)
        CHECK(cmin[k] == 0);
}

TEST_CASE("MatrixClock gossip convergence — all-to-all pairwise exchange") {
    // 3 nodes each with a known local VC.  After all-to-all gossip, every
    // node's matrix should have the same values (convergence).
    const size_t N = 3;

    // Build per-node VCs
    VectorClock vc0(N); vc0.tick(0); vc0.tick(0); vc0.tick(0); // [3,0,0]
    VectorClock vc1(N); vc1.tick(1); vc1.tick(1);               // [0,2,0]
    VectorClock vc2(N); vc2.tick(2);                             // [0,0,1]

    MatrixClock mc0(N), mc1(N), mc2(N);
    mc0.set_own_row(vc0, 0);
    mc1.set_own_row(vc1, 1);
    mc2.set_own_row(vc2, 2);

    // Round 1: every node sends to every other node
    MatrixClock mc0_copy = mc0, mc1_copy = mc1, mc2_copy = mc2;

    // Node 0 receives from 1 and 2
    mc0.merge_matrix(1, mc1_copy);
    mc0.merge_matrix(2, mc2_copy);

    // Node 1 receives from 0 and 2
    mc1.merge_matrix(0, mc0_copy);
    mc1.merge_matrix(2, mc2_copy);

    // Node 2 receives from 0 and 1
    mc2.merge_matrix(0, mc0_copy);
    mc2.merge_matrix(1, mc1_copy);

    // Round 2: exchange again with updated matrices
    mc0_copy = mc0; mc1_copy = mc1; mc2_copy = mc2;
    mc0.merge_matrix(1, mc1_copy); mc0.merge_matrix(2, mc2_copy);
    mc1.merge_matrix(0, mc0_copy); mc1.merge_matrix(2, mc2_copy);
    mc2.merge_matrix(0, mc0_copy); mc2.merge_matrix(1, mc1_copy);

    // After 2 rounds of all-to-all gossip, matrices should converge
    // All nodes should know: row0=[3,0,0], row1=[0,2,0], row2=[0,0,1]
    for (size_t node = 0; node < N; ++node) {
        const MatrixClock& mc = (node == 0) ? mc0 : (node == 1) ? mc1 : mc2;
        CHECK(mc.at(0, 0) == 3);
        CHECK(mc.at(0, 1) == 0);
        CHECK(mc.at(0, 2) == 0);
        CHECK(mc.at(1, 0) == 0);
        CHECK(mc.at(1, 1) == 2);
        CHECK(mc.at(1, 2) == 0);
        CHECK(mc.at(2, 0) == 0);
        CHECK(mc.at(2, 1) == 0);
        CHECK(mc.at(2, 2) == 1);
    }

    // column_min should now be [0, 0, 0] (each node's row has zeros in
    // non-own columns, which is correct — they haven't observed others' events)
    // Actually col_min = [min(3,0,0), min(0,2,0), min(0,0,1)] = [0,0,0]
    VectorClock cmin = mc0.column_min();
    CHECK(cmin[0] == 0);
    CHECK(cmin[1] == 0);
    CHECK(cmin[2] == 0);
}

TEST_CASE("MatrixClock gossip convergence — after VC merge (message passing)") {
    // Simulate actual message passing where VCs merge, then matrix clocks propagate.
    const size_t N = 3;

    // Node 0 ticks, sends message to Node 1
    VectorClock vc0(N);
    vc0.tick(0); // [1,0,0]

    // Node 1 receives message, merges VC, then ticks
    VectorClock vc1(N);
    vc1.merge(vc0);
    vc1.tick(1); // [1,1,0]

    // Node 1 sends to Node 2
    VectorClock vc2(N);
    vc2.merge(vc1);
    vc2.tick(2); // [1,1,1]

    // Now build matrix clocks reflecting these VC states
    MatrixClock mc0(N), mc1(N), mc2(N);
    mc0.set_own_row(vc0, 0);
    mc1.set_own_row(vc1, 1);
    mc2.set_own_row(vc2, 2);

    // All-to-all gossip rounds
    for (int round = 0; round < 3; ++round) {
        MatrixClock s0 = mc0, s1 = mc1, s2 = mc2;
        mc0.merge_matrix(1, s1); mc0.merge_matrix(2, s2);
        mc1.merge_matrix(0, s0); mc1.merge_matrix(2, s2);
        mc2.merge_matrix(0, s0); mc2.merge_matrix(1, s1);
    }

    // After linear propagation (0 -> 1 -> 2), only node 0's event is known
    // by ALL nodes (node 0 itself has not yet received events from node 1 or 2).
    VectorClock cmin = mc0.column_min();
    CHECK(cmin[0] == 1);
    CHECK(cmin[1] == 0);
    CHECK(cmin[2] == 0);

    // All three should agree on column_min
    CHECK(mc1.column_min()[0] == 1);
    CHECK(mc1.column_min()[1] == 0);
    CHECK(mc1.column_min()[2] == 0);
    CHECK(mc2.column_min()[0] == 1);
    CHECK(mc2.column_min()[1] == 0);
    CHECK(mc2.column_min()[2] == 0);

    // Complete the ring: node 2 communicates back to node 0 and 1
    vc0.merge(vc2);
    vc1.merge(vc2);
    mc0.set_own_row(vc0, 0);
    mc1.set_own_row(vc1, 1);

    for (int round = 0; round < 2; ++round) {
        MatrixClock s0 = mc0, s1 = mc1, s2 = mc2;
        mc0.merge_matrix(1, s1); mc0.merge_matrix(2, s2);
        mc1.merge_matrix(0, s0); mc1.merge_matrix(2, s2);
        mc2.merge_matrix(0, s0); mc2.merge_matrix(1, s1);
    }

    // Now all nodes have observed all events
    VectorClock cmin_full = mc0.column_min();
    CHECK(cmin_full[0] == 1);
    CHECK(cmin_full[1] == 1);
    CHECK(cmin_full[2] == 1);
    CHECK(mc1.column_min()[0] == 1);
    CHECK(mc1.column_min()[1] == 1);
    CHECK(mc1.column_min()[2] == 1);
    CHECK(mc2.column_min()[0] == 1);
    CHECK(mc2.column_min()[1] == 1);
    CHECK(mc2.column_min()[2] == 1);
}

TEST_CASE("MatrixClock row() returns correct VectorClock") {
    MatrixClock mc(3);
    mc.set(1, 0, 5);
    mc.set(1, 1, 3);
    mc.set(1, 2, 8);

    VectorClock row1 = mc.row(1);
    CHECK(row1[0] == 5);
    CHECK(row1[1] == 3);
    CHECK(row1[2] == 8);
}

TEST_CASE("MatrixClock to_string is non-empty and formatted") {
    MatrixClock mc(2);
    mc.set(0, 0, 1);
    mc.set(1, 1, 2);
    std::string s = mc.to_string();
    CHECK(!s.empty());
    CHECK(s.find("MatrixClock") != std::string::npos);
}

TEST_CASE("MatrixClock equality operator") {
    MatrixClock a(3), b(3);
    CHECK(a == b);

    a.set(0, 0, 1);
    CHECK_FALSE(a == b);

    b.set(0, 0, 1);
    CHECK(a == b);
}
