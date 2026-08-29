# Live Verification — entry-side arrival guard

**Verdict: `LIVE_VERIFICATION_FAIL`** (10 PASS / 15 FAIL) **but the guard WORKED
and the bot is now INSIDE the starport for the first time.**

- **Run ID**: `20260826-183906-d8-entry-arrival` — evidence `bin/log/trip-verify-20260826-183906-d8-entry-arrival.log` (2155 lines)
- Only variable vs run 4: the new binary. Config byte-identical
  (`suppressLeash` stayed false).
- Counters: `zSanity/teleports/resumeFailures/hollowEscalationsFailed = 0`

## The portal crossing now works

```
t=162617  doorEgress action=entering door=3613.8,-4845.38,5.83582 cellIndex=15 cellOid=1106383
t=162724  ST_PATH result=accepted nodes=4 cell=1106383
t=164233  ST_PHASE ApproachDoor -> InteriorRoute reason=entered_structure     <- 1.5s
t=164609  exitAssert inCell=1 parentCell=1106383 pos=(3613.79,-4845.36,5.97457)
```

`parentCell=1106383`, `inCell=1`, z climbed 5.085 -> 5.975 over the elevated
sill. The 53-second freeze is gone; the crossing takes 1.5s. The 4m proximity
radius was the whole blocker, exactly as diagnosed.

## NEXT BLOCKER: the egress does not resume after entering

The exit step ends the instant the bot is inside:

```
exitAssert pass=0 inCell=1 ... -> SCENARIO_STEP op=exit FAIL reason=exit_not_outdoors
```

Mechanism: now that the bot IS parented to a cell, `hollowEscalationArrival` is
false (its condition requires `parent == nullptr || !parent->isCellObject()`), so
checkArrival takes the ORDINARY arrival tail:

```cpp
completeStructureTraversalIfArrived(currentPos);   // return value DISCARDED
clearInteriorApproachLeg();
locker.release();
onArrived();                                        // step reported complete
```

`completeStructureTraversalIfArrived` returns `NotHandled`: `hollowEscalationActive`
is false, the door candidate was consumed, `exitIntent && !inCell` is now false,
and an exit intent has no `finalTargetCell` to match. So nothing re-issues a move
toward the outdoor destination, and `onArrived()` tells the harness the exit
finished — while the bot stands just inside cell 15.

The bot went IN the pad-side door and now needs to continue OUT the west side.
That is the "pad -> pad-side door -> interior -> west exit" route, and the
interior leg is precisely what `repair=multicell` (Retry 6) exists to solve.

## Recommended fix (NOT applied)

There is already a mechanism for exactly this: `HollowEscalationOutcome::ResumeFinalDestination`,
handled in the escalation branch as

```cpp
clearInteriorApproachLeg();
moveToWithOrigin(finalWorld, finalLocal, finalCell.get(), TraversalMoveOrigin::Internal);
```

Two changes:
1. `completeStructureTraversalIfArrived` should return `ResumeFinalDestination`
   when an exit-intent traversal has just completed a door-entry leg and is now
   inside a cell, instead of `NotHandled`.
2. The ordinary arrival tail must HONOUR that outcome instead of discarding the
   return value and calling `onArrived()` — the same discipline the escalation
   branch already applies.

## Progression across five runs

| run | change | door entries | crosses portal | dominant failure |
|---|---|---|---|---|
| 1 | gates on | 11 | no | `controller_path_failed` x13 |
| 2 | +entry gate | 11 | no | `exit_not_outdoors` x11 |
| 3 | +InProgress | 13 | no | `exit_budget_exceeded` x11 |
| 4 | -suppressLeash | 13 | no | `exit_budget_exceeded` x11 |
| 5 | +entry arrival guard | 11 | **YES** | `exit_not_outdoors` x11 |

`suppressLeash` remains OFF and unneeded so far; runs 4 and 5 both ran without it.

## Cleanup

`sim_player_manager.lua` restored to default-off, `luac -p` clean.
