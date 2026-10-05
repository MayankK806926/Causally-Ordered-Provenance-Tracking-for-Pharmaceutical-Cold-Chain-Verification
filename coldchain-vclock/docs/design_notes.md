# Design Notes

## Week 1 Decisions

### VectorClock implemented early
The plan originally deferred VectorClock to Week 2, but `events.hpp` embeds
`VectorClock` in both event structs.  Full VC implementation done in Week 1
so the entire foundation compiles and tests pass end-to-end.

### Crypto simplification
Using MAC-style `SHA256(serialize || secret)` rather than asymmetric signatures.
Sufficient for proving which simulated node originated an event.
Stated as explicit scope limitation per the report.

### picosha2 over OpenSSL
Single-header, public-domain SHA-256.  Avoids pulling OpenSSL as a dependency
for a simulation project.
