# Live Verification — F_0.8.0 Structure Traversal Foundation

**Verdict: `LIVE_VERIFICATION_FAIL`**

- **Run ID**: `20260814-091057-structure-traversal-foundation`
- **Date**: 2026-08-14
- **Branch**: `feat/structure-traversal-foundation` (plan commit `df38e771c0`)
- **Plan**: `docs/1-plans/F_0.8.0_structure-traversal-foundation.plan.md`
- **Skill**: `.claude/skills/TRIP-verify`

## Changed subsystems

| File | Role |
| --- | --- |
| `src/server/zone/managers/collision/PathFinderManager.cpp` | Gated egress repair ladder inside `findPathFromCellToWorld` |
| `src/server/zone/objects/creature/simplayer/SimPlayerController.{h,cpp}` | Traversal state machine, movement-origin discriminator, resume monitor, harness controller |
| `src/server/zone/objects/creature/simplayer/SimPlayerManager.{h,cpp}` | Config, 25-scenario runner, dashboard `structureTraversal` root |
| `src/server/zone/objects/creature/simplayer/StructureTraversalDiagLog.h` | Run-scoped diagnostic log |
| `src/server/zone/objects/creature/simplayer/SimHunterController.{h,cpp}`, `SimPvPController.{h,cpp}` | `isCombatDriverActive()` overrides |
| `bin/scripts/managers/sim_player_manager.lua` | `structureTraversal` + `structureTraversalTest` config (default-off) |
| `bin/scripts/ai/simTraversalTest.lua`, `bin/scripts/ai/templates.lua` | Idle no-op AI template for harness bots |

## Static gate

| Check | Result |
| --- | --- |
| `build` (`-Werror`, clang, Debug) | **PASS** — exit 0, warning-clean, binary current with source |
| `luac -p` — `sim_player_manager.lua`, `simTraversalTest.lua`, `templates.lua` | **PASS** |
| `core3 runUnitTests --gtest_filter="LuaMobileTest.*:CommandLuaTest.*"` | 4/5 pass — 1 **pre-existing** failure |

The `LuaMobileTest.LuaMobileTemplatesTest` failure is unrelated to this change:
it reports `light_jedi_padawan` objectName, weapon `hitChance > 0`, and unknown
command names. Grep of the test output for `simTraversalTest` /
`structureTraversal` returns 0 hits.

Note: the repository's `bin/testsuite3` binary is stale (2026-06-24) and does not
run GoogleTest; unit tests are invoked through `./core3 runUnitTests`.

## Lifecycle

`core3` had been cleanly stopped since 2026-08-06 (`SIGINT` → `gdb exit` at the
tail of `screenlog.0`; no crash, no backtrace). **Deviation from the skill's
"capture baseline dashboard sections before restart" step**: no running server
existed to sample, so baselines were taken from a freshly started gate-off
process on the same binary. That start also serves as the A18 regression
snapshot.

| Event | Time |
| --- | --- |
| Gate-off start (baseline) | ready in 12s, 0 players |
| Gate-on restart (matrix) | ready in 8s, 0 players |
| Gate-off restart (cleanup) | ready in 13s, 0 players |

Total matrix wall time: **57.4 min**. Process stayed alive throughout.

## Scenario configuration

`structureTraversal.enabled = true`, `structureTraversal.logging = true`,
`structureTraversalTest.enabled = true`, `dwellScaling = 1.0` (full fidelity —
combat-interrupt `durationMs` is not scaled by `dwellScaling`, verified at
`SimPlayerManager.cpp:31200`). Tagged non-persistent harness bots only; no
persistent roster touched.

## Results — 25/25 resolved, 9 PASS / 16 FAIL

