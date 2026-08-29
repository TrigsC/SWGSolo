# Live Verification — controlled test: suppressLeash OFF

**Verdict: `LIVE_VERIFICATION_FAIL`** (10 PASS / 15 FAIL), and the test is
**conclusive**: `suppressLeash` is NOT the blocker. Root cause now identified.

- **Run ID**: `20260826-170712-d8-noleash` — evidence `bin/log/trip-verify-20260826-170712-d8-noleash.log` (2224 lines)
- Single variable changed from run 3: `suppressLeash = false`. Same binary.
- `leash=` lines: **0** (suppression never installed). Freeze is IDENTICAL.
- Failure profile byte-identical to run 3: 11 `exit_budget_exceeded`,
  2 `controller_path_failed`, 1 `target_cell_unresolved`, 1 `combat_pause_not_observed`.

Two hypotheses eliminated this run:
1. **Leash suppression blocking the portal transition** — refuted directly.
2. **`moveToWithOrigin(Internal)` differing from the working enter** — refuted by
   reading `moveToInterior()`, which is exactly
   `interiorApproachLeg = true; moveToWithOrigin(..., External);`. Leg B already
   sets the latch, so the only difference is the origin flag, and the path IS
   accepted either way.

## ROOT CAUSE: the 4-metre arrival radius fires before the bot crosses the portal

`SimPlayerController::checkArrival`:

```cpp
if (distSq < 16.0f) arrived = true;   // 4m proximity, horizontal
```

Measured entry leg:
```
botPos=(3616.75,-4842.3,5.08505)  target=(3613.8,-4845.38,5.83582) cell=1106383
ST_PATH result=accepted nodes=4  local=(-44.1848,6.03827,0.835817)
```
The bot closes from 4.33m to **1.95m** and climbs 5.085 -> 5.471 toward the door
sill at 5.836 (owner: "the starport doors are elevated"). At 1.95m,
`distSq ~= 3.8 < 16` so `arrived = true` **while the agent is still outdoors and
its parent is still null**. The final path node -- the one that carries the cell
parent change -- is never executed.

Everything downstream is that one fact wearing three different masks:

| run | what the "arrival" was routed into | symptom |
|---|---|---|
| 1 | escalation re-check -> `Failed` | `controller_path_failed` x13 |
| 2 | `NotHandled` tail -> `clearInteriorApproachLeg()` + `onArrived()` | `exit_not_outdoors` x11 |
| 3,4 | `InProgress` -> re-arm, return (correct) | `exit_budget_exceeded` x11 (loops until budget) |

Run 3/4 behaviour is now CORRECT for what it is told: it is asked every tick
whether an in-flight entry leg should be preempted, it says no, and re-arms. It
loops because the leg it is protecting can never complete.

## The fix has an exact precedent in this file

Immediately below the proximity check:

> "Cell-egress 'arrival' means the agent is actually OUTDOORS, not merely within
> 4m of the ejection point. The egress path ends with outdoor (cell 0) nodes;
> without this, the coarse 4m proximity check can fire while the agent is still a
> few metres inside the last cell (short of the final portal)..."

The same defect, already solved for the EGRESS direction by requiring real
parent state instead of proximity. The ENTRY direction needs the mirror guard:

**an entry leg whose destination is a CellObject has not arrived until
`agent->getParent()` IS that cell.** Suppress `arrived` from the 4m radius (and
from `queueExhausted`) while `destinationCell != nullptr` and the parent has not
yet changed, so the mover executes the final node that performs the transition.

Bounded by the existing stuck-watchdog and the scenario budget, so a door that
genuinely cannot be crossed still fails rather than hanging.

## Cleanup

`sim_player_manager.lua` restored to default-off, `luac -p` clean. Running server
retains gates until the next restart.
