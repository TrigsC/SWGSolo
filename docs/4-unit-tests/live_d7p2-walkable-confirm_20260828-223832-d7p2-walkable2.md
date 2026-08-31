# Live Evidence — D7 Phase 2: walkable-geometry confirmation

**Clip rate of walked paths: 11.3% → 3.5%.** 22 PASS / 4 FAIL, zero
regressions, zero scenario differences, probe cost unchanged. The requirement is
still not 0, but the measurement is now trustworthy and the residual is real.

- Run: `20260828-223832-d7p2-walkable2`
- Baseline: `20260828-101454-d2b-harness-fix` (enforce off, raw probe)
- Config: run-9 gates + `zeroClip.enforce`, `zeroClip.walkableConfirm`,
  `rejectionCap = 2`, `walkableToleranceRatio = 1.25`

## The four attempts, measured the same way

"Clip rate" is obstructed paths as a share of paths the bots **actually walked**
— refusals are excluded because the bot never travelled them.

| run | probed | refused | walked | obstructed | **clip rate** |
|---|---|---|---|---|---|
| run10 — enforce off, raw probe | 1226 | 0 | 1226 | 139 | **11.3%** |
| run12 — enforce + identical repath | 1388 | 227 | 1161 | 113 | **9.7%** |
| run13 — + navmesh rung | 1617 | 298 | 1319 | 144 | **10.9%** |
| **run15 — + walkable confirmation** | **987** | **64** | **923** | **32** | **3.5%** |

Refusals fell 227 → 64 and total path requests 1388 → 987, because the probe
stopped refusing routes that were never obstructed.

## What the confirmation does

The appearance ray tests the straight **chord** between two path nodes. For a
staircase or bridge deck that chord dives through the solid mass *beneath the
walkable surface* — geometry the bot is meant to walk ON. Stock
`CollisionManager::checkMovementCollision` never sees this because its ray is a
per-tick micro-segment that hugs the surface; D7's chord is tens of metres and
does not.

The navmesh is the authority on what an agent can stand on, so a flagged chord
is confirmed against it: if recast can walk between the same two endpoints
without meaningfully detouring (≤ 1.25× the chord), the intersection is a false
positive. A real obstruction forces recast around it, which shows up as a path
materially longer than the chord.

**110 mesh hits were overruled as walkable — 53% of all raw hits
(110 of 206) were never clipping at all.**

## The historical 25.4% figure was inflated

The block rate that has driven this defect since 2026-08-20, reproduced across
four runs, was **roughly half false positive**. What disappeared from the
blocking-template list is exactly what should have:

| gone after confirmation | still blocking |
|---|---|
| `bridge_stairs_s01` (27 in baseline) | `starport_tatooine` 42 |
| `seawall_rocks_naboo_theed_style_1` | `debris_tatt_drum_dented_1` 24 |
| `gungan_wall_ruined_lg_s02` (17) | `starport_corellia` 12 |
| | `starport_naboo` 6 |
| | `cantina_tatooine`, filler buildings, planters |

Stairs, a seawall and a ruined (walkable) wall are out. Starport shells, solid
debris and building blocks remain — which is the requirement's actual target.

## Cost

`p50 7849 µs, p95 47748 µs, max 97573 µs` — statistically unchanged from the
baseline probe (p50 8332 / p95 47373 / max 91211). The recast confirmation runs
only on a segment the cheap ray already flagged, so it rides along inside the
existing budget.

Matrix wall time and every anomaly counter unchanged: teleports 0, zSanity 0,
resumeFailures 0.

## A defect found and fixed inside this work

The first attempt reclassified **nothing** (0 overrules in 63 blocks). Cause:
`PathFinderManager::getRecastPath`'s `float& len` is **not a path length** — it
accumulates `x² + z²` of each point's *absolute world coordinates*, about 1.2e7
per point out at x=3500. The ratio test compared that against metres and could
never be true. The length is now measured from the returned points.

It is consumed, just never as a distance: `findPathFromWorldToWorld` uses it in a
`finalLengthSq` comparison to pick between candidate routes
(`PathFinderManager.cpp:380`), and `CollisionManager::checkMovementCollision`
passes a value in and ignores the result.

## What is still not done

**3.5% is not 0.** The residual 32 obstructions are genuine clips through
starport shells and debris, walked because the rejection budget is exhausted
(33 cap exhaustions) and no rung produces a route around them. Reaching 0 needs
either a detour generator or navmesh coverage that includes these objects —
neither is in scope here.

Two known limits of the confirmation itself:

- A short wall on a long chord could force a detour under the 1.25 tolerance and
  be wrongly cleared. Not observed, but the tolerance is a tuning knob, not a
  proof.
- Segments with no navmesh at either end get no confirmation and fall back to
  the raw ray. That is open wilderness, where there is little to clip.

## Cleanup

`sim_player_manager.lua` restored to committed defaults with the two new keys at
`walkableConfirm = false`, `walkableToleranceRatio = 1.25`; `luac -p` clean. All
gates default-off. `core3` alive, no crash markers.

## Evidence

- `bin/log/trip-verify-20260828-223832-d7p2-walkable2.log` (987 ST_CLEARANCE lines)
- `bin/log/trip-verify-20260828-201648-d7p2-navmesh.log` (run 13)
- `bin/log/trip-verify-20260828-163536-d7p2-repath.log` (run 12)
