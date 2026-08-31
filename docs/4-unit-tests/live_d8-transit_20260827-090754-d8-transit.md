# Live Verification — multi-hop transit scenario (pad -> cell -> pad)

**The transit scenario PASSES.** Matrix 11 PASS / 15 FAIL of 26 (the new
scenario passed; nothing else moved from the 10/15-of-25 baseline).

- **Run ID**: `20260827-090754-d8-transit` — evidence `bin/log/trip-verify-20260827-090754-d8-transit.log`
- Restart clean, 0 players, ready in 15s. 26 scenarios armed, transit present.
- Config identical to run 6 (`suppressLeash` off), plus the new scenario.

## Result

```
starport_transit_terminal_to_collector   PASS   84074 ms
   enter    PASS  54031 ms   mid-pad -> starport cell 1106383 (ticket TERMINAL)
   dwell    PASS   5003 ms   buying (cosmetic)
   moveTo   PASS  24516 ms   cell 1106383 -> back to the pad (ticket COLLECTOR)
```

Log evidence that it is genuine, not vacuous:
```
t=885341  ST_PHASE Idle -> ApproachDoor building=1106368 cell=1106383 reason=outdoor_enter
t=885342  ST_PATH request botPos=(3575,-4813,5) -> cell=1106383
t=885449  ST_PATH accepted nodes=47
t=895539  ST_PHASE ApproachDoor -> InteriorRoute reason=entered_structure
t=939204  ST_PHASE to=Idle reason=target_cell_arrived        <- genuinely inside
          ... dwell ...
t=969391  moveTo PASS -- ended within 6m of (3575,-4813,5), i.e. on the pad
```

## Why this matters

**The multi-hop travel primitive the owner described works today.** A bot can
arrive in the hollow, walk into the starport cell to the ticket terminal, and
walk back out to the hollow where the ticket collector initiates travel. The
Lok -> Tatooine -> Corellia transit shape is movement-complete.

It also confirms the correction to my earlier recommendation: forcing hollow
egress to pick an `inHollow == 0` exit would have BROKEN this scenario, marching
a boarding bot out the far side of the building. See
[[starport-hollow-is-a-destination]].

And it narrows the remaining D2 blocker precisely: the failure is NOT "the bot
cannot leave the starport". Cell -> hollow egress works (24.5s). The failure is
only "the bot cannot reach a destination on the FAR SIDE of the building",
because the interior path graph from the pad-side cells does not reach the west
exterior doors and base pathfinding returns a valid short path out the nearest
portal instead (so the repair ladder, which only engages on path FAILURE, never
runs).

## Remaining failures (unchanged)

13 `controller_path_failed`, 1 `target_cell_unresolved` (D4, theed),
1 `combat_pause_not_observed` (D6, harness-side).

## Cleanup

Gates restored to default-off; the transit scenario is retained in the committed
config (it is inert while `structureTraversalTest.enabled = false`). The
scratchpad clean baseline was refreshed so future restores keep the scenario.
