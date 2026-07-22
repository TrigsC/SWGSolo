# Tutorial 0.4.6 — Finding NPCs Inside Buildings: Spatial Queries vs. Delayed-Load Cells

**Audience**: advanced; Core3/engine3 internals (`getInRangeObjects`, the
`SceneObject` container graph, `BuildingObject`/`CellObject`, delayed container
loading, POB navigation).

This release wires PvE hunters to real Doctor/Musician/Dancer buffer NPCs. The
feature *looked* finished after code review and a clean build — yet the first
live run showed **every** provider resolving as `absent` and every hunter falling
back to synthetic buffs. The bug, and its fix, teach a Core3 internals principle
worth keeping: **a spatial `getInRangeObjects` query does not see objects that
live inside a building cell whose container has not been loaded yet.**

## 1. The symptom

`realBuffs.enabled=true`, providers spawned and working for a human player, but
the dashboard read:

```
providerResolveState: { tatooine:mos_eisley: "absent" }
doctorInteractions=0  musicianListens=0  dancerWatches=0  syntheticFallbacks=6
```

So `SimPlayerManager::resolvePveBuffProviders` ran, scanned, and found *nothing*
matching — even though the "Doctor Buffer" NPC is standing in the Mos Eisley med
center. It never reached the interaction code at all.

## 2. Two coordinate spaces, two containers

A ground zone is a quadtree of `SceneObject`s indexed by **world** position.
`Zone::getInRangeObjects(x, z, y, range, &out, ...)` walks that tree. But objects
*inside a building* are not siblings in the world tree — they are contained in a
`CellObject`, which is contained in a `BuildingObject`. The building sits in the
world tree; its cell contents do not. The query's `includeBuildingObjects` mode
descends into building cells to include their contents — **but only for cells
whose container is already loaded.**

Core3 loads cell containers lazily (delayed load) to avoid paging in every
interior on every zone. A static city building can therefore be present in the
world tree while `CellObject::isContainerLoaded()` is still false — and a
just-spawned screenplay NPC inside that unloaded cell is invisible to the scan.
The building was found; the doctor inside it was not.

## 3. The fix: force-load the provider cells, then rescan

In `SimPlayerManager::resolvePveBuffProviders`, before matching by name, we locate
the relevant building and load its cells:

```cpp
BuildingObject* building = candidate->asBuildingObject();
// ... filter by template path ("hospital" / "cantina") ...
for (int cellIndex = 1; cellIndex <= building->getMapCellSize(); ++cellIndex) {
    CellObject* cell = building->getCell(cellIndex);
    if (cell != nullptr && !cell->isContainerLoaded()) {
        cell->getContainerObjectsSize();   // touching size triggers the load
        loadedCell = true;
    }
}
if (loadedCell) {                          // rescan now that the interior exists
    objects.removeAll();
    zone->getInRangeObjects(anchor.getX(), 0, anchor.getY(),
        pveBuffProviderScanRadiusMeters, &objects, true, true);
}
```

Three things to notice, each a Core3 idiom:

- **Touching `getContainerObjectsSize()` forces the delayed load.** You do not call
  a "load()" — asking the container for its contents materializes it. After that,
  a fresh `getInRangeObjects` sees the NPCs.
- **The building is matched by its template path, not by guesswork** (`hospital`,
  `cantina`), so we only page in the interiors we actually need — not every
  building near the anchor.
- **The world query stays outside every manager/agent lock.** As with the
  `scanForTarget` and mission-terminal resolvers, the scan takes no `pveMutex`;
  provider fields are then snapshotted under the provider's own `Locker`. Loading
  a cell touches the container, not the SimPlayer state, so lock discipline is
  unchanged.

## 4. Why radius mattered too

The med-center/cantina *anchor* comes from `getPveHomeLocations` and is an
**outdoor** point beside the building; the NPC is at an interior cell position
whose world coordinates can be well over 40 m away. So the two-part fix is: load
the cell **and** widen `providerScanRadiusMeters` (40 → 400) so the outdoor anchor
actually reaches the interior world position. Either alone is insufficient.

## 5. The reachability corollary (POB, not Recast)

Finding the NPC is separate from *walking to it*. Cities are navmeshed with
Recast, but building interiors are navigated by **POB portal/floor graphs**, a
different subsystem. This release's leg-scoped `interiorApproachLeg` latch routes
the buff-approach leg through the base cell-aware path (world position for
distance, cell-local position + `CellObject` for the path request) rather than the
hunter's outdoor overland hybrid mover. There was no static navmesh blocker — but
the lesson pairs with §2: **cell containment shows up twice — once in what a
spatial query can *see*, and once in how movement must *path*.**

## Takeaway

When code that reads correctly still finds "nothing" in a live world, suspect the
**container graph**, not your predicate. `getInRangeObjects` indexes the world
tree; interior objects hide behind unloaded `CellObject` containers. Force the load
(`getContainerObjectsSize`), rescan, and keep the query lock-free — then match.
