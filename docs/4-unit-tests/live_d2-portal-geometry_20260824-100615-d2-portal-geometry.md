# Live Verification — D2 Retry 5 (`portal_geometry`)

**Verdict: `LIVE_VERIFICATION_FAIL` for the fix. `SCOPE ESTABLISHED` for the
defect.** No receipt.

**The bot is not next to a door. It is in an interior room with no exit at all.**
Its cell has five doorways and every one leads to another interior cell. Retry 5
can only find a door in the bot's own cell, so it correctly does nothing here.

- **Run ID**: `20260824-100615-d2-portal-geometry`
- **Change**: Retry 5 `repair=portal_geometry`, Codex APPROVED after 2 rounds
- **Building**: `1697358` cell `1697364` (Naboo med centre), template-level

## The measurement

```
portal index=0 targetCell=7 geometryIndex=15 doorModel=not_outside floorResult=1 reachable=0
portal index=1 targetCell=5 geometryIndex=13 doorModel=not_outside floorResult=1 reachable=0
portal index=2 targetCell=7 geometryIndex=16 doorModel=not_outside floorResult=1 reachable=0
portal index=3 targetCell=5 geometryIndex=14 doorModel=not_outside floorResult=1 reachable=0
portal index=4 targetCell=4 geometryIndex=10 doorModel=not_outside floorResult=1 reachable=0
result=failed reason=no_outside_portal portalsExamined=5 outsidePortals=0
```

Five portals, targets **7, 5, 7, 5, 4** — all interior. `outsidePortals=0`.

**This corrects my own earlier explanation.** I described the bot as standing
3 m from its own front door refusing to leave. That 3 m door belongs to the
BUILDING — D7's exit set reads it off the *exterior* floor mesh — not to the room
the bot occupies. The bot is several rooms deep.

## Assertions

| # | Assertion | Actual | |
| --- | --- | --- | --- |
| Retry 5 repairs D2 | med-centre scenarios pass | still `controller_path_failed` | **FAIL** |
| No regression | 10 PASS / 15 FAIL, same 10 | unchanged, 0 regressions | PASS |
| No new anomalies | z-sanity / teleports 0 | **0 / 0** | PASS |
| Telemetry is decisive | scope identifiable in one run | **yes — `outsidePortals=0`** | PASS |
| No crash | gdb.log unchanged | 876 lines, alive 1h10m | PASS |

`egressPathFailures` 6 and `pathfinderFallbackActivations` 6 are unchanged from
the previous run, so Retry 5 added no new failure paths.

## What D2 actually requires

A **multi-cell geometry route**: room → adjacent cell → … → a cell with a world
portal → outside. Every input exists:

- portal adjacency is in the data (`targetCell=4,5,7`);
- doorway geometry is intact for every portal, and the model→cell-local
  conversion is now proven correct and logged in both frames;
- floor-surface routing inside a cell works (measured repeatedly);
- **D7's exit set already performs exactly this BFS** — its
  `cellsVisited=15 cellsWithWorldPortal=7` line walks `getConnectedCells()`
  looking for cells with `hasWorldPortal()`.

Seven of this building's seventeen cells open to the outside. The bot's is not
one of them, but cells 4, 5 or 7 may be. The traversal already exists in D7; it
has never been wired into the pathfinder.

The work is chaining several in-cell floor routes through successive doorways
instead of one, which is materially larger than Retry 5 and should be scoped
deliberately rather than bolted on.

## Retry 5 is kept

It is correct, bounded, default-off, costs nothing when `outsidePortals=0`, and
its telemetry produced this scope finding. It will also be the terminal step of
the multi-cell route once that exists — the last hop is exactly this: find the
outside portal in the current cell and walk to it.

## Cleanup

Config restored to all gates off, tunable retained (md5
`7a054f01b6f7c148b3cc65492469d39e`). Sampler stopped. Evidence at
`bin/log/trip-verify-20260824-100615-d2-portal-geometry-structuretraversal.log`.
