# Measurement — where does the starport actually lead?

**Answer: there ARE exits outside the hollow. The exit set has been looking at
the wrong half of the building.**

```
ST_HOLLOW buildingExits building=1106368 cells=17 worldPortals=6 outsideHollow=4
```

- **Run ID**: `20260825-194345-building-exits` (observe only; `walk` off)

## Every world portal in the Mos Eisley starport

Bot on the pad at `(3575.89, -4813.35)`.

| cell -> target | doorway | inHollow | dist from bot |
| --- | --- | --- | --- |
| 0 -> 1 | `(3541.39, -4802.76, 5.63)` | **0** | **36.1** |
| 1 -> 0 | `(3541.39, -4802.76, 5.63)` | **0** | **36.1** |
| 2 -> 1 | `(3539.62, -4814.24, 5.56)` | **0** | **36.3** |
| 3 -> 1 | `(3539.18, -4791.39, 5.56)` | **0** | **42.8** |
| 15 -> 0 | `(3613.80, -4845.38, 5.84)` | 1 | 49.6 |
| 16 -> 0 | `(3607.66, -4749.08, 5.59)` | 1 | 71.7 |

The building spans x ~3539 to ~3618. **The hollow (pad) is the EAST side; the
four exits to the open world are on the WEST face at x ~3540** — and they are
*closer to the bot* (36 m) than the pad doors the exit set currently chooses
(49.6 m and 71.7 m).

This is the owner's spur, measured: enter the band from the pad side (cells
15/16), traverse the interior, leave at the far face (cells 1/2/3).

## Why the exit set never saw them

```
exitSet=graph building=1106368 cellNumber=0 exteriorNodes=16 globalNodes=3 entrancesRaw=0 doorNodes=3
doorEgress result=found doors=2 nearestDist=53.8574
```

`buildCellEgressExitSet` reads **the exterior floor mesh's path graph**, whose
`CellPortal` nodes are all at x ~3617 — the pad side only. The west doors belong
to cells 1/2/3 and appear only in the **cell properties' portal lists**, which is
what this measurement enumerates.

So the door-finder is structurally incapable of seeing the real exits. Walking to
its best candidate was always going to leave the bot in the hollow, exactly as
the corrected assertion showed.

## What to build next

Select hollow-egress candidates from **`CellProperty` world portals across the
whole building**, not from the exterior path graph, and **prefer
`inHollow == 0`**. On this building that yields a target 36 m away on the west
face instead of 49.6 m on the pad.

The route then becomes pad -> enter cell 15/16 -> interior -> exit via cell 1/2/3,
and the interior half is already Retry 6's job — built and live-verified.

Two cautions carried forward:
- `inHollow` must be evaluated per candidate, not assumed from distance; the
  nearest door is not the useful one here, which is precisely how the previous
  attempt went wrong.
- Any new pass must be judged by the corrected `ST_HARNESS exitAssert`
  (`inHollowOfScenarioBuilding` against the scenario's building), never by the
  old vacuous predicate.

## Cleanup

All gates restored to default-off. Evidence at
`bin/log/trip-verify-20260825-194345-building-exits-structuretraversal.log`.
