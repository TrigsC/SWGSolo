# Live Verification — D8 useCellPortals + suppressLeash

**Verdict: `LIVE_VERIFICATION_FAIL`.** Matrix **10 PASS / 15 FAIL** against an
11 PASS / 14 FAIL baseline — a net regression of one, with a *changed failure
signature* that pinpoints the next defect.

- **Run ID**: `20260826-081121-d8-cellportals-leash`
- **Evidence**: `bin/log/trip-verify-20260826-081121-d8-cellportals-leash.log` (1989 lines, post-restart delta)
- Restart: clean, 0 test players disconnected, ready in 9s, PID 3322329
- Counters: `zSanityViolations=0 teleportsDetected=0 resumeFailures=0`
  `egressPathFailures=2 pathfinderFallbackActivations=2 hollowEscalationsFailed=11`

## Both gates do exactly what they were built to do

```
doorEgress cellResolve door=3541.39,-4802.76,5.63282 targetCellIndex=1
doorEgress cellResolve door=3613.8,-4845.38,5.83582  targetCellIndex=15
doorEgress cellResolve door=3607.66,-4749.08,5.59342 targetCellIndex=16
doorEgress result=found doors=5 nearestDist=36.0914 botPos=3575.89,-4813.35,5.08505
doorEgress leash=suppressed previousMap=4241078320 generation=6 movementStateBefore=4 movementStateAfter=4
doorEgress action=walking  target=3613.8,-4845.38,5.83582 dist=49.6334 attempt=1
doorEgress action=entering door=3613.8,-4845.38,5.83582 cellIndex=15 cellOid=1106383 distFromBot=4.33273
doorEgress result=entered reason=door_cell_resolved
```

- **`useCellPortals` — WORKS.** Doors resolve at floor level (z 5.56–5.84)
  instead of the path-graph node at z 11.04. That was the owner-identified
  elevated-door problem, and it is fixed: the bot now reaches and crosses a door.
- **Interior-member cell rule — WORKS.** `targetCellIndex=15/16/1`, matching the
  earlier PathGraph run's 15/16. No `getCell(0)` error path taken.
- **Pad-side preference (review fix 2) — WORKS.** Selected cell 15 at 49.6m over
  the west doors at x≈3539 (`outsideHollow=4`), i.e. the reachable door.
- **`suppressLeash` lifecycle — CLEAN.** 11 suppressed / 11 restored /
  **0 install_abandoned**. Generation stamps pair correctly (`generation=6` →
  `generation=6`). No stranded bot, no orphaned no-op MOVE map. The
  agent-lock serialization and generation-owned restore hold under live churn.
- Leash telemetry answered the owner's hypothesis: `movementStateBefore=4`
  → `movementStateAfter=4` on install, `4 → 0` on restore.

## Root cause of the regression — a 1-second race, NOT the two gates

11 entries, and **all 11** then hit:

```
+0.0s  doorEgress result=entered reason=door_cell_resolved
+0.1s  ST_PATH result=accepted nodes=4 cell=1106383   <- entry leg issued
+1.0s  ST_EGRESS escalation=result status=still_inside reason=attempt_cap
+1.0s  ST_FAIL reason=path_failed
```

Measured accept→fail gaps: **1005, 1007, 1008, 1006, 1006 ms** — the
arrival-check tick.

The bot is **1.95 m from the door and still walking** when the hollow-escalation
re-check fires on the next arrival tick. It observes "still inside the hollow"
(true — it has not crossed yet), finds `hollowEscalationAttempts` already at
`hollowEscalationAttemptCap = 1`, and declares the traversal failed.

**The escalation re-evaluation is not gated on an in-flight entry leg.** It
preempts the very entry it just scheduled. This is the same class of bug as the
already-fixed `external_move_preemption`: a leg being destroyed by the
machinery that started it.

## Why the count got worse

Baseline failures were `exit_not_outdoors` — the bot never reached a door.
Now it reaches and enters one, so the failure moves downstream to
`controller_path_failed` (13 of 15). We traded "stuck on the pad" for "killed
1 second into the entry". Strictly more progress, one fewer PASS.

## Recommended fix (D8 follow-up, NOT applied — verification mode)

1. Gate the hollow-escalation re-evaluation on `interiorApproachLeg` (and/or the
   door-entry latch) so it cannot fire while an entry leg is in flight. This is
   the actual defect.
2. Only then reconsider `hollowEscalationAttemptCap`; raising it alone would
   mask the race with retries rather than fix it.

## Remaining non-mode-2 failures (unchanged, pre-existing)

- `theed_starport_hangar` — `target_cell_unresolved` (D4)
- `attacker_dies_instantly` — `combat_pause_not_observed` (D6, harness-side)

## Deviations from the TRIP-verify contract

- Codex review did **not** reach APPROVED. The loop capped at 8 rounds; the
  residual stale-pause Major is deferred as **D9** (documented with fix
  direction and blast radius). Owner explicitly authorized this run anyway.
- `zeroClip.enforce` deliberately left **false**: this run measures clipping,
  it does not block on it. Enforcement is the separate D7 Phase 2 decision.

## Cleanup

`sim_player_manager.lua` restored to committed default-off values and
`luac -p` clean. **The running server still has the gates live in memory** — the
restored file takes effect on the next restart, which is the owner's call.
