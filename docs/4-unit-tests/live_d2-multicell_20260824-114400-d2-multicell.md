# Live Verification — D2 Retry 6 (`multicell`)

**Verdict: `LIVE_VERIFICATION_PASS` for the slice contract.**
**First new scenario pass in the entire F_0.8.0 sequence, and the first
`result=success` from any of the six repair strategies.**

D2 is **half closed**: one of its two scenarios now passes, the other has moved
off pathing entirely.

- **Run ID**: `20260824-114400-d2-multicell`
- **Change**: Retry 6 `repair=multicell` — BFS over portal links, one floor
  route per cell along the chain. Codex APPROVED after 2 rounds.

## Matrix: 11 PASS / 14 FAIL (was 10 / 15)

| | Before | After | |
| --- | --- | --- | --- |
| PASS | 10 | **11** | ▲ first movement since run 3 |
| `cantina_to_corellia_hospital` | `controller_path_failed` | **PASS** | NEW |
| `naboo_hospital_enter_exit` | `controller_path_failed` | `exit_budget_exceeded` | moved off pathing |
| `egressPathFailures` | 6 | **0** | ▼ |
| `pathfinderFallbackActivations` | 6 | **2** | ▼ |
| `zSanityViolations` / `teleportsDetected` | 0 / 0 | 0 / 0 | held |
| regressions | — | **0** | |

## The route it builds

```
bfs result=found cellsVisited=4 budgetHit=0 chainLen=2 exitCell=7 exitPortalGeom=0
hop=0 fromCell=6 toCell=7 geometryIndex=15 doorModel=-31.125,0.25,11 doorCellLocal=-31.125,11,0.25 floorResult=0 reachable=1 segLen=11.71
hop=1 fromCell=7 toCell=0 geometryIndex=0  doorModel=-16.75,0.25,20  doorCellLocal=-16.75,20,0.25  floorResult=0 reachable=1 segLen=16.96
result=success reason=ok hops=2 nodes=9
```

Cell 6 → 11.7 m to the doorway into cell 7 → 16.96 m to a portal onto cell 0.
Two hops, nine nodes, exit found after four cells with the budget untouched.

Three properties visible in that output rather than assumed:

- **`floorResult=0` on both hops** — each leg is a real triangle-mesh route, so
  the bot walks the floor rather than through the room.
- **The frame conversion is correct and auditable**:
  `doorModel=-31.125,0.25,11` → `doorCellLocal=-31.125,11,0.25`, components 1
  and 2 swapped, with `0.25` the floor height already lowered from the AABB
  centre. This conversion was wrong twice before; it is now logged in both frames.
- **`budgetHit=0`** — last round's BFS fix is not masking anything; the exit was
  genuinely found early.

It fired **twice**, both times succeeding — building `1697358` cell `1697364`
(Naboo) and building `1855529` cell `1855535` (Corellia). Identical topology:
`chainLen=2`, `exitCell=7`, `exitPortalGeom=0`. That is the template-level
behaviour the diagnosis predicted.

## What is NOT fixed

`naboo_hospital_enter_exit` now fails `exit_budget_exceeded`, **not** a path
failure. Its exit set builds cleanly (`candidates=2`, `rejectedElevation=1`,
`cellsWithWorldPortal=6`) and `exitCandidatesTried=5` against
`exitSetsBuilt=4`, with `egressCandidateBudgetExhausted=0`. So the bot is
finding doors and attempting them; the harness's own per-scenario exit budget
runs out first.

That is a **D7 budget-tuning question, not a D2 pathing one** — a different
subsystem from the one this slice fixed, and it should be treated as such rather
than folded in.

The 11 `exit_not_outdoors` failures are untouched and remain the dominant
blocker (mode 2, walled hollows). Nothing here addresses them.

## Assertions

| # | Assertion | Actual | |
| --- | --- | --- | --- |
| Retry 6 produces valid multi-cell routes | ≥1 success | **2 / 2 attempts** | PASS |
| D2 improves | ≥1 scenario better | **1 PASS + 1 moved off pathing** | PASS |
| No regression | 10 baseline passes hold | **all 10 hold, 0 regressions** | PASS |
| No new anomalies | z-sanity / teleports 0 | **0 / 0**, egressPathFailures 6→0 | PASS |
| No crash | gdb.log unchanged | 876 lines, alive 1h14m | PASS |

## Cleanup

Config restored to all gates off (md5 `7a054f01b6f7c148b3cc65492469d39e`).
Sampler stopped. Evidence at
`bin/log/trip-verify-20260824-114400-d2-multicell-structuretraversal.log`.
