# Live Evidence — F_0.8.0 D7 Phase 1, run 2 (segment cap + ray height fixed)

**Verdict: `PHASE_1_EVIDENCE_COLLECTED` (run 2).** Not a TRIP-verify receipt.
Both limitations of run 1 are closed, and closing them **raised the measured
block rate from 8.5% to 25.2% of conclusive paths** — run 1 was a substantial
undercount, not merely an imprecise estimate.

- **Run**: 2026-08-20, `feat/structure-traversal-foundation`
- **Change under test**: one world query per path (was per segment),
  `maxProbedSegments` 16 → 128, `broadPhasePadMeters`/`maxCandidates` moved to
  Lua, measured ray height at the `moveToWithOrigin` funnel, nearest-hit
  selection, original path-segment indices
- **Codex**: APPROVED, no new findings, all 9 checklist sections pass
- **Config**: temporary deployment-local override; committed defaults restored
  to fully off afterwards (md5 `bf7b26d9a27a85ce7f381c828bc83949`)

## Run 1 vs run 2

| Metric | Run 1 | Run 2 | |
| --- | --- | --- | --- |
| n | 276 | 277 | |
| `clear` | 54.3% | 31.0% | |
| `would_block` | 5.1% | **10.5%** | ▲ |
| `truncated` | 32.2% | **6.9%** | ▼ good |
| `skipped` | 8.3% | **51.6%** | ▲ see below |
| conclusive | 59.4% | 41.5% | ▼ |
| **block rate of conclusive** | 8.5% | **25.2%** | ▲ |
| segments p50 / max | 7 / 16 | 3 / 128 | |
| elapsedUs p50 / p95 / max | 4774 / 19059 / 29978 | 6884 / 49176 / 97608 | ▲ cost |
| distinct rayHeight values | 1 (`1.0` default) | 8 (`0.47`–`0.93`) | ▲ good |

## What the cap was hiding

`hitSegment` distribution: `0 ×21, 15, 17, 20, 22 ×4, 23`.

**7 of 29 blocks occurred beyond segment 16** — structurally invisible under the
old cap. One in four measured obstructions could not have been seen by run 1.

New obstructions that only appear in run 2:

| Hits | Template |
| --- | --- |
| 8 | `naboo/bridge_stairs_s01.iff` |
| 7 | `naboo/gungan_wall_ruined_lg_s02.iff` |
| 5 | `tatooine/starport_tatooine.iff` |
| 4 | `tatooine/debris_tatt_drum_dented_1.iff` |
| 2 | `naboo/starport_naboo.iff` |
| 1 each | `corellia/filler_building_corellia_style_01.iff`, `tatooine/capitol_tatooine.iff`, `general/debris_deathstar_storage.iff` |

11 of 39 distinct bots blocked (run 1: 13 of 39).

## Two things run 2 makes worse, honestly reported

### 1. `skipped` rose 8.3% → 51.6% — a definition change, not a regression

Run 1 silently `continue`d past any segment with a cell endpoint without
recording that it had done so. Run 2 counts those as unexamined, which is the
correct conservative reading — but because **one** skipped segment marks the
whole path `Skipped`, half of all paths now land there, and conclusive coverage
fell from 59.4% to 41.5%.

The information is not lost, it is merely not reported: on most of those paths
every *outdoor* segment was probed and found clear. **Recommended follow-up
(not done — outside the two fixes that were asked for):** log
`pathSegments` / `probedSegments` / `skippedSegments` per line so the analysis
can separate "wholly unexamined" from "outdoor portion examined, cell portion
not", *without* relaxing the conservative outcome rule that four review rounds
established.

### 2. Cost roughly doubled at p50 and 2.6× at p95

`p50 4774 → 6884 µs`, `p95 19059 → 49176 µs`, `max 29978 → 97608 µs`.

Expected: paths that used to stop at 16 segments now probe up to 128, and a
blocking segment no longer early-exits (it scans its full candidate list to find
the *nearest* obstruction). This runs on the pathfinding worker and is committed
only after delivery, so it is off the movement path — but a ~50 ms p95 per path
request is worth watching, and `maxProbedSegments` is now a Lua tunable
precisely so it can be traded against coverage without a rebuild.

## Unchanged safety posture

`enforce=false`, `exitSetEnabled=false`, `blocked=0` — nothing was ever refused.
Miner steady state healthy: `7 stationed / 0 moving / 1 sampling /
0 pathFailures / 0 activationFailures`.

## Still outstanding

1. **Part 2 (POB exit set + per-candidate anti-stuck budget) remains
   unexercised** — it needs the 25-scenario matrix, not an economy observe run.
2. Enforcement is still undesigned, and should stay that way until the
   `skipped` reporting above is split out; a 41.5%-conclusive sample is a weaker
   basis than run 1's headline suggested.
