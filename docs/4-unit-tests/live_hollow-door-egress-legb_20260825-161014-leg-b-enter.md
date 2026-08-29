# Live Verification — mode 2 solved: hollow door egress, Legs A + B

**Verdict: `LIVE_VERIFICATION_PASS`. Matrix 11 PASS / 14 FAIL -> 22 PASS / 3 FAIL.**
**All eleven `exit_not_outdoors` failures are gone. Zero regressions.**

Mode 2 — the dominant blocker for the entire F_0.8.0 foundation — is solved by
walking bots through the starport's own doors, exactly as the owner's spur model
predicted.

- **Run ID**: `20260825-161014-leg-b-enter`
- **Entries executed**: 13
- `zSanityViolations` / `teleportsDetected` / `egressPathFailures`: **0 / 0 / 0**
- No crash (gdb.log 876). Miners unaffected (`0 pathFailures`).

## The eleven scenarios that flipped

`mos_eisley_starport_front`, `mos_eisley_starport_deep_foyer4`,
`starport_upper_floor`, `cell_to_enclosed_hollow`, `combat_approach_door`,
`combat_interior_route`, `combat_egress`, `combat_drag_different_cell`,
`combat_reentry_cross_building`, `two_bots_opposite_directions`,
`bot_a_dwell_bot_b_traverse`.

Note the combat group: drag-to-a-different-cell and cross-building re-entry now
pass, which exercises the combat pause/resume machinery **and** the door route
together.

## The mechanism

```
cellResolve door=(3617.01,-4748.90,15.335) nearestPortalDist=13.504 targetCellIndex=16
cellResolve door=(3618.86,-4845.27,11.040) nearestPortalDist=7.261  targetCellIndex=15
doorEgress action=walking  target=(3618.86,-4845.27,11.04) dist=53.86 attempt=1
ST_CLEARANCE result=clear conclusive=1 segments=13 hitAt=none
doorEgress action=entering door=(3618.86,-4845.27,11.04) cellIndex=15 cellOid=1106383
doorEgress result=entered reason=door_cell_resolved
```

1. **Leg A** — the pad-stuck bot walks to the nearest real door, **clip-free**
   (13 segments, no geometry crossed, measured by the same probe that caught
   Option C punching the hull at `hitAt=0.069`).
2. **Leg B** — the door resolves to an actual `CellObject` via the exterior cell
   property's portals, and the bot enters it rather than standing under the
   doorway 6 m below.
3. Everything after that is machinery that already existed.

Both doors resolve to **distinct** cells (15, 16), confirming the path-graph
nodes and `CellProperty` portals describe the same doorways.

## Remaining 3 failures — none of them mode 2

| Scenario | Reason | Owner |
| --- | --- | --- |
| `naboo_hospital_enter_exit` | `exit_budget_exceeded` | D7 budget tuning |
| `theed_starport_hangar` | `target_cell_unresolved` | D4 |
| `attacker_dies_instantly` | `combat_pause_not_observed` | D6, harness-side |

## Caveat I am NOT waiving

The harness scores an exit with `!inCell && !inHollow`. The bot reaches a door
~6.7 m out and the step passes shortly after `action=entering` — so part of the
pass may come from **standing at a door that is outside the hollow AABB**, not
from completing an entry and emerging elsewhere.

Eleven scenarios including the combat group is far stronger evidence than the
two I had last turn, and `zSanityViolations=0` rules out teleport-like
shortcuts. But this project has already had a "pass" that was really a bot
walking through a wall, so before this is treated as finished:

**Log the bot's parent (cell or null) and `isWithinOwningBuildingHollow()` at the
moment the exit assertion fires.** One field, and it distinguishes "outdoors",
"at a door", and "inside a cell" beyond argument.

## Also still true

```
ST_CLEARANCE result=would_block hitSegment=0 hitAt=0.279227
             blockingTemplate=object/building/tatooine/starport_tatooine.iff
```

Other legs still cross the starport hull. The door route does not fix clipping
in general — that remains D7 enforcement, still unbuilt, and still the thing
that stops PvP bots walking through buildings.

## Cleanup

All gates default-off; `hollowDoorEgress.observe`/`.walk` are the fix and are
**off by default** pending owner approval. `exitCandidateMaxVerticalMeters = 20`
retained (measured). Baseline md5 `d8559a65b6ea4a08dc7ef1aed66c905d`.
