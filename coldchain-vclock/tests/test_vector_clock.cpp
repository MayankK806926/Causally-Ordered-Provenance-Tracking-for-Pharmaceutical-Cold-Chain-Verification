/// @file test_vector_clock.cpp
/// @brief Week 2 unit tests for VectorClock (Fidge-Mattern logical clocks).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/vector_clock.hpp"

#include <map>
#include <random>
#include <set>

using namespace coldchain;

TEST_CASE("VectorClock initialization") {
    VectorClock vc4;
    CHECK(vc4.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(vc4[i] == 0);
    }

    VectorClock vc6(6);
    CHECK(vc6.size() == 6);
    for (size_t i = 0; i < 6; ++i) {
        CHECK(vc6[i] == 0);
    }
}

TEST_CASE("VectorClock tick increments only targeted slot") {
    VectorClock vc(4);
    vc.tick(1);
    CHECK(vc[0] == 0);
    CHECK(vc[1] == 1);
    CHECK(vc[2] == 0);
    CHECK(vc[3] == 0);

    vc.tick(1);
    vc.tick(3);
    CHECK(vc[1] == 2);
    CHECK(vc[3] == 1);
    CHECK(vc[0] == 0);
    CHECK(vc[2] == 0);
}

TEST_CASE("VectorClock merge is element-wise max") {
    VectorClock a(4);
    a.tick(0); // [1,0,0,0]
    a.tick(0); // [2,0,0,0]
    a.tick(1); // [2,1,0,0]

    VectorClock b(4);
    b.tick(1); // [0,1,0,0]
    b.tick(1); // [0,2,0,0]
    b.tick(2); // [0,2,1,0]

    VectorClock c = a;
    c.merge(b);

    CHECK(c[0] == 2);
    CHECK(c[1] == 2);
    CHECK(c[2] == 1);
    CHECK(c[3] == 0);
}

TEST_CASE("VectorClock merge algebraic properties: idempotence, commutativity, associativity") {
    VectorClock a(4); a.tick(0); a.tick(1);
    VectorClock b(4); b.tick(1); b.tick(2); b.tick(2);
    VectorClock c(4); c.tick(0); c.tick(3);

    // Idempotence: a merge a == a
    VectorClock a_idem = a;
    a_idem.merge(a);
    CHECK(a_idem == a);

    // Commutativity: merge(a, b) == merge(b, a)
    VectorClock ab = a; ab.merge(b);
    VectorClock ba = b; ba.merge(a);
    CHECK(ab == ba);

    // Associativity: merge(merge(a, b), c) == merge(a, merge(b, c))
    VectorClock lhs = a; lhs.merge(b); lhs.merge(c);
    VectorClock bc = b; bc.merge(c);
    VectorClock rhs = a; rhs.merge(bc);
    CHECK(lhs == rhs);
}

TEST_CASE("VectorClock precedes: strict happens-before relation") {
    VectorClock a(4);
    VectorClock b(4);

    // a and b both [0,0,0,0] -> neither precedes the other
    CHECK_FALSE(a.precedes(b));
    CHECK_FALSE(b.precedes(a));

    a.tick(0); // a = [1,0,0,0]
    CHECK(b.precedes(a));
    CHECK_FALSE(a.precedes(b));

    // Irreflexivity: !x.precedes(x)
    CHECK_FALSE(a.precedes(a));
}

TEST_CASE("VectorClock Fidge-Mattern classic 3-process example") {
    // 3 processes: P0, P1, P2
    VectorClock e0_1(3);
    e0_1.tick(0); // [1, 0, 0]

    // P0 sends message to P1
    VectorClock e1_1(3);
    e1_1.merge(e0_1);
    e1_1.tick(1); // [1, 1, 0]

    // Local event on P1
    VectorClock e1_2 = e1_1;
    e1_2.tick(1); // [1, 2, 0]

    // P1 sends message to P2
    VectorClock e2_1(3);
    e2_1.merge(e1_2);
    e2_1.tick(2); // [1, 2, 1]

    // Causal chain: e0_1 -> e1_1 -> e1_2 -> e2_1
    CHECK(e0_1.precedes(e1_1));
    CHECK(e1_1.precedes(e1_2));
    CHECK(e1_2.precedes(e2_1));

    // Transitivity checks
    CHECK(e0_1.precedes(e1_2));
    CHECK(e0_1.precedes(e2_1));
    CHECK(e1_1.precedes(e2_1));

    // Reverse must NOT hold
    CHECK_FALSE(e2_1.precedes(e0_1));
    CHECK_FALSE(e2_1.precedes(e1_2));
    CHECK_FALSE(e1_2.precedes(e1_1));
}

