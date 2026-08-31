# Live Evidence — F_0.8.0 D7 Phase 1 (observe-only)

**Verdict: `PHASE_1_EVIDENCE_COLLECTED`.** Not a TRIP-verify receipt — this is
the observational run the proposal requires *before* enforcement may be
considered. The 25-scenario matrix has not been run on this binary.

**Headline: bots are walking through starports and walls on ordinary economy
traffic.** 14 of 276 probed paths (5.1%; **8.5% of conclusive** ones) would have
been blocked, and **13 of 39 distinct bots (33%)** produced at least one.

- **Run**: 2026-08-19, `feat/structure-traversal-foundation`
- **Config**: `structureTraversal.enabled=true`, `zeroClip.enabled=true`,
  `zeroClip.logging=true`, **`enforce=false`**, **`exitSetEnabled=false`**,
  scenario harness **off**, hollow escalation **off**
- **Binary**: D7 Phase 1 + Part 2, Codex-APPROVED (4 rounds), `PROMOTION_READY`

## What is actually being hit

| Hits | Template |
| --- | --- |
| 6 | `object/static/structure/naboo/gungan_wall_ruined_lg_s02.iff` |
| 3 | `object/building/tatooine/starport_tatooine.iff` |
| 2 | `object/building/corellia/filler_building_corellia_style_01.iff` |
| 1 | `object/building/corellia/starport_corellia.iff` |
| 1 | `object/building/tatooine/guild_theater_tatooine_style_01.iff` |
| 1 | `object/building/corellia/filler_block_corellia_64x32_s01.iff` |

`hitAt` (normalised distance along the leg at which the mesh is entered):
`0.006, 0.006, 0.022, 0.027, 0.097, 0.157, 0.206, 0.59 ×6, 0.696`

Two of those enter geometry **0.6% into the leg** — the bot is starting its walk
essentially inside the object. Blocked paths were 6-node (9), 2-node (3) and
44-node (2), so this is not only the straight-line fallback: ordinary short
routes clip too.

## The finding that justifies the code review

**Every single probe — all 276, and all 14 blocks — came through the `generic`
leg. Zero came through `directOverland`.**

The original proposal named `SimPathFindTask::run()`'s explicit overland branch
as *the* root cause, and the first implementation instrumented only that branch.
Had it shipped that way, this run would have collected **nothing**. Miners are
non-hybrid and reach `PathFinderManager::findPathFromWorldToWorld`'s
`size() < 2` fallback through the generic branch instead. The code review caught
this; the live run proves it empirically.

## Evidence quality (the reason for the outcome enum)

| Outcome | Count | Share |
| --- | --- | --- |
| `clear` | 150 | 54.3% |
| `truncated` | 89 | 32.2% |
| `skipped` | 23 | 8.3% |
| `would_block` | 14 | 5.1% |
| `errors` | 0 | 0% |
| **conclusive** (`clear` + `would_block`) | **164** | **59.4%** |

The very first observe run, before the broad-phase cull was added, truncated
**every** segment (`candidates == segments × 64`) and produced **zero**
conclusive results. Under the original boolean design all of those would have
been logged `clear`, and this report would have read "no clipping detected".
That is exactly the failure mode the four review rounds were about.

**Fix applied mid-run:** the probe now runs stock `checkMovementCollision`'s
cheap bounding-sphere cull (`CollisionManager::getPointIntersection`) before
spending any narrow-phase budget. Candidates per short path fell from 256–1024
to **2–9**. The +192 m origin pad decides what is *fetched*; the cull decides
what is worth *intersecting*.

## Cost

`elapsedUs` per path request (worker thread, not per tick):
`min 0, p50 4774, p95 19059, max 29978`.

Bounded and off the delivery path, but a p95 of ~19 ms per path request is worth
watching if enforcement ever adds a re-path on top.

## Miner steady state — unchanged, as required

`stationed 6 / moving 2 / sampling 0 / pathFailures 0 / activationFailures 0 /
movementArrivalTimeoutCount 0`, against a pre-restart baseline of
`moving 2 / validated 6 / pathFailures 0`. `enforce=false` is behaviourally
inert exactly as designed, and `blocked` is `0` — nothing was ever refused.

## Known limitations of this run

1. **32% truncated.** 88 of 89 truncations hit the 16-segment probe cap on long
   routes (median 58 nodes). Those are navmesh routes, which are routed around
   geometry by construction, so the truncation is concentrated where clipping
   risk is lowest — but the block rate is therefore measured over the 59.4% that
   is conclusive, not the whole population.
2. **Ray height is 1.0 m (default) on every line**, because all traffic was
   `generic` and only `directOverland` legs take a locked measurement. Real
   torso height is `getHeight() - 0.3`, so the probe is testing slightly low.
3. **`exitSetEnabled=false`** — Part 2 (POB exit set, per-candidate budget) is
   built and reviewed but **not exercised**. It needs the 25-scenario matrix.

## Next

1. **Do not enable `enforce` yet.** A 32%-truncated, 1.0 m-ray sample is enough
   to prove clipping is real and widespread; it is not enough to predict what
   fraction of legs enforcement would refuse. Raising the segment cap and
   plumbing a measured ray height into generic legs would close that.
2. Run the 25-scenario matrix to exercise Part 2's exit set and the anti-stuck
   budget.
3. Only then design Phase 2 enforcement against a measured refusal rate.
