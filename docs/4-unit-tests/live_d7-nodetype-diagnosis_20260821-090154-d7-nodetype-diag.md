# Diagnosis — why D7 Part 2's `getEntrances()` returns nothing

**Result: `OBSERVABILITY_GAP` CLOSED. Root cause identified and measured.**

**A building's exterior path graph contains ZERO `BuildingEntrance` nodes. Its
doors are typed `CellPortal`.** `PathGraph::getEntrances()` filters on
`getType() == PathNode::BuildingEntrance` (type 3), so it can only ever return
an empty vector for a per-building graph. The exit set was asking the right
object the wrong question.

- **Run ID**: `20260821-090154-d7-nodetype-diag`
- **Slice**: diagnostics only, Codex APPROVED, no behaviour change
- **Config**: temporary override, restored to md5 `bf7b26d9a27a85ce7f381c828bc83949`

## Evidence

```
ST_EGRESS exitSet=graph building=1697358 cellNumber=6  exteriorNodes=17 globalNodes=5 entrancesRaw=0
ST_EGRESS exitSet=nodeTypes CellPortal=5 CellWaypoint=12
ST_EGRESS exitSet=build candidates=0 worldPortalCorroborated=true entrancesRaw=0
          rejectedBounds=0 rejectedDuplicate=0 cellsVisited=15 cellsWithWorldPortal=7

ST_EGRESS exitSet=graph building=4005516 cellNumber=13 exteriorNodes=16 globalNodes=3 entrancesRaw=0
ST_EGRESS exitSet=nodeTypes CellPortal=3 CellWaypoint=13
ST_EGRESS exitSet=build candidates=0 worldPortalCorroborated=true entrancesRaw=0
          rejectedBounds=0 rejectedDuplicate=0 cellsVisited=17 cellsWithWorldPortal=6
```

Totals across builds: **`CellPortal=8`, `CellWaypoint=25`, `BuildingEntrance=0`.**

## Four facts, each independently useful

1. **No `BuildingEntrance` nodes exist.** Only `CellPortal` (0) and
   `CellWaypoint` (1). Type 3 is evidently a CITY-level path-graph concept, not
   a per-building one — the original hypothesis, now measured rather than assumed.
2. **`globalNodes` == `CellPortal` count in both samples** (5==5, 3==3). The
   global nodes *are* the doors. This also reconciles D1 run 4, which found
   exactly **3** nodes on this class of building via `findNearestGlobalNode`.
3. **Nothing was filtered.** `rejectedBounds=0`, `rejectedDuplicate=0`. The
   candidate list was not culled to empty — it was never populated. The
   transform and the bounds check are exonerated; they were never reached.
4. **The `worldPortal` BFS works.** `cellsVisited=15/17`,
   `cellsWithWorldPortal=7/6`. `CellProperty::hasWorldPortal()` — the flag with
   no consumers anywhere in the codebase — correctly identifies which cells open
   to the outside.

## The fix this implies (NOT implemented)

Select exterior-graph nodes by **`getType() == PathNode::CellPortal`**, or
equivalently by `getGlobalGraphNodeID() != -1`, instead of calling
`getEntrances()`. Both selectors returned the identical count on both samples,
so either works; the global-node form has the advantage of matching what stock
`findPathFromCellToWorld` already uses to pick its exterior node.

Two cautions for whoever implements it:

- This yields doors **of the building**, which for a starport are inside the
  walled landing pad (D1 run 4: `nodes examined=3 rejectedHollow=3`). It fixes
  **mode 1** (stuck in a cell) and does nothing for **mode 2** (stuck in an
  enclosed hollow). Those remain different problems, as the D7 proposal says.
- `getEntrances()` should not simply be replaced everywhere — it is stock code
  and may be correct for city graphs. Only D7's exit-set selection is wrong.

## Cleanup

Config restored to all-off (`structureTraversal`, `structureTraversalTest`,
`zeroClip`, `exitSetEnabled`, `enforce` all false). Evidence at
`bin/log/trip-verify-20260821-090154-d7-nodetype-diag-structuretraversal.log`. No databases, rosters or
unrelated logs touched.