| Scenario | Status | Duration | Steps |
| --- | --- | --- | --- |
| cantina_enter_exit | PASS | 119.2s | enter, dwell, exit |
| naboo_hospital_enter_exit | **FAIL** | 117.1s | enter PASS, dwell PASS, exit → `controller_path_failed` |
| mos_eisley_starport_front | **FAIL** | 84.1s | enter PASS, exit → `exit_not_outdoors` |
| mos_eisley_starport_deep_foyer4 | **FAIL** | 94.1s | enter PASS, exit → `exit_not_outdoors` |
| cantina_immediate_exit | PASS | 108.6s | enter, exit |
| cantina_long_dwell | PASS | 710.0s | enter, dwell (11.8 min), exit |
| theed_starport_hangar | **FAIL** | 0.0s | `target_cell_unresolved` — no steps ran |
| starport_upper_floor | **FAIL** | 104.6s | enter PASS, exit → `exit_not_outdoors` |
| hospital_to_cantina | PASS | 185.7s | enter, enter, exit |
| cantina_to_corellia_hospital | **FAIL** | 0.0s | `target_cell_unresolved` — no steps ran |
| cell_to_enclosed_hollow | **FAIL** | 178.1s | enter, moveTo, enter PASS, exit → `exit_not_outdoors` |
| combat_approach_door | **FAIL** | 0.6s | `attacker_spawn_failed` |
| combat_interior_route | **FAIL** | 4.6s | `attacker_spawn_failed` |
| combat_egress | **FAIL** | 29.1s | enter PASS, `attacker_spawn_failed` |
| combat_drag_different_cell | **FAIL** | 29.1s | enter PASS, `attacker_spawn_failed` |
| combat_ends_outdoors | **FAIL** | 29.1s | enter PASS, `attacker_spawn_failed` |
| combat_reentry_cross_building | **FAIL** | 58.3s | enter PASS, `attacker_spawn_failed` |
| attacker_dies_instantly | **FAIL** | 0.6s | `attacker_spawn_failed` |
| unreachable_target_bounded_failure | PASS | 1.5s | enter, moveTo |
| external_preemption | PASS | 33.6s | enter, moveTo |
| prepare_for_relocation | PASS | 2.5s | enter, moveTo |
| death_or_incapacity_recovery | PASS | 2.1s | enter, moveTo |
| ten_sequential_cycles | PASS | 1370.5s | 10× (enter, exit), all PASS |
| two_bots_opposite_directions | **FAIL** | 85.1s | enter, enter PASS, exit → `exit_not_outdoors` |
| bot_a_dwell_bot_b_traverse | **FAIL** | 96.6s | enter, enter, dwell PASS, exit → `exit_not_outdoors` |

### Failure taxonomy

| Cause | Count | Nature |
| --- | --- | --- |
| `attacker_spawn_failed` | 7 | Harness config defect |
| `exit_not_outdoors` | 6 | **Foundation defect — egress** |
| `target_cell_unresolved` | 2 | **Harness code defect** (re-diagnosed — see D4) |
| `controller_path_failed` | 1 | **Foundation defect — egress** |

**Every `enter` step that executed passed, in all 25 scenarios.** Ingress is
reliable. All foundation failures are on egress.

## Assertion table

| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| A1 | All 25 scenarios resolve | 25 resolved | 25 resolved, cursor 25 | PASS |
| A2 | All 25 PASS | 0 FAIL | 16 FAIL | **FAIL** |
| A3 | No scenario exceeds budget | no budget timeouts | none observed | PASS |
| A4 | Teleport anomalies | 0 | 0 | PASS |
| A5 | Z-sanity violations | 0 | 0 | PASS |
| A6 | Resume failures | 0 | 0 | PASS |
| A7 | Egress path failures | 0 | 1 | **FAIL** |
| A8 | Phase ordering legal | all legal edges | all observed transitions legal, every one carries a reason | PASS |
| A9 | Generation discipline | strictly increasing | gen 1→7+ strictly increasing, no stale-generation steps | PASS |
| A10 | Combat interrupts pause | CombatPaused entered + resumed | **not exercised** — attacker never spawned | **UNTESTED** |
| A11 | Combat is real | successful engage | **not exercised** | **UNTESTED** |
| A12 | Bounded failure bounded | resolves within budget | resolved in 1.5s via injected path failure | PASS |
| A13 | No `exitpath is nullptr` | 0 | 0 | PASS (see caveat) |
| A14 | Process survives | alive, no crash | alive 57.4 min; only GDB `Catchpoint 2/3` registrations, no backtrace | PASS |
| A15 | No test-entity leak | bots 0, no attackers | `bots=[]`, 0 `hermit_spider` references | PASS |
| A16 | Dashboard contract | 200, gated, roots intact | 200 with token, **403 without**, 38/38 roots preserved, no root lost | PASS |
| A17 | Simulation-only | no real mutation | no resource/credit/market/persistence mutation observed | PASS |
| A18 | Gate-off regression | clean, counters 0 | `enabled=false`, `testEnabled=false`, bots/scenarios empty, all 5 counters 0 | PASS |

