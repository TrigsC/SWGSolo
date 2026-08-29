# Live Verification Run 2 — F_0.8.0 Structure Traversal Foundation

**Verdict: `LIVE_VERIFICATION_FAIL`** (run halted early — see "Run terminated")

- **Run ID**: `20260814-132303-structure-traversal-foundation`
- **Date**: 2026-08-14
- **Purpose**: verify the D3 and D4 fixes; gather D2 topology diagnostics to
  inform the D1 design
- **Predecessor**: `live_structure-traversal-foundation_20260814-091057.md`

## What changed since run 1

| Fix | Change |
| --- | --- |
| D3 | `attackerTemplate` `adult_hermit_spider` → `hermit_spider` (registered) |
| D4 | `resolveStructureTraversalTestTarget()`: unreachable `buildingOid` branch made reachable; bogus distance-to-anchor guard replaced with a cell-local/world round-trip check; hard-fail + `ST_RESOLVE` diagnostics on unresolvable explicit `cellOid`/`buildingOid`; `getTotalCellNumber()` bounds check |
| D2 | Per-retry failure diagnostics in the `PathFinderManager` egress repair ladder (`repair=topology`, per-retry `result=failed reason=...`, enriched `all_failed`) |

## Results — 20 of 25 resolved before the run was halted

4 PASS / 16 FAIL. Anomaly counters at halt:
`egressPathFailures 2`, `pathfinderFallbackActivations 2`,
`resumeFailures 0`, `teleportsDetected 0`, `zSanityViolations 0`.

| Scenario | Run 1 | Run 2 | Note |
| --- | --- | --- | --- |
| cantina_enter_exit | PASS | PASS | |
| naboo_hospital_enter_exit | FAIL | FAIL `controller_path_failed` | D2 diagnostics captured |
| mos_eisley_starport_front | FAIL | FAIL `exit_not_outdoors` | D1 |
| mos_eisley_starport_deep_foyer4 | FAIL | FAIL `exit_not_outdoors` | D1 |
| cantina_immediate_exit | PASS | PASS | |
| cantina_long_dwell | PASS | PASS | |
| theed_starport_hangar | FAIL | FAIL `target_cell_unresolved` | **new precise reason** |
| starport_upper_floor | FAIL | FAIL `exit_not_outdoors` | D1 |
| hospital_to_cantina | PASS | PASS | |
| cantina_to_corellia_hospital | FAIL `target_cell_unresolved` | FAIL `controller_path_failed` | **D4 fix confirmed** |
| cell_to_enclosed_hollow | FAIL | FAIL `exit_not_outdoors` | D1 |
| combat_approach_door | FAIL `attacker_spawn_failed` | FAIL `enter_budget_exceeded` | **D3 fix confirmed; revealed D5** |
| combat_interior_route | FAIL `attacker_spawn_failed` | FAIL `enter_budget_exceeded` | D5 |
| combat_egress | FAIL `attacker_spawn_failed` | FAIL `enter_budget_exceeded` | D5 |
| combat_drag_different_cell | FAIL `attacker_spawn_failed` | FAIL `enter_budget_exceeded` | D5 |
| combat_ends_outdoors | FAIL `attacker_spawn_failed` | FAIL `enter_budget_exceeded` | D5 |
| combat_reentry_cross_building | FAIL `attacker_spawn_failed` | FAIL `enter_budget_exceeded` | D5 |
| attacker_dies_instantly | FAIL `attacker_spawn_failed` | FAIL `enter_budget_exceeded` | D5 |
| unreachable_target_bounded_failure | **PASS** | FAIL `enter_budget_exceeded` | **D5b contamination** |
| external_preemption | **PASS** | FAIL `preemption_did_not_clear_traversal` | **D5b contamination** |
| prepare_for_relocation | PASS | not reached | halted |
| death_or_incapacity_recovery | PASS | not reached | halted |
| ten_sequential_cycles | PASS | not reached | halted |
| two_bots_opposite_directions | FAIL | not reached | halted |
| bot_a_dwell_bot_b_traverse | FAIL | not reached | halted |

