# AI Cell-Entry Navigation

## Problem

SimPlayer NPCs can reach a building doorway but fail while crossing from the
outdoor world into a POB interior cell. The observed result is a snap or
teleport into a cell corner or wall, followed by a stuck bot. The issue has
appeared at Theed starport and at the Mos Eisley cantina during PvE buff
approaches.

This is cross-cutting behavior shared by miners, PvE hunters, and PvP bots. It
does not belong in the miner-only navigation design document.

## Suspected chain

An interior movement request carries a world-space target for distance checks
and a model-local target plus `CellObject` for pathfinding. The controller
queues `WorldCoordinates` path nodes, then `AiAgentImplementation` consumes
those nodes and transforms the current position when the parent cell changes.
`updateCurrentPosition` applies the resulting local/world position and updates
the zone parent. A bad cell on a path node, a world/local transform applied in
the wrong space, or an incorrect floor/interpolation value can therefore cause
the doorway snap.

## Pass 1 instrumentation

Pass 1 is diagnostic only and is default-off behind
`SimPlayerManagerConfig.cellNavDiag.enabled`. When enabled, one tagged bot is
spawned outside the Mos Eisley cantina and uses the same no-op `simMiner` tree
and cell-aware controller movement pipeline as production bots. The dedicated
one-shot controller issues one protected `moveToInterior` request, then stops
on arrival or failure.

The trace is appended to `bin/log/cellnav.log` and includes:

- model-space target resolution, every nearby building/cell floor test, and the
  selected cell/floor;
- controller requests, accepted/rejected path results, complete path nodes,
  queued patrol nodes, arrival ticks, parent-cell changes, and terminal state;
- engine-mover path results, node consumption, world↔cell transforms, applied
  `setPosition` arguments, and the resulting parent/world/local position.

The engine-mover trace is restricted to the single tagged bot. Resolution
failure retries on the existing controller for at most ten delayed attempts,
then emits `CELLNAV_RESOLVE_FAILED` without issuing a raw-world fallback move.

## Findings (Pass 1 run)

_To be filled after the owner enables `cellNavDiag`, restarts the server, and
reviews `bin/log/cellnav.log`._

- Run date:
- Bot OID:
- Doorway crossing result:
- First suspicious node/transform:
- Applied position and parent-cell outcome:
- Pass 2 hypothesis:

---

## Future design item: portal-aware combat pursuit (owner decision 2026-07-23)

**Context.** Once ticket-collector travel (F_0.4.11) began bringing PvP squads
into starport hollows to board, opposing squads started meeting there. Observed:
a squad *outside* the starport engaged an enemy squad standing in the enclosed
hollow and clipped **through the exterior wall** to attack.

**Mechanism.** The PvP targeting scan (`SimPvPController.cpp` ~:250) skips a
target whose `getParent() != nullptr` (i.e. inside a building cell) — "hunt
outdoors, never chase into cells." But the enclosed starport **hollow is cell 0
(parent == null)** — geometrically "outdoors" yet reachable only through the
door. So an outside squad treats a hollow target as directly reachable, engages,
and the combat mover drives it straight through the wall.

**Owner's intended end-state (NOT a LoS engagement block).** LoS should gate the
**attack** only — bots may only land an attack with line of sight, exactly like a
player. Detection and pursuit are *not* blocked: a bot that sees a hostile appear
should **path (portal-aware) to converge** on it — routing through the door into
the hollow/cell if that is where the enemy is. If the convergence point ends up
inside a cell, that is simply where the fight happens; when it ends, both squads
resume their patrol/travel loop.

**Required capability (future).** Portal-aware combat movement: when the combat
target is separated from the pursuer by a POB boundary (different cell, or an
enclosed cell-0 hollow), drive pursuit with a portal-following `findPath` (the
same routing the travel legs use) instead of the direct/overland combat mover;
gate the actual attack on `CollisionManager::checkLineOfSight`. This is a larger,
combat-subsystem change and is deliberately **deferred** — logged here, not yet
implemented. The current direct-mover clip is a known cosmetic artifact until then.