**A13 caveat**: the literal string never appears because the new repair ladder
intercepts before the legacy error line. The underlying inability to path out of
the Theed medical center is still present and is captured by A7 instead.

## Defects to fix

### D1 — Egress accepts a truncated path and strands the bot inside the enclosed hollow (6 scenarios)

Trace (`mos_eisley_starport_front`, building `1106368`):

```
ST_PHASE  gen=6 Idle→Egress building=1106368 cell=0 reason=exit_requested
ST_PATH   request  world=(3527,-4803,5)
ST_PATH   accepted nodes=42 world=(3616.28,-4845.67,5.10)   <- not the request
ST_PATH   request  world=(3527,-4803,5)
ST_PATH   accepted nodes=13 world=(3573.4,-4813.2,5.09)     <- still not the request
SCENARIO_STEP exit status=FAIL durationMs=55592 reason=exit_not_outdoors
```

`findPath` returns a path whose final node is short of the requested outdoor
destination. The controller treats reaching that node as arrival, so
`completeStructureTraversalIfArrived()` never fires `exit_complete_outdoors`
(correctly — `isWithinOwningBuildingHollow()` is still true), and after
`egressAttemptCap = 2` the leg is abandoned with the bot inside the walled pad.

`isWithinOwningBuildingHollow()` is working as designed; the gap is that egress
has no escalation once the navmesh cannot produce a path clear of the hollow.
Suggested direction: when the egress leg arrives but the hollow test still holds,
route to the hollow's known exit aperture — the F_0.4.11
`resolveStarportInteriorWaypoint` machinery already resolves that point — before
handing off to the overland leg. This is the same class as the original P.4
finding: SWG only navmeshes cities/POIs.

### D2 — Repair ladder exhausts on the Theed medical center (1 scenario)

```
ST_PATH repair=legacy_failure building=1697358 cell=1697364
        from=world=(-5030.74,4179.85,6.26) local=(-21.16,4.84,0.26)
        to=world=(-5034.32,4176.54,13.25) cell=0 zone=naboo
ST_PATH repair=all_failed building=1697358 cell=1697364
SCENARIO_RESULT naboo_hospital_enter_exit status=FAIL reason=controller_path_failed
```

All three retries (interior re-hint, source-cell-linked exterior node, reversed
entry) returned null from `portalLayout->getPath()`, suggesting the bot's cell
subgraph has no portal-graph connection to the exterior floor mesh's global
nodes. Note the chosen exterior node sits at **z = 13.25** while the bot stands
at z = 6.26 — only ~4.8 m away horizontally but 7 m above, i.e. plausibly an
upper-floor/roof node.

**Observability debt**: the ladder logs a per-retry line only on *success* and a
single aggregate `repair=all_failed`. Which retry failed, and whether it failed
because the node was null or because `getPath()` returned null, is not
distinguishable. Add per-retry failure diagnostics first.

### D3 — Harness: invalid attacker template (7 scenarios, blocks A10/A11)

