# ADR 0015: a versioned API snapshot at M3, and the freeze when the engine is complete

**Status:** Accepted. **Date:** 2026-10-09. **Amends:** A6
([0004](0004-api-abi-versioning.md), [0013](0013-media-grade-roadmap.md)).

## Context

A6 froze the public API at M3. With the game runtime in scope (ADR 0014), the API will
keep changing well after M3. The author, 2026-10-09: "mature them as the engine
matures. So I would probably just wait, you know, maybe freeze a snapshot and then
build out a full build out when the engine and everything is mature and complete."

## Decision

1. **At M3, take a snapshot, not a freeze.** The renderer and scene APIs are tagged as
   a numbered snapshot. Each snapshot is documented, has an API dump checked in CI, and
   ships a compatibility test suite that runs against it.
2. **The APIs keep maturing after M3.** A change that breaks a snapshot is allowed. It
   moves to the next snapshot number, with a migration note and the compatibility suite
   updated in the same change.
3. **The full freeze waits for a mature, complete engine.** It is a separate decision,
   taken by the author when the milestone plan is done. `world` and `scripting` stay
   0.x until then.

## Consequences

- M3 criterion 5 changes from a promise of source compatibility to a published snapshot
  with its dump and compatibility suite.
- Users of a snapshot see each break as a numbered step, never as silent drift.

## What would reverse it

An outside user who needs long-term stability before the engine is complete would argue
for freezing one snapshot early.