TEST_CASE("VectorClock is_concurrent correctly flags non-comparable clocks") {
    VectorClock a(4);
    a.tick(0); // [1, 0, 0, 0]

    VectorClock b(4);
    b.tick(1); // [0, 1, 0, 0]

    CHECK(a.is_concurrent(b));
    CHECK(b.is_concurrent(a));
    CHECK_FALSE(a.precedes(b));
    CHECK_FALSE(b.precedes(a));

    // Clocks with overlapping higher slots
    VectorClock c(4); // [2, 1, 0, 0]
    c.tick(0); c.tick(0); c.tick(1);

    VectorClock d(4); // [1, 2, 0, 0]
    d.tick(0); d.tick(1); d.tick(1);

    CHECK(c.is_concurrent(d));
    CHECK(d.is_concurrent(c));

    // Preceding clocks are NOT concurrent
    VectorClock base(4);
    base.tick(0);
    VectorClock advanced = base;
    advanced.tick(1);
    CHECK(base.precedes(advanced));
    CHECK_FALSE(base.is_concurrent(advanced));
    CHECK_FALSE(advanced.is_concurrent(base));

    // Equal clocks are NOT concurrent
    VectorClock identical = base;
    CHECK_FALSE(base.is_concurrent(identical));
}

TEST_CASE("VectorClock property test: 4-way mutual exclusion") {
    // For any two vector clocks A and B, exactly one of the following is true:
    // 1) A == B
    // 2) A.precedes(B)
    // 3) B.precedes(A)
    // 4) A.is_concurrent(B)
    std::mt19937 rng(1337);
    std::uniform_int_distribution<size_t> node_dist(0, 3);
    std::uniform_int_distribution<int> ticks_dist(1, 15);

    for (int trial = 0; trial < 100; ++trial) {
        VectorClock a(4);
        VectorClock b(4);

        int a_ticks = ticks_dist(rng);
        for (int i = 0; i < a_ticks; ++i) a.tick(node_dist(rng));

        int b_ticks = ticks_dist(rng);
        for (int i = 0; i < b_ticks; ++i) b.tick(node_dist(rng));

        bool eq = a.equals(b);
        bool a_prec_b = a.precedes(b);
        bool b_prec_a = b.precedes(a);
        bool conc = a.is_concurrent(b);

        int count = (eq ? 1 : 0) + (a_prec_b ? 1 : 0) + (b_prec_a ? 1 : 0) + (conc ? 1 : 0);
        CHECK(count == 1);

        // Antisymmetry check
        if (a_prec_b) {
            CHECK_FALSE(b_prec_a);
        }
        if (b_prec_a) {
            CHECK_FALSE(a_prec_b);
        }
    }
}

TEST_CASE("VectorClock to_string format") {
    VectorClock vc(4);
    CHECK(vc.to_string() == "[0,0,0,0]");

    vc.tick(0);
    vc.tick(1); vc.tick(1);
    vc.tick(3); vc.tick(3); vc.tick(3);
    CHECK(vc.to_string() == "[1,2,0,3]");
}

TEST_CASE("VectorClock container ordering operator< provides strict weak ordering") {
    VectorClock a(4); a.tick(0); // [1, 0, 0, 0]
    VectorClock b(4); b.tick(1); // [0, 1, 0, 0]

    // Although concurrent, one must be < the other for std::set/std::map to work
    CHECK(a.is_concurrent(b));
    CHECK((b < a)); // Lexicographically [0,1,0,0] < [1,0,0,0]
    CHECK_FALSE(a < b);

    std::set<VectorClock> clock_set;
    clock_set.insert(a);
    clock_set.insert(b);
    CHECK(clock_set.size() == 2);

    std::map<VectorClock, std::string> clock_map;
    clock_map[a] = "clock_a";
    clock_map[b] = "clock_b";
    CHECK(clock_map[a] == "clock_a");
    CHECK(clock_map[b] == "clock_b");
}