`structureTraversalTest.attackerTemplate = "adult_hermit_spider"` does not exist.
`spawnCreature()` returns null and the scenario fails at
`SimPlayerManager.cpp:30595`. Valid templates include `hermit_spider`,
`hermit_spider_guard`, `hermit_spider_mountain`, `hermit_spider_wasteland`.
Until this is corrected the entire combat-interrupt path is unverified.

### D4 — `resolveStructureTraversalTestTarget()` cannot resolve anything but an explicit cell OID (2 scenarios)

**Re-diagnosed 2026-08-14 after the run — this is a code defect, not the config
typo it was first classified as.** Two independent bugs in
`SimPlayerManager::resolveStructureTraversalTestTarget()`:

1. **Unreachable branch.** The building-pinned path was guarded by
   `else if (step.buildingOid != 0 && !step.cellName.isEmpty())`, whose body then
   asked `if (!step.cellName.isEmpty()) ... else ... getCell(1)`. The outer
   condition already requires a non-empty `cellName`, so the `getCell(1)`
   fallback was dead code and `buildingOid`-only entries never resolved.
2. **Wrong round-trip guard.** The world-point fallback required
   `world.distanceTo(requested) <= 3.f`, where `requested` is the *anchor* passed
   in. But `resolveStarportInteriorWaypoint()` takes a starport-side anchor (a
   `PlanetTravelPoint`) and deliberately returns an interior path-graph node
   metres away from it. The guard therefore rejected every successful
   resolution, making the whole world-point branch of the DSL dead.

`theed_starport_hangar` hit bug 2; `cantina_to_corellia_hospital` hit bug 1 and
then bug 2. Both failed at 0.0s with `target_cell_unresolved` and no steps run.

Two latent hazards were found and closed in the same function: an explicit
`cellOid` or `buildingOid` that failed to resolve fell *through* to the world
resolver, handing it CELL-LOCAL coordinates as a world anchor — the F_0.4.7
world-coord-as-cell-local confusion in reverse. Both now fail fast with a
diagnostic. `BuildingObject::getCell(idx)` is an unchecked `cells.get(idx)`, so
the cell count is now verified before requesting cell 1.

## What is proven to work

- Ingress into every building type exercised — 100% of executed `enter` steps passed.
- `ten_sequential_cycles`: 10 consecutive enter/exit round trips, 22.8 min, all PASS — no drift, no leak, no anomaly.
- `cantina_long_dwell`: 11.8 min inside a cell, then clean egress.
- Bounded failure (`unreachable_target_bounded_failure`, 1.5s), external preemption, relocation prep, and death/incapacity recovery all PASS.
- Generation discipline and phase-transition legality held across the whole run.
- Zero teleport anomalies, zero z-sanity violations, zero resume failures.
- Cross-planet scenario hops (tatooine → naboo → corellia) worked.

## Raw evidence

| Path | Contents |
| --- | --- |
| `MMOCoreORB/bin/log/trip-verify-20260814-091057-structure-traversal-foundation-dashboard.jsonl` | 8.1 MB, `result.structureTraversal` sampled at 5s |
| `MMOCoreORB/bin/log/trip-verify-20260814-091057-structure-traversal-foundation-structuretraversal.log` | 58,549 B, `ST_PHASE` / `ST_PATH` / `SCENARIO_STEP` / `SCENARIO_RESULT` |

## Cleanup confirmation

- `sim_player_manager.lua` restored byte-for-byte (md5 `b1d3021ce58544f0a81e21fc9c53c3fb`); all three gates back to `false`; `luac -p` clean.
- Server restarted on the restored config; gate-off state re-confirmed (A18).
- No harness bots or attackers remain (`bots=[]`, 0 `hermit_spider` references).
- No database, persistent roster, or unrelated log was cleared.
- No receipt recorded — receipts are bound to PASS only.

## Next step

Return to `TRIP-2-implement`. Recommended order: **D3 and D4** (one-line config
corrections that unblock 9 scenarios and the A10/A11 combat assertions), then
**D2's per-retry diagnostics**, then the **D1 egress escalation**, which is the
substantive foundation change. Re-run this matrix afterwards.
