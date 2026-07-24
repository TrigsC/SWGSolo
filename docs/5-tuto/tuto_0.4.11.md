# Tutorial 0.4.11 — Coordinate Spaces in Core3 Movement: World vs. Cell-Local (and Why Facing Is Parent-Relative)

**Audience**: advanced; Core3/engine3 internals (`AiAgentImplementation::findNextPosition`,
`WorldCoordinates`, `PatrolPoint`, `CellObject`, `PathFinderManager::transformToModelSpace`,
`SceneObject` parent graph, POB portal/floor navigation).

Almost every bug fixed in this release — the F_0.4.8 cell-entry teleport, the pivot-swivel,
and the cause-3 attempt we had to *revert* — is one mistake wearing different costumes:
**mixing two coordinate spaces in one subtraction.** This tutorial pins down the two spaces,
shows exactly where they leak, and states the rule that keeps engine movement code correct.

## 1. Two spaces, one graph

A `SceneObject`'s position is stored **relative to its parent**:

- **Outdoors**, the parent is the ground zone → the stored position *is* world coordinates.
- **Inside a building cell**, the parent is a `CellObject` → the stored position is
  **cell-local** (relative to the cell's origin, which is itself rotated + translated by the
  building's world placement).

`WorldCoordinates` is the bridge: it carries `(point, cell)` and can produce a world point on
demand via `getWorldPosition()`. A path returned by `PathFinderManager::findPath` is a list of
`WorldCoordinates` whose nodes switch space at the doorway — outdoor nodes carry `cell == null`
(world coords), interior nodes carry a cell (cell-local coords). You can see both in one route
in `bin/log/cellnav.log`:

```
ENGINE_PATH_NODE index=1 world=(3607.65,-4749.08,5.97) cell=0        local=(3607.65,-4749.08,5.97)
ENGINE_PATH_NODE index=2 world=(3607.65,-4749.08,5.97) cell=6595536  local=(52.22,10.34,0.97)
```

Same world point, two representations. Node 2's `local` is what `getPoint()` returns.

## 2. Where the leak happens

`AiAgentImplementation::findNextPosition` walks the path, consuming nodes up to `maxSpeed`.
The distance loop is *careful*: before comparing the agent to a node in a different cell it
re-expresses the agent in the node's space (`checkPos`, `AiAgentImplementation.cpp:4841-4854`,
via `transformToModelSpace`). So distances never mix spaces.

The **direction** calc, right below it, historically did not:

```cpp
float dx = nextMovementPosition.getX() - getPositionX();   // node space  -  agent parent space
float dy = nextMovementPosition.getY() - getPositionY();
```

On the single frame that crosses a portal, `nextMovementPosition` is already the *interior*
node (cell-local `~52`) while the agent is still outdoors (`getPositionX() ~3608`). The
subtraction yields a **~3556 m garbage vector** → a wild heading. Next frame the agent is in
the cell, both operands are cell-local again, and it snaps back. That snap-and-return *is* the
90/180° swivel players saw at doorways.

## 3. Two fixes that were right, one that was wrong

Fixes **1 and 2** (shipped) stay entirely inside one space, so they are always safe:

- **Zero-delta guard**: when `dx*dy` is ~0 the agent isn't advancing this frame, so `atan2(0,0)`
  is meaningless — hold the current facing (`AiAgentImplementation.cpp` ~:4995).
- **Wrap `+2*PI`, not `+PI`**: normalizing a negative post-conversion angle must add a full
  turn; `+PI` is a 180° flip. This now matches the proven patrol-side calc at `~:3894`
  (`M_PI + (M_PI + directionAngle)`).

The **reverted** attempt (cause-3) tried to fix the crossing by computing `dx/dy` from
`getWorldPosition()` on *both* operands. That genuinely killed the crossing garbage — but it
made in-cell facing **worse**, and the reason is the whole point of this tutorial:

> **A creature's `direction` (yaw) is stored relative to its parent, just like its position.**
> Inside a rotated building cell, the correct facing is *cell-local*. Deriving it from a
> world-space delta applies the building's rotation to the yaw, so the bot faces the wrong way
> in-cell.

So world-space was the wrong target space. The right operands are **both in the agent's current
parent space** — which, for same-cell frames, is exactly the original cell-local
`getX() - getPositionX()`. We reverted to that (verified-good) form and logged the crossing
frame as a known future item, because fixing *only* the one crossing frame safely means
transforming the node **into the agent's parent space** (not into world) — a targeted change we
chose not to rush.

## 4. The rule

When you write engine movement or facing math:

1. **Never subtract two positions without confirming they share a space.** If either operand
   can be cell-local, reconcile first (`transformToModelSpace` into the *same* parent, or both
   to world — but only if the result is consumed in world space).
2. **Facing follows position.** A yaw applied to a cell-parented object is interpreted
   cell-local; derive it in the parent's space, not world.
3. **The safe default is "stay in the space you're already in."** Fixes 1 and 2 did; they
   shipped. The world-space detour didn't; it reverted.

This is the same lesson ARCHI.md §12 already flagged from P.6.5b ("`findNextPosition` math
assumes one coordinate space per comparison") — now with three fresh scars to prove it.
