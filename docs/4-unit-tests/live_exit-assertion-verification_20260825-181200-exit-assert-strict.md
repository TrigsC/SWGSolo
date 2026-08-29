# Verification — was the 22-PASS result genuine?

**No. RETRACTED. The exit assertion was vacuous, and mode 2 is NOT solved.**

Strict re-run: **11 PASS / 14 FAIL** — identical to the pre-Leg-B baseline. The
previous run's 22 PASS / 3 FAIL was an artifact.

- **Run ID**: `20260825-181200-exit-assert-strict`

## The defect in the assertion

```cpp
bool isWithinOwningBuildingHollow() const {
    if (agent == nullptr || structureTraversalIntent.owningBuildingOid == 0 || ...)
        return false;      // "not in hollow" -- because there is nothing to test
```

`isHarnessOutdoorsClear()` is `!inCell && !inHollow`. When Leg B completed the
traversal the intent was cleared, `owningBuildingOid` became 0, the hollow half
could no longer fire, and the assertion degenerated to `!inCell` — which the bot
satisfied by standing outside a cell on the pad.

**The assertion was measuring "did the traversal declare itself finished",
not "is the bot outside."** Circular, and it flipped exactly eleven scenarios at
once — a signal that should have increased suspicion rather than confidence.

## The corrected assertion, and the real answer

The building is now resolved from the **scenario config**, walking back through
the steps to the most recent one naming a building (an `exit` step carries none
of its own — the first correction attempt was equally vacuous at
`scenarioBuilding=0`).

```
mos_eisley_starport_front       pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1
mos_eisley_starport_deep_foyer4 pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1
starport_upper_floor            pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1
cell_to_enclosed_hollow         pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1
combat_approach_door            pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1
combat_interior_route           pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1
combat_egress                   pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1
combat_drag_different_cell      pass=0 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=1

cantina_enter_exit              pass=1 scenarioBuilding=1082874 inCell=0 inHollowOfScenarioBuilding=0
cantina_to_corellia_hospital    pass=1 scenarioBuilding=8105493 inCell=0 inHollowOfScenarioBuilding=0
combat_ends_outdoors            pass=1 scenarioBuilding=1106368 inCell=0 inHollowOfScenarioBuilding=0
```

Every starport failure is `inCell=0, inHollow=1` — **out of the cell, still on
the pad.** Every cantina pass is genuinely clear. `combat_ends_outdoors` passes
against the *same* starport building, which confirms the check discriminates
rather than blanket-failing.

## What I got wrong, specifically

D1 measured `nodes examined=3 rejectedHollow=3` on 2026-08-17. I treated that
rejection as a bug to invert. It was reporting something true: **the starport's
doors lie INSIDE the hollow region** (`hollowMissDistance=0` at the door). So
walking to a door cannot, by itself, leave the hollow — and Leg B's premise was
wrong even though its mechanism worked.

## What survives

Leg A is independently measured and still real:
- 53.9 m pad crossing to a genuine door, **clip-free**
  (`ST_CLEARANCE result=clear conclusive=1 segments=13`).
- Both doors resolve to distinct real cells (15, 16).
- `nearestDist` 53.86 -> 6.67.

Getting a bot to a door without clipping is solved. Getting it *out* is not.

## Net effect of this slice

The harness now measures what it claims to measure. That is the durable gain:
no future attempt can pass by clearing its own state.

Anomalies all 0, no regressions against the true baseline, miners unaffected.
All gates restored to default-off.
