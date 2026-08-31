# Live Verification — D2b far-side egress + Codex harness fixes

**PASS. Matrix 22 PASS / 4 FAIL of 26 — scenario-by-scenario IDENTICAL to run 9.
Zero regressions, zero differences.** The two Codex Major findings are fixed and
observable, and the carried reconstruction defect is now live-clean.

- **Run ID**: `20260828-101454-d2b-harness-fix`
- **Target**: `docs/1-plans/F_0.8.0-D2b_far-side-egress.proposal.md`
- **Contract**: 10 assertions, 10 passed
- **Branch**: `feat/structure-traversal-foundation` (uncommitted)

## Source under test

| # | Change | Where |
|---|---|---|
| 1 | D2b far-side egress (the feature) | `SimPlayerController::acceptFoundPath`, `tryStartFarSideInteriorLeg` |
| 2 | Codex Major 1 — exit assertion resolved the wrong building / failed open | `SimPlayerManager.cpp`, `SimTraversalTestController::isHarnessOutdoorsClearFor` |
| 3 | Codex Major 2 — harness runner was not single-writer | `SimPlayerManager::scheduleStructureTraversalTest{Runner,Spawn}`, `runStructureTraversalTestRunner[Body]` |
| 4 | Carried from run 8 — restored redundant-phase guard in the RECONSTRUCTED `setStructureTraversalPhase` | `SimPlayerController.cpp` |

Changes 2-4 are harness/telemetry-side, so the governing question was regression;
2 and 4 had never been observed live before this run.

## Gates

Build clean (clang `-Werror`, 0 warnings — the 5 grep hits are the flag echo, not
diagnostics). `luac -p` clean on all three changed Lua files, before and after the
run. No new unit tests: the code drives real `AiAgent` pathfinding against
navmesh/PortalLayout data that only exists in a running zone server, so the live
matrix is the verification vehicle (recorded in `COVERAGE-DEBT.md`).

## Restart

`server-cycle.sh restart` — 0 connected players, restart authorized, `core3`
ready with a fresh `who.json` after **10 s**. Traversal config loaded within
another ~45 s (26 scenarios armed). No force-kill needed.

## Configuration

Exactly the run-9 gate set, recovered from that run's own dashboard snapshot so
the comparison is like-for-like:

```
structureTraversal.enabled                        = true
structureTraversal.logging                        = true
structureTraversal.hollowEscalationEnabled        = true
structureTraversal.farSideEgress                  = true
structureTraversal.hollowDoorEgress.{observe,walk,useCellPortals} = true
structureTraversal.zeroClip.{enabled,logging}     = true
structureTraversal.zeroClip.{enforce,exitSetEnabled} = false
structureTraversalTest.enabled                    = true
```

Ten flag flips, diffed line-by-line against the backup before the restart.
Everything else at committed defaults.

## Assertions

| # | Assertion | Expected | Actual | |
|---|---|---|---|---|
| A1 | Clean start, `core3` alive throughout, no crash | no crash | alive; no backtrace/SIGSEGV/SIGABRT markers in `gdb.log` | PASS |
| A2 | No new FATAL/ERROR signature | 0 | 1 match, and it is GDB's benign `Error disabling address space randomization` at startup — not attributable | PASS |
| A3 | 26 scenarios terminal; 26 unique results | 26 / 26 | 26 `SCENARIO_RESULT`, each scenario name exactly once | PASS |
| A4 | **No regression**: PASS >= 22 and every run-9 PASS still passes | >= 22 | **22 PASS / 4 FAIL; 0 scenario-by-scenario differences; 0 regressions** | PASS |
| A5 | Exit assertions non-vacuous: no `scenarioBuilding=0` | 0 | 28 `exitAssert` lines, all `pass=1`, all with a non-zero `scenarioBuilding` | PASS |
| A6 | Fix 1 observable: no `exit_building_unresolved`; no exit assertion against cantina 8105493 | 0 / 0 | 0 / 0 | PASS |
| A7 | Fix 4 observable: zero no-op `ST_PHASE from=X to=X` | 0 | **0 of 165 transitions** (run 8 emitted 128) | PASS |
| A8 | Anomaly counters: teleports / zSanity / resumeFailures delta 0 | 0 | 0 / 0 / 0 — every anomaly counter byte-identical to run 9 | PASS |
| A9 | Fix 3 sanity: no duplicated or skipped scenario result | 26 unique | 26 unique, cursor 26 | PASS |
| A10 | Cleanup | config restored, 0 bots | config md5 `1b6e3e84…` byte-identical to pre-run; `bots: []` | PASS |

