/// @file test_ledger_integrity.cpp
/// @brief Week 1 tests — hash chain integrity, tamper detection, block sealing.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "coldchain/events.hpp"
#include "coldchain/hashing.hpp"
#include "coldchain/ledger_block.hpp"
#include "coldchain/vector_clock.hpp"

using namespace coldchain;

// ── Helper: make a simple SensorEvent ─────────────────────────

static SensorEvent make_sensor(uint32_t shipment, uint8_t entity,
                                float temp, uint64_t counter,
                                VectorClock vc) {
    SensorEvent se;
    se.shipment_id            = shipment;
    se.origin_entity_id       = entity;
    se.temperature_celsius    = temp;
    se.latitude               = 13.0f;
    se.longitude              = 80.0f;
    se.local_monotonic_counter = counter;
    se.vc                     = std::move(vc);
    se.signature              = sign_event(se.serialize(), "secret_node_" + std::to_string(entity));
    return se;
}

// ═════════════════════════════════════════════════════════════
//  SHA-256 basics
// ═════════════════════════════════════════════════════════════

TEST_CASE("sha256 produces correct length") {
    auto h = sha256_bytes("hello");
    CHECK(h.size() == 32);

    auto hex = sha256_hex("hello");
    CHECK(hex.size() == 64);
}

TEST_CASE("sha256 is deterministic") {
    CHECK(sha256_hex("test data") == sha256_hex("test data"));
}

TEST_CASE("sha256 differs for different inputs") {
    CHECK(sha256_hex("a") != sha256_hex("b"));
}

// ═════════════════════════════════════════════════════════════
//  Event signing / verification
// ═════════════════════════════════════════════════════════════

TEST_CASE("sign_event and verify_signature round-trip") {
    std::string payload = "SE|1|0|4.5|13.0|80.0|1|[1,0,0,0]";
    std::string secret  = "node_secret_0";

    Hash256 sig = sign_event(payload, secret);
    CHECK(verify_signature(payload, secret, sig));
}

TEST_CASE("verify_signature rejects wrong secret") {
    std::string payload = "SE|1|0|4.5|13.0|80.0|1|[1,0,0,0]";
    Hash256 sig = sign_event(payload, "correct_secret");
    CHECK_FALSE(verify_signature(payload, "wrong_secret", sig));
}

TEST_CASE("verify_signature rejects tampered payload") {
    std::string payload = "SE|1|0|4.5|13.0|80.0|1|[1,0,0,0]";
    std::string secret  = "my_secret";
    Hash256 sig = sign_event(payload, secret);

    std::string tampered = "SE|1|0|9.9|13.0|80.0|1|[1,0,0,0]";
    CHECK_FALSE(verify_signature(tampered, secret, sig));
}

// ═════════════════════════════════════════════════════════════
//  Event serialization determinism
// ═════════════════════════════════════════════════════════════

TEST_CASE("SensorEvent serialize is deterministic") {
    VectorClock vc(4);
    vc.tick(0);
    auto se = make_sensor(1, 0, 5.5f, 1, vc);
    CHECK(se.serialize() == se.serialize());
}

TEST_CASE("CustodyEvent serialize is deterministic") {
    CustodyEvent ce;
    ce.shipment_id          = 1;
    ce.from_entity_id       = 0;
    ce.to_entity_id         = 1;
    ce.custody_transfer_seq = 1;
    ce.vc                   = VectorClock(4);
    CHECK(ce.serialize() == ce.serialize());
}

// ═════════════════════════════════════════════════════════════
//  Block sealing & chain integrity
// ═════════════════════════════════════════════════════════════

TEST_CASE("seal_block produces valid hash") {
    VectorClock vc(4);
    vc.tick(0);
    std::vector<LedgerEvent> events;
    events.push_back(make_sensor(1, 0, 4.0f, 1, vc));

    auto blk = seal_block(0, events, genesis_hash(), 4);

    CHECK(blk.block_index == 0);
    CHECK(blk.prev_hash == genesis_hash());
    // Recompute hash independently and verify match.
    CHECK(compute_block_hash(blk) == blk.this_hash);
}

