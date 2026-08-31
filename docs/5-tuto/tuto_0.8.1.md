# Tutorial 0.8.1 — Shipping machinery is not the same as adopting it

F_0.8.0 shipped a structure-traversal state machine: phases, generations, hollow
escalation, far-side egress, a 26-scenario matrix. It was reviewed, verified,
tagged and released.

Then someone asked how to make every bot type use it, and one `grep` answered the
whole question:

```bash
$ grep -rn 'enterStructure(' --include=*.cpp | grep -v '^.*SimPlayerController::'
SimPlayerController.cpp:5285   SimTraversalTestController::issueResolvedStep
```

One caller. The **test harness**. Every production bot — miners, hunters, PvP
leaders, PvP members — reached buildings through `moveTo` or `moveToInterior`,
neither of which creates a `StructureTraversalIntent`. The foundation was
exercised only by the thing built to test it.

That is the principle of this release. **A feature is not adopted until something
that isn't a test calls it**, and that is a separate engineering step with its own
risk, not a footnote to the commit that built the machinery.

The check is cheap. Run it on your own subsystems.

## Trap 1 — the degradation path is part of the interface

`enterStructure` is written to be safe when the gate is off:

```cpp
void SimPlayerController::enterStructure(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell) {
    if (!isStructureTraversalFeatureEnabled()) {
        moveToInterior(worldPos, localPos, targetCell);   // (a)
        return;
    }
    ...
    structureTraversalIntent.active = targetCell != nullptr;
    if (!structureTraversalIntent.active) {
        moveTo(worldPos, localPos, targetCell);           // (b)
        return;
    }
```

Two different fallbacks — `(a)` `moveToInterior`, `(b)` `moveTo`. They are not
interchangeable: `moveToInterior` sets `interiorApproachLeg`.

So "inert when gated off" depends entirely on **what the site called before**:

| legacy call at the site | guard needed | why |
| --- | --- | --- |
| `moveToInterior` (stage 3 sites) | cell guard only | path `(a)` is byte-identical to the legacy call |
| `moveTo` (stage 2 collector sites) | cell guard **and** feature-gate guard | otherwise gate-off silently routes through `(a)` and gains `interiorApproachLeg` |

Hence the asymmetry in the diff — `SimPvPController.cpp:1312`:

```cpp
bool collectorTraversalEntry = collectorCell != nullptr &&
    isStructureTraversalFeatureEnabled();
```

versus `SimPvPController.cpp:1296`, which needs only `interiorCell != nullptr`.
Identical-looking migrations, different guards, for a reason you can only see by
reading the callee's fallthrough.

## Trap 2 — `virtual` lets a subclass delete your invariants silently

`SimPvPController` overrode `acceptFoundPath` wholesale and never chained to the
base. PvP leaders were therefore opted out of the P.6.1b stale-path defense and
the D2b far-side-egress check — not by decision, by omission. Nothing warned.

The fix is the template method pattern, and the point is the **non-virtual**
keyword:

```cpp
// SimPlayerController.h
bool acceptFoundPath(const Vector3& pathEnd);            // NOT virtual
protected:
    bool acceptFoundPathInvariants(const Vector3& pathEnd);
    virtual bool acceptFoundPathHook(const Vector3& pathEnd);
```

```cpp
bool SimPlayerController::acceptFoundPath(const Vector3& pathEnd) {
    if (!acceptFoundPathInvariants(pathEnd))
        return false;
    return acceptFoundPathHook(pathEnd);
}
```

A subclass can now only extend. Trying to skip the invariants is a **compile
error**, not a silent behavioural hole. When a base-class method carries
invariants that must always run, `virtual` is the wrong tool — split it.

## Trap 3 — a pre-lock guard needs a post-lock recheck

This one came out of a review finding about a *test counter* and turned out to be
a production bug. `SimPlayerController::checkArrival`:

```cpp
void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->getZone() == nullptr) return;   // :4419
    ...
    Locker locker(agent);                                          // :4446
    // ... refills patrol points, calls agent->findNextPosition()
```

Between line 4419 and line 4446 the task holds no lock. Another thread can take
the agent lock and run teardown — `destroyStructureTraversalTestBot` does, and so
does recovery despawn, both calling `destroyObjectFromWorld` under that same lock.
Our task then acquires the lock it was waiting on and proceeds to drive movement
on a **world-destroyed agent**.

```cpp
    Locker locker(agent);

    if (agent->getZone() == nullptr)   // recheck the SAME predicate
        return;
```

The general rule for this codebase: **every pre-lock lifecycle guard needs a
matching post-lock recheck**, because `Locker` is a blocking acquire and the world
can change underneath you while you wait. Return without rescheduling — a zoneless
agent has no work chain to continue.

## Trap 4 — a test oracle must never assert on a global counter

The matrix decided `movement_anomaly` like this:

```cpp
} else if (structureTraversalTeleportAnomalies.get() >
              structureTraversalTestStepTeleportBaseline[botIndex] ||
           structureTraversalZSanityViolations.get() >
              structureTraversalTestStepZSanityBaseline[botIndex]) {
```

Those are **process-wide** `AtomicInteger`s feeding the dashboard. Any bot
anywhere tripping an anomaly during a harness step failed that step. This was
correct only by accident: before F_0.8.1, no production bot ran traversal, so
nothing else could touch the counter. The migration made a latent bug real, and a
PvP bot on a Theed ramp failed an unrelated Mos Eisley scenario.

Two lessons, in order of how much they cost to learn:

1. **Separate aggregate telemetry from oracle telemetry.** The globals stay for
   the dashboard; the harness reads per-agent tallies on `SimPlayerController`.
2. **Fix every instance, not the one you were shown.** There were *two* such
   assertions — per-step and per-scenario. Stage 2 fixed the per-step one. Stage 3
   discovered the per-scenario one the hard way, when
   `mos_eisley_starport_deep_foyer4` failed with **both of its steps passing**.

The per-agent counters are `std::atomic<uint64>` with relaxed ordering, because
the oracle reads them from the runner task while `recordTraversalMovementStep`
writes them from the movement task. Relaxed is right here: they are monotonic
tallies and the oracle needs a coherent value, not a happens-before edge. The
final snapshot at teardown additionally takes the agent lock — matching the
writer's choreography — so no increment can be in flight when the value is folded
into the harness slot.

## The measurement that closes the loop

If adoption is the deliverable, the assertion must prove adoption happened —
otherwise it passes vacuously the same way the original code did.

`StructureTraversalDiagLog` keys every `ST_PHASE` line by agent OID, so the check
is direct: a migrated bot emits phase traces where it previously emitted nothing.
The releases read 0 → 17 → 84 distinct agents across the three stages.

Better still, the stage 2 evidence was checked by *attribution* rather than by
count: the six agents that logged `collectorCellOid=1692104` were exactly the six
that emitted `same_building_enter → InteriorRoute → target_cell_arrived`.
One-to-one, no gaps either direction. A count can be satisfied by coincidence; a
bijection cannot.