## Regression comparison vs run 9 (`20260827-200923-d2b-farside`)

| | run 9 | run 10 |
|---|---|---|
| result | 22 PASS / 4 FAIL of 26 | **22 PASS / 4 FAIL of 26** |
| scenario-by-scenario differences | — | **0** |
| `far_side_no_progress` rejections | 13 | 13 |
| `farSide action=interior_leg` starts | 13 | 13 |
| teleportsDetected / zSanityViolations / resumeFailures | 0 / 0 / 0 | 0 / 0 / 0 |
| egressPathFailures / pathfinderFallbackActivations | 2 / 2 | 2 / 2 |
| zeroClip exitSetsBuilt | 13 | 13 |
| transit scenario | PASS 84120 ms | PASS 84077 ms |
| `cell_to_enclosed_hollow` | PASS 204172 ms | PASS 204179 ms |

The four remaining failures are unchanged and individually tracked:
`naboo_hospital_enter_exit` and `cantina_to_corellia_hospital`
(`controller_path_failed`), `theed_starport_hangar` (`target_cell_unresolved`,
defect D4), `attacker_dies_instantly` (`combat_pause_not_observed`, defect D6,
harness-side).

`zeroClip.wouldBlock` (108 -> 139) and `clearanceChecks` (1340 -> 1225) are D7
Phase-1 observation counters that track the specific route geometry sampled;
`enforce=false` and `blocked=0` in both runs, so they gate nothing.

## What this run establishes that the previous one could not

**The 22 PASS is now genuine.** Run 9's number came from a harness whose exit
assertion could resolve the wrong building and failed open on an unresolved one.
This run reproduces the same 22 with an assertion that fails closed and derives
the building from the resolved enter target, and all 28 exit assertions carry a
real building OID.

**The reconstructed `setStructureTraversalPhase` is now live-clean.** Run 8
emitted 128 no-op phase transitions (36 ApproachDoor, 91 Egress, 1 InteriorRoute)
because my first reconstruction omitted the redundant-phase early-return. This
run: **0 of 165**. That function remains reconstructed-from-evidence rather than
recovered, and that stands as a documented risk, but its one known defect is
closed and verified.

**D2b's mechanism is doing the work.** 13 `far_side_no_progress` rejections each
converted into an interior leg targeting the west door
`(3539.62, -4814.24, 5.56)` at `cellOid=1106369`, 80.4 m away — identical to run 9.

## Evidence

- `bin/log/trip-verify-20260828-101454-d2b-harness-fix.log` — 2210 lines, 419 KB, diag delta for this run
- `bin/log/trip-verify-20260828-101454-d2b-harness-fix-dashboard.jsonl` — 1.6 MB sampled `structureTraversal` section, 30 s interval

Representative exit assertion (west side, adjacent to the x=3527 destination):

```
ST_HARNESS exitAssert scenario=mos_eisley_starport_front pass=1
  scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=0
  hollowMissDistance=0 pos=(3529.84,-4802.95,5.08505)
```

## Cleanup

`sim_player_manager.lua` restored from backup, md5 `1b6e3e84717831e11d70b9b08dd0deb5`,
byte-identical to committed defaults and re-parsed with `luac -p`. All gates back
to default-off. Zero harness bots left alive. No database, roster, or unrelated
log touched.

## Verdict

**LIVE_VERIFICATION_PASS** — 10 of 10 assertions passed.