TEST_CASE("seal_block summary VC is element-wise max of events") {
    VectorClock vc1(4); vc1.tick(0);                // [1,0,0,0]
    VectorClock vc2(4); vc2.tick(1); vc2.tick(1);   // [0,2,0,0]

    std::vector<LedgerEvent> events;
    events.push_back(make_sensor(1, 0, 4.0f, 1, vc1));
    events.push_back(make_sensor(1, 1, 5.0f, 1, vc2));

    auto blk = seal_block(0, events, genesis_hash(), 4);
    CHECK(blk.block_vc[0] == 1);
    CHECK(blk.block_vc[1] == 2);
    CHECK(blk.block_vc[2] == 0);
    CHECK(blk.block_vc[3] == 0);
}

TEST_CASE("verify_chain accepts a valid chain") {
    VectorClock vc(4);
    vc.tick(0);

    std::vector<LedgerEvent> ev1;
    ev1.push_back(make_sensor(1, 0, 4.0f, 1, vc));
    auto blk1 = seal_block(0, ev1, genesis_hash(), 4);

    vc.tick(0);
    std::vector<LedgerEvent> ev2;
    ev2.push_back(make_sensor(1, 0, 5.0f, 2, vc));
    auto blk2 = seal_block(1, ev2, blk1.this_hash, 4);

    std::vector<LedgerBlock> chain = {blk1, blk2};
    CHECK(verify_chain(chain));
}

TEST_CASE("verify_chain detects a single tampered byte") {
    VectorClock vc(4);
    vc.tick(0);

    std::vector<LedgerEvent> ev1;
    ev1.push_back(make_sensor(1, 0, 4.0f, 1, vc));
    auto blk1 = seal_block(0, ev1, genesis_hash(), 4);

    vc.tick(0);
    std::vector<LedgerEvent> ev2;
    ev2.push_back(make_sensor(1, 0, 5.0f, 2, vc));
    auto blk2 = seal_block(1, ev2, blk1.this_hash, 4);

    std::vector<LedgerBlock> chain = {blk1, blk2};

    // Tamper with block 1's stored hash (simulates corruption).
    chain[0].this_hash[0] ^= 0xFF;
    CHECK_FALSE(verify_chain(chain));
}

TEST_CASE("verify_chain detects broken linkage") {
    VectorClock vc(4);
    vc.tick(0);

    std::vector<LedgerEvent> ev1;
    ev1.push_back(make_sensor(1, 0, 4.0f, 1, vc));
    auto blk1 = seal_block(0, ev1, genesis_hash(), 4);

    vc.tick(0);
    std::vector<LedgerEvent> ev2;
    ev2.push_back(make_sensor(1, 0, 5.0f, 2, vc));
    // Intentionally chain from genesis instead of blk1 → broken linkage.
    auto blk2 = seal_block(1, ev2, genesis_hash(), 4);

    std::vector<LedgerBlock> chain = {blk1, blk2};
    CHECK_FALSE(verify_chain(chain));
}

TEST_CASE("empty chain is valid") {
    std::vector<LedgerBlock> empty;
    CHECK(verify_chain(empty));
}

// ═════════════════════════════════════════════════════════════
//  event_vc / event_origin / event_counter helpers
// ═════════════════════════════════════════════════════════════

TEST_CASE("event_vc extracts correct VC from SensorEvent") {
    VectorClock vc(4);
    vc.tick(2);
    LedgerEvent ev = make_sensor(1, 2, 3.0f, 5, vc);
    CHECK(event_vc(ev)[2] == 1);
}

TEST_CASE("event_origin returns origin for SensorEvent") {
    VectorClock vc(4);
    LedgerEvent ev = make_sensor(1, 3, 3.0f, 1, vc);
    CHECK(event_origin(ev) == 3);
}

TEST_CASE("event_origin returns from_entity for CustodyEvent") {
    CustodyEvent ce;
    ce.shipment_id          = 1;
    ce.from_entity_id       = 2;
    ce.to_entity_id         = 3;
    ce.custody_transfer_seq = 1;
    ce.vc                   = VectorClock(4);
    LedgerEvent ev          = ce;
    CHECK(event_origin(ev) == 2);
}
