# Live Verification — D7 Part 2 with the `CellPortal` selector

**Verdict: `LIVE_VERIFICATION_FAIL`.** No receipt recorded.

**The selector fix works. The candidate *ordering* does not, and it introduced a
regression: `zSanityViolations` went from 0 in every prior run to 240.**

The exit set is now populated and the per-candidate budget genuinely advances —
both assertions that failed on 2026-08-20 now pass. But the set is ordered by
**2D** distance while containing doors at wildly different **elevations**, so
bots are being sent to a door up to 77 m above their own floor.

- **Run ID**: `20260823-125017-d7-exit-set-cellportal`
- **Change**: exit-set selection switched from `getEntrances()` (type 3) to
  `getType() == PathNode::CellPortal` (type 0), per the measured diagnosis
- **Code state**: Codex APPROVED, build clean

## Assertions

| # | Assertion | Expected | Actual | |
| --- | --- | --- | --- | --- |
| A1 | `exitSetsBuilt > 0` | > 0 | 7 | PASS |
| A2 | `candidates > 0` per build | > 0 | **3 on every build** (`doorNodes` 3–5) | **PASS** (was FAIL) |
| A3 | `exitCandidatesTried > exitSetsBuilt` | > 7 | **17** | **PASS** (was FAIL) |
| A4 | no bot stuck with an untried candidate | none | `budgetExhausted=0`; but see A7 | INCONCLUSIVE |
| A5 | run-3's 10 passes still pass | 10 | **10, zero regressions** | PASS |
| A6 | `blocked == 0` | 0 | 0 | PASS |
| A7 | `zSanityViolations == 0` | 0 | **240** | **FAIL** |
| A8 | no crash | none | gdb.log unchanged (876 lines), alive 1h14m | PASS |

Matrix: 25/25 resolved, 10 PASS / 15 FAIL — same counts, no regressions and no
new passes.

## Root cause of the regression: 2D ordering over a 3D candidate set

Model-space `.Y` is HEIGHT. The elevation spread across all logged candidates:

```
  4 x  -19.997      5 x  6.0401
  2 x    0.003      5 x 10.335
                    5 x 77.6163   <-- 77 metres up
```

Building `4005516`, bot in cell 1:

```
exitSet=candidate index=0 model=-0.189,77.6163,0.639 world=1238.39,3062.48,84.6163 distFromBot=75.4531
exitSet=candidate index=1 model=-44.169, 6.0401,0.975 world=1202.54,3087.95,13.0401 distFromBot=84.6442
exitSet=candidate index=2 model= 52.218,10.335 ,0.975 world=1281.54,3032.74,17.335  distFromBot=94.2373
```

The door **77.6 m above** the building origin is ranked **first**, because
`distanceTo2d` discards the vertical axis entirely: its 2D distance (75.5 m) is
the smallest of the three. The bot then attempts to walk to a point ~70 m above
its own floor, the z-sanity watchdog fires, and with `zSanityMeters = 5` it
fires repeatedly — 240 times across the run.

Same shape at building `1697358` (candidates at world Z 6.0 and −14.0, a 20 m
drop) and `1026824` (world Z 89.6 vs 22.3 vs 18.0).

Corroborating counters: `egressPathFailures` 2 → **10**,
`pathfinderFallbackActivations` 2 → **10**, and
`cantina_to_corellia_hospital` changed failure mode from
`controller_path_failed` to **`movement_anomaly`** — the anomaly being exactly
this.

## What this says about the selector itself

The selector is **right about which nodes are doors** and wrong about nothing
else: `entrancesRaw=0` on every build confirms `getEntrances()` still yields
nothing, while `doorNodes` 3–5 with `rejectedBounds=0` shows real, in-bounds
world positions. `CellPortal` is simply broader than "ground-level way out" —
it includes upper-level and sub-level portals.

**Do not revert the selector.** The fix belongs in candidate filtering/ordering:

1. **Order by 3D distance**, not `distanceTo2d`.
2. **Reject candidates outside a vertical band** around the bot's own elevation,
   tied to `zSanityMeters` so the exit set cannot propose a move the watchdog is
   guaranteed to reject. A door the bot provably cannot walk to is not a
   candidate.

Both are small and local to `buildCellEgressExitSet`, and the telemetry to
prove them already exists (the per-candidate lines log model, world and distance).

## `naboo_hospital_enter_exit` — new failure mode, and it is progress

It moved from `controller_path_failed` to **`exit_budget_exceeded`**. That is
the per-candidate budget doing its job: the bot no longer gives up after one
door, it works the set and exhausts it. With the elevation filter in place those
attempts should land on reachable doors instead.

## Cleanup

Config restored to md5 `bf7b26d9a27a85ce7f381c828bc83949` (all gates off,
harness off). Sampler stopped. Evidence at
`bin/log/trip-verify-20260823-125017-d7-exit-set-cellportal-structuretraversal.log`. No databases, rosters or
unrelated logs touched.
