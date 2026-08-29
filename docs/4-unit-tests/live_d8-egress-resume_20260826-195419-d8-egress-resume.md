# Live Verification — egress resume after door entry

**Verdict: `LIVE_VERIFICATION_FAIL`** (10 PASS / 15 FAIL). The resume works; the
remaining blocker is now isolated to **D2 interior topology**, and it is not
something D8 can fix.

- **Run ID**: `20260826-195419-d8-egress-resume` — evidence `bin/log/trip-verify-20260826-195419-d8-egress-resume.log` (2391 lines)
- Failures: 13 `controller_path_failed`, 1 `target_cell_unresolved`,
  1 `combat_pause_not_observed`. `hollowEscalationsFailed=11` (looping).

## The resume fires correctly

```
t=667033  ST_PHASE ApproachDoor -> InteriorRoute reason=entered_structure
t=667033  ST_PATH request botPos=(3613.79,-4845.36,5.97457) world=(3527,-4803,5) cell=0
t=667035  ST_PHASE InteriorRoute -> Egress reason=internal_egress
t=667139  ST_PATH accepted nodes=8 world=(3616.28,-4845.67,5.10497) cell=0
```

The bot enters cell 15, and the egress correctly re-issues toward the outdoor
destination at x=3527 (WEST). Both D8 fixes are behaving as designed.

## The blocker: the returned path exits the door it just came in

The request targets **x=3527** (west). The accepted path ends at
**x=3616.28, y=-4845.67** -- the pad-side door, ~2m from where the bot entered.
So the bot goes in, turns around, comes back out onto the pad, is still inside
the hollow, escalates, and loops until the attempt cap fails it.

**The repair ladder never engages for the starport.** Every `repair=multicell`
line in this run belongs to `building=1697358` (naboo hospital, which routes
fine: `exitChoice cell=11 depth=4 distToTarget=10.5533 candidates=6`). The
starport egress produces no repair lines at all, because base pathfinding
**succeeds** -- it returns a valid 8-node path out the nearest portal. The
repair ladder is only reached on path FAILURE. A path that succeeds but goes
nowhere useful is invisible to it.

This is the D2 finding restated with the bot finally inside the building: the
interior path graph from the pad-side cells does not reach the west exterior
doors (source cell global IDs {10,13,14,15,16} vs exterior {1}, disjoint), so
the pathfinder does the only thing it can -- leaves by the nearest portal.

## What this means

D8's scope (get the bot from the pad through a door) is **complete and working**:
floor-level door resolution, correct interior cell, pad-side preference, portal
crossing over an elevated sill, and egress resume. The remaining failure is the
pre-existing D2 interior-topology problem, now reachable for the first time.

## Options for the next slice (owner decision, none applied)

1. **Engage the repair ladder on a path that "succeeds" but ends short of the
   requested destination.** This is what `requireCompletePath` measures; the
   config comment records it works but costs `cantina_to_corellia_hospital`,
   whose route legitimately ends short. Would need a tolerance keyed to
   "ended at a portal of the building we are trying to leave" rather than raw
   distance.
2. **Force the multicell repair for hollow egress specifically** -- when leaving
   an enclosed hollow, require the chosen exit to be a portal with
   `inHollow == 0`, which the exit-set telemetry already computes
   (`outsideHollow=4` for this building).
3. **Accept the two-hop route explicitly**: treat "exit into hollow" as a failed
   egress and re-target the west door as a fresh traversal from inside the cell.

Option 2 reuses machinery that already exists and already knows which portals
leave the hollow.

## Six-run progression

| run | change | crosses portal | resumes egress | dominant failure |
|---|---|---|---|---|
| 1 | gates on | no | - | `controller_path_failed` x13 |
| 2 | +entry gate | no | - | `exit_not_outdoors` x11 |
| 3 | +InProgress | no | - | `exit_budget_exceeded` x11 |
| 4 | -suppressLeash | no | - | `exit_budget_exceeded` x11 |
| 5 | +entry arrival guard | **yes** | no | `exit_not_outdoors` x11 |
| 6 | +egress resume | yes | **yes** | `controller_path_failed` x13 |

The PASS count held at 10/15 throughout while the mechanism advanced every run.
The count was never the useful signal; the failure reason and the telemetry were.

## Cleanup

`sim_player_manager.lua` restored to default-off, `luac -p` clean. Server retains
gates in memory until the next restart.
