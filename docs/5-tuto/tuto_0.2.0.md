# Tutorial 0.2.0 — Coordinate Spaces in Core3 Movement: Why "Just Pass the Cell" Is Never Just That

*Level: Advanced · Focus: engine architecture (WorldCoordinates, PatrolPoint,
findNextPosition) · Grounded in the P.6.5b diff.*

## The core principle

Core3 represents an indoor position **relative to its cell**, not the world.
`WorldCoordinates` (engine3, `server/zone/objects/scene/WorldCoordinates.h`)
is the type that makes this explicit: it pairs a `Vector3` with an optional
`CellObject*`, and the same `Vector3` means two entirely different points
depending on that cell. `getPoint()` returns the raw (possibly cell-local)
vector; `getWorldPosition()` applies the building's transform to give you a
world-space point. Buildings are rotated and translated relative to the
terrain, so the two differ by an affine transform — not just an offset.

Every subsystem that computes a *distance*, *direction*, or *interpolation*
between two positions is therefore making an implicit claim: **both operands
are in the same space**. P.6.5b was, at its heart, an exercise in auditing
that claim along the entire movement pipeline the first time an in-cell
target ever entered it.

## What the pipeline looked like before

SimPlayer movement flows: `SimPlayerController::moveTo(Vector3)` →
`findPath` → `simPath` (a `Vector<WorldCoordinates>`) →
`queuePatrolPointsFromSimPath()` → `AiAgent` patrol queue →
`AiAgentImplementation::findNextPosition()` walks the cached path each tick.

Two load-bearing details were world-only by construction:

1. `queuePatrolPointsFromSimPath` built every patrol point as
   `PatrolPoint pp(p.getX(), p.getZ(), p.getY(), nullptr)` — the node's cell
   was **discarded** (SimPlayerController.cpp, pre-diff :342). Harmless while
   every SimPlayer path was outdoor, because a cell-less node's `getPoint()`
   *is* its world position.
2. `findNextPosition`'s multi-node consumption loop compared
   `checkPos - nextMovementPosition.getWorldPosition()` where `checkPos`
   could be cell-local (it ran `transformToModelSpace` on cell transitions).
   Also latent: with no cells in the path, both sides degenerated to world
   space and the mixing never fired.

This is the archetypal *latent invariant*: code that is only correct because
one input class never occurs — until a feature makes it occur.

## What P.6.5b changed, and the pattern to copy

The fix threads **both coordinate forms in parallel**, and picks per use:

- The boarding-point cache (`SimPlayerManager::PvpBoardingPoint`) stores
  `worldPos` (for distance/arrival math), `localPos` (for the path request),
  and `cellOid`. Never one position doing double duty.
- `moveTo(worldPos, localPos, targetCell)` keeps `destination` (world) for
  `checkArrival`/`acceptFoundPath` comparisons while handing
  `WorldCoordinates(localPos, targetCell)` to the path-finder — that is the
  form `findPathFromWorldToCell` expects.
- `queuePatrolPointsFromSimPath` now passes each node's own cell into the
  `PatrolPoint` and uses `getWorldPosition()` only for the world-side
  proximity check. For a null-cell node both changes are identities — which
  is exactly why miners are provably unaffected.
- `findNextPosition` now converts the current position **into the next
  node's space** before subtracting (same parent → no transform; different
  parent → through world space, then `transformToModelSpace` if the node is
  in a cell). The interpolation inherits consistency because it reuses
  `checkPos`, and the terrain Z-snap (`getWorldZ`) is gated to world-space
  nodes only — snapping a cell-local Y/X pair to terrain height would
  teleport the agent's Z to the ground *outside* the building.

The transferable rule: **when a position crosses a subsystem boundary, carry
its cell with it or convert at the boundary — never store a converted value
in a variable whose other assignments are unconverted.** The bug lived
precisely in a variable (`checkPos`) whose space depended on which branch
assigned it.

## Why the review process mattered here

The plan (correctly) demanded dual coordinate forms, but the *first*
implementation only fixed the producer side (`SimPlayerController`); the
consumer (`findNextPosition`) still mixed spaces — my own self-review missed
it because the consumer lives two files away in engine-adjacent code. The
Codex review caught it by tracing the patrol point from producer to
consumer. Lesson for this codebase (rhymes with P.6.1d's
`currentFoundPath`): **the AiAgent movement stack has its own private copies
and its own math — any change to what enters the patrol queue requires
reading `findNextPosition` end-to-end, every time.**

## Where to look in the diff

- `SimPlayerController.cpp` — `moveTo` overload, `onPathFound` world/local
  split, `queueMorePathNodes` cell propagation.
- `AiAgentImplementation.cpp:4767-4830` — node-space `checkPos`, world-Z
  snap guard.
- `SimPlayerManager.cpp` — `resolvePvpBoardingPoint` (dual-form cache),
  `onPvpSquadDepartureIntent` (dual-form hand-off).
- `SimPvPController.cpp` — `enterToShuttle` (cell OID resolved fresh at
  drive time, never held across phases; cleared in `prepareForRelocation`).
