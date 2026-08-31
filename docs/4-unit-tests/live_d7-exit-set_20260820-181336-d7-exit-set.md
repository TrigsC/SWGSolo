# Live Verification — F_0.8.0 D7 Part 2 (POB exit set)

**Verdict: `LIVE_VERIFICATION_FAIL`, with an `OBSERVABILITY_GAP` blocking
diagnosis.** No receipt recorded.

**Part 2 does not work.** The exit set is built 8 times and yields
`candidates=0` every single time, so **not one exit candidate was ever tried**
(`exitCandidatesTried = 0`). The per-candidate budget the owner approved was
therefore never exercised at all — it cannot advance through a set that is empty.

This is the same failure shape as D1 run 4: the mechanism is wired correctly and
the datum it was pointed at does not contain what the design assumed.

- **Run ID**: `20260820-181336-d7-exit-set`
- **Date**: 2026-08-20
- **Subject**: Part 2 only — POB exit set + per-candidate egress budget
- **Code state**: Codex APPROVED, build clean (0 errors / 0 warnings)

## Assertions

| # | Assertion | Expected | Actual | |
| --- | --- | --- | --- | --- |
| A1 | `exitSetsBuilt > 0` | > 0 | **8** | PASS |
| A2 | `ST_EGRESS exitSet=build candidates=N`, N > 0 | N > 0 | **0, all 8 times** | **FAIL** |
| A3 | `exitCandidatesTried > exitSetsBuilt` | > 8 | **0** | **FAIL** |
| A4 | no bot stuck in a cell with an untried candidate remaining | none | vacuous — there were never any candidates | **NOT PROVEN** |
| A5 | run-3's 10 passing scenarios still pass | 10 | **10, zero regressions** | PASS |
| A6 | `blocked == 0` (enforce inert) | 0 | **0** | PASS |
| A7 | `teleportsDetected` / `zSanityViolations` | 0 / 0 | **0 / 0** | PASS |
| A8 | clean startup, alive, no crash | yes | **gdb.log unchanged at 876 lines; core3 alive 1h10m** | PASS |

Matrix: **25/25 resolved, 10 PASS / 15 FAIL** — identical pass count to runs 3
and 4, no regressions and no new passes.

## The finding

Every build logged the same thing:

```
ST_EGRESS exitSet=build candidates=0 worldPortalCorroborated=true
```

`worldPortalCorroborated=true` on all 8. So `CellProperty::hasWorldPortal()`
— the flag with **zero consumers anywhere in the codebase** — correctly reports
that the bot's cell chain does reach the outside. The POB knows the building has
a way out.

But `PathGraph::getEntrances()`, which the design made **authoritative**,
returned nothing usable. It filters on `node->getType() == PathNode::BuildingEntrance`
(type 3). The likely explanation is that a *building's own* exterior path graph
does not type its door nodes as `BuildingEntrance` — that type may belong to
CITY-level path graphs, with per-building doors carrying `CellPortal` (0),
`BuildingPortal` (5) or `CityBuildingEntrance` (6) instead. D1 run 4 reached
the same nodes through `findNearestGlobalNode` (any node with
`getGlobalGraphNodeID() != -1`) and *did* find three of them on the Mos Eisley
starport, which is consistent with the nodes existing but not being typed
`BuildingEntrance`.

**That is a hypothesis, not a conclusion**, which is the gap below.

## `OBSERVABILITY_GAP`

`candidates=0` cannot distinguish between:

1. `getEntrances()` returned an empty vector (wrong node type assumed), and
2. entrances were returned but every one was rejected by
   `zone->isWithinBoundaries()` or the de-duplication.

Required instrumentation, default-off and run-scoped, before this can be fixed
rather than guessed at:

- total nodes in the exterior path graph, and a **histogram by `PathNode::typeToString`**;
- `getEntrances()` raw size, before filtering;
- rejection counts split by reason (out-of-bounds vs duplicate);
- the same for the `worldPortal` BFS: cells visited, cells flagged.

Per TRIP-verify, implementation code is not edited from verification mode, so
this returns to `TRIP-2-implement`.

## Part 1 rode along (not the subject, but recorded)

929 clearance checks, **104 `would_block`**, 42 truncated, 477 skipped,
0 errors, `blocked=0`. Block rate over conclusive paths ≈ **23.0%**,
consistent with run 2's 25.2% on a completely different traffic mix. The probe
is stable.

## Note on the changed failure modes

12 scenarios moved from `controller_path_failed` to `exit_not_outdoors`
versus run 4. That is expected and is my configuration choice, not a regression:
this run deliberately set `hollowEscalationEnabled=false` and
`hollowEscalationDirectFallback=false` to isolate Part 2 from D1's superseded
clipping path, which reverts those scenarios to their original pre-D1 failure.

## Cleanup confirmed

`sim_player_manager.lua` restored to md5 `bf7b26d9a27a85ce7f381c828bc83949`
— `structureTraversal.enabled=false`, `structureTraversalTest.enabled=false`,
`zeroClip.enabled=false`, `exitSetEnabled=false`, `enforce=false`. Sampler
stopped. No databases, rosters or unrelated logs touched. Evidence retained at
`bin/log/trip-verify-20260820-181336-d7-exit-set-structuretraversal.log`.