## Fixes confirmed

### D3 — CONFIRMED FIXED

The failure mode moved from `attacker_spawn_failed` to `enter_budget_exceeded`,
i.e. the attacker now spawns and real combat starts. This exercised the
combat-interrupt path for the first time and immediately exposed D5 (below).

### D4 — CONFIRMED FIXED (code), scenario data still wrong for Theed

`cantina_to_corellia_hospital` went from a 0.0s `target_cell_unresolved` with no
steps executed, to **step 0 PASS** (Coronet cantina resolved via `buildingOid`
alone) and **step 1 PASS** at 84.5s (Corellia hospital cell), failing only later
on the shared egress defect. The unreachable-branch and round-trip-guard fixes
both work.

`theed_starport_hangar` still fails, but now with a precise reason instead of
silence:

```
ST_RESOLVE result=failed stage=starport_waypoint status=1
           cellResolved=false anchor=(-4858.83,4164.07,5.94832) planet=naboo
```

`status=1` is `STARPORT_NO_INTERIOR`: no cell-bearing building contains that
anchor. The scenario used the Theed Spaceport `PlanetTravelPoint`
(`planet_manager.lua:287`, `landingRange = 6`) — an outdoor shuttle landing
point, not an interior. **This is a genuine scenario-data error**, and the
remaining half of D4. Fix: pin a real `buildingOid`. The resolver already logs
`STARPORT_WP_CAND` lines with candidate building OIDs and bounds to
`CellNavDiagLog`; enabling that gate for one run yields the Theed starport
building OID directly.

## D2 diagnostics — the hospital topology

```
repair=topology legacyExitNode=393218(global=-1 pos=(-27.6438,2.00564,0.26))
                legacyExteriorNode=14(global=1 pos=(-19.243,0.00299905,7.25))
                sourceGlobalNodes=5 building=1697358 cell=1697364
repair=interior_rehint result=failed reason=no_portal_path
                rehintExitNode=393219(global=-1 ...)
repair=linked_exterior result=failed reason=no_linked_exterior_node
                candidatesScanned=5 linkedExteriorNode=null
repair=reversed_entry  result=failed reason=no_portal_path_either_direction
                legacyPairAlsoTried=true
```

Facts established:

1. Both exit-node candidates have `global=-1` — they are ordinary interior path
   nodes, not portal nodes. `findNearestPathNode` selects by proximity and is
   indifferent to portal-graph membership.
2. The cell has 5 global nodes and **none** matches a global ID in the exterior
   floor mesh. `PortalLayout::connectFloorMeshGraphs()` builds cross-cell links
   only by matching `globalGraphNodeID`, so cell `1697364` has no direct portal
   link to the exterior — it is a deep interior room requiring a multi-hop
   cell→cell→exterior route.
3. The exterior node targeted (`id 14`, local z = 7.25) is well above the bot's
   z = 0.26.

**Template-level, not location-specific.** The Corellia med center produced an
identical signature on a different planet:

```
Corellia (building 1855529, cell 1855535):
  legacyExitNode=393218(global=-1 pos=(-27.6438,2.00564,0.26))
  legacyExteriorNode=16(global=1 pos=(-19.243,0.003,7.25))   sourceGlobalNodes=5
```

Same node IDs, same cell-local positions — the same building template. Every
medical center in the game is affected.

**Lead worth checking**: this may be the same root cause as the F_0.7.3/F_0.7.4
doctor-buffer finding that "cell→outdoor egress is broken (entry works, egress
doesn't)". Same building type, same direction, same asymmetry. Unverified.

**Candidate fix**: a fourth strategy that iterates
`sourcePathGraph->getGlobalNodes()` and tries `getPath(globalNode, exteriorNode)`
for each. Not proven — `PortalLayout::getPath()` runs A* on the start node's
graph and can in principle hop meshes via global-node children even from a
non-global start, so why it fails is not yet established.

