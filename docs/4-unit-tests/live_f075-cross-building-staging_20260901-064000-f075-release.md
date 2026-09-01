# Live Verification — F_0.7.5 cross-building provider staging

**Run ID**: `20260901-064000-f075-release`
**Date**: 01-09-2026, week 8
**Target**: F_0.7.5 cross-building provider staging (PvE hunter buff approach)
**Verdict**: `LIVE_VERIFICATION_PASS`

## Source / changed subsystems

`SimHunterController` (staging leg), `SimPlayerManager` (F_0.7.3 dual-anchor med-centre
resolver + the staging gate), `sim_player_manager.lua` (shipped gate profile).

## Static gate

- Build: warning-clean against `-Werror`, incremental, relinked.
- Lua: `luac -p` clean on `sim_player_manager.lua`.
- Unit tests: no GoogleTest suite exists for SimPlayer/traversal (see `COVERAGE-DEBT.md`);
  the live contract below is the test deliverable.

## Scenario configuration

Verified against the **exact shipped profile**, not a diagnostic one — confirmed at runtime
from the dashboard rather than by reading the file:

```
structureTraversal.enabled   = true    zeroClip.enabled            = false
  zeroClip.exitSetEnabled    = true    structureTraversal.logging  = false
  farSideEgress              = true    structureTraversalTest      = false
  hollowEscalationEnabled    = true    cellNavDiag.logging         = false
  hollowDoorEgress.*         = true
realBuffs.crossBuildingStaging = true  (compiled default false)
```

The six `theed_doc_*` isolation scenarios were removed; the matrix is back to 26.

## Assertions

| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| A1 | Clean startup, process alive, no crash/backtrace | none | 0 crash signatures; `core3` alive | **PASS** |
| A2 | Shipped gates loaded at runtime | as above | exactly as above | **PASS** |
| A3 | Non-vacuity: production agents drive traversal | > 0 | peaked at 9 concurrent | **PASS** |
| A4 | Theed-homed hunter reaches `real-doctor` **in Theed** | yes | `real-doctor`, `planet=naboo`, `pos=(-5023,4187)` — ~12 m from the Theed doctor at `(-5029.6,4177.0)` | **PASS** |
| A5 | `doctorInteractions` > 0 and increasing | > 0 | 0 → 4, across 4 distinct cities (theed, moenia, keren, mos_eisley) | **PASS** |
| A6 | `medCenterResolved` for theed + coronet | true | both true | **PASS** |
| A7 | Anomaly counters bounded, no runaway | bounded | `egressPathFailures` 0→12, `pathfinderFallbackActivations` 0→16, `resumeFailures` 0, `teleportsDetected` 0 | **PASS** |

Final roster: 4 of 6 bots on `real-doctor`; `syntheticFallbacks` **0** for the whole run.

## Regression measured

The Theed hunter produced **536** `ST_FAIL path_failed` events pre-fix with **zero** doctor
visits. Post-fix the fleet total is 12 `egressPathFailures`, and Theed completes the full
cantina → med-centre loop against real providers.

## Observations (not assertion failures)

- `zSanityViolations` 0 → 21, accumulating steadily. Diagnostics-only; it gates nothing.
  v0.8.1 recorded 53 of these and characterised them as pre-existing outdoor behaviour
  surfaced by traversal sampling, and F_0.8.1 skips the indoor case by construction.
  **LIMITATION**: with `structureTraversal.logging = false` this run CANNOT confirm the
  events are outdoor (`cell=0`) — that is per-event log data. The conclusion rests on the
  code path plus v0.8.1's characterisation, not on evidence from this run.
- `A3` is an instantaneous gauge, not a cumulative counter; the closing snapshot reads 0
  because agents had finished their traversals. The evidence is the observed sequence.
- The harness matrix did not run (harness gated off in the shipped profile), so this run
  carries no 26-scenario matrix result. The matrix evidence for the underlying traversal
  machinery is v0.8.1's receipt.

## Raw evidence

`/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260901-064000-f075-release.jsonl`
(baseline + final dashboard samples)

## Cleanup

No temporary entities spawned; the harness was never enabled. No database, roster or log
was cleared. The running configuration IS the shipped configuration — nothing to restore.