## D5 — NEW DEFECT: traversal never resumes after combat

Revealed only because D3 was fixed; unreachable in run 1.

```
896291  ApproachDoor → CombatPaused  reason=arrival_combat
896291  ST_COMBAT_PAUSE gen=24
904256  SCENARIO_ATTACKER_DESPAWN    reason=scripted_duration_elapsed  (+8s)
1015841 FAIL enter_budget_exceeded                                    (+111s)
```

The pause half works correctly. The resume never fires. Two bots, two
`ST_COMBAT_PAUSE` events, **zero** resumes.

### D5a — resume never fires

`SimPlayerManager::despawnStructureTraversalTestAttacker()` calls
`clearCombatState(true)` on the **attacker** and destroys it, but nothing clears
the **bot's** side. `checkStructureTraversalResume()`
(`SimPlayerController.cpp:1455`) re-arms while `inCombat || combatDriverActive`,
so a stale defender referencing a destroyed object re-arms it forever.

Two candidate mechanisms remain unseparated — the bot stuck `isInCombat()`, or
the monitor ceasing to re-arm. Both are silent today. **First instrumentation to
add**: one line in `checkStructureTraversalResume()` logging `inCombat`,
`combatDriverActive` and `peaceSinceMs` per tick.

`resumeFailures` stayed **0** throughout: the counter never reaches its failure
paths (lines 1485/1502), so it is currently a misleading metric that cannot
observe this failure.

### D5b — stale combat contaminates every later scenario

`resetStructureTraversalTestBots()` clears traversal state but not combat state.
After the first combat scenario, the bot is permanently in combat:

```
gen=31 ST_PATH request ...                      t=1786741738423
gen=31 ApproachDoor → CombatPaused move_combat  t=1786741738423   <- same ms
```

`unreachable_target_bounded_failure` and `external_preemption` — both PASS in
run 1 — failed purely from this contamination, on scenarios with no attacker.
For `external_preemption`, preemption did fire and cleared generation 32, but
generation 33 immediately re-paused.

**These two are not regressions from the D2/D3/D4 changes.**

## Run terminated

Halted after scenario 20. D5b made every subsequent scenario untrustworthy —
they can no longer distinguish a real regression from contamination — and
`ten_sequential_cycles` carries a 1-hour budget it would have spent hanging in
`CombatPaused`. Scenarios 1–11 ran before any attacker spawned and are valid;
they show no regression from the D2/D3/D4 changes (all four run-1 passes in that
range passed again).

## Evidence

| Path | Contents |
| --- | --- |
| `MMOCoreORB/bin/log/trip-verify-20260814-132303-structure-traversal-foundation-structuretraversal.log` | 34,006 B run delta |
| `MMOCoreORB/bin/log/trip-verify-20260814-132303-structure-traversal-foundation-dashboard.jsonl` | dashboard samples at 5s |

## Cleanup

- `sim_player_manager.lua` restored byte-for-byte (md5 `0c42cbfaacbfe196fc80649c2af46efa`); gates back to `false`; `luac -p` clean.
- Server restarted on the restored config; no crash during the run.
- No receipt recorded.

## Defect queue after this run

| ID | Status |
| --- | --- |
| D1 | Open — egress truncated-path/hollow escalation (6 scenarios) |
| D2 | Diagnostics shipped; **underlying med-center egress defect still open**, now precisely characterised and known to be template-level |
| D3 | **FIXED, confirmed** |
| D4 | Code **FIXED, confirmed**; Theed scenario anchor still wrong (needs a real `buildingOid`) |
| D5a | Open — traversal never resumes after combat |
| D5b | Open — scenario reset leaks combat state into later scenarios |

Recommended order: **D5b** (cheapest, and it unblocks trustworthy results for
scenarios 19–25), then **D5a**, then **D2's fourth strategy**, then **D1**.
D4's Theed anchor can ride along with any run that enables `CellNavDiagLog`.
