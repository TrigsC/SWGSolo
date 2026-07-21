# Tutorial 0.4.5 — Single-Writer Ownership Across Independently-Scheduled Tick Loops

**Audience**: advanced; Core3/engine3 internals (task scheduling, `Locker`/`ManagedReference`, `@preLocked`, the SimPlayer controller pattern).

This release fixed two PvE-hunter combat bugs, but the most transferable lesson is
not either bug — it is a concurrency principle the code review surfaced: **when two
independently-scheduled loops can mutate the same object's state, pick one writer, or
you have a data race.** SWGSolo's #1 stability rule is "match existing lock
choreography exactly"; this is the same rule applied to *which task* is allowed to
write a field, not just *which lock* guards it.

## 1. The two loops

`SimHunterController` is driven by two separate `Task` subclasses, each of which
re-schedules itself and dispatches its real work as a lambda onto the shared
`TaskManager` thread pool:

- `SimHunterActiveTickTask::run()` → `runActiveTick()` (the "active tick", ~2s cadence).
- `ArrivalCheckTask::run()` (in `SimPlayerController.cpp`) → `checkArrival()` → `onTick()`
  (the "arrival cadence", faster, tied to movement).

Both hand off via `Core::getTaskManager()->executeTask([...], "name")`. Nothing
serializes them against each other, so the two lambdas can run **on different worker
threads at the same time**. That is the trap: they look like "the same controller's
loop," but they are two concurrent execution contexts.

## 2. How the bug fix created a race

The Bug 2 fix made `onTick` and `runActiveTick` share a dispatcher that promotes the
actual attacker to the mission target — which mutates `targetOid`, drops/registers the
kill observer, and (via `selectTarget`) synchronizes the manager. Suddenly *both* loops
were writing `targetOid`/observer state with no common lock. That is a classic data
race: torn reads, orphaned observers, controller/manager target divergence.

The tempting fix is a mutex around the dispatch. But a mutex only serializes the two
`engageActiveAttacker` calls; `runActiveTick` *also* mutates `targetOid` through
`checkMissionSocialAggro → beginRetreat` and the death/timeout `disengageTarget` paths.
A mutex that truly closed the race would have to wrap most of `runActiveTick` — broad,
lock-held-across-manager-calls, exactly the kind of novel choreography this project
avoids.

## 3. The chosen fix: restore the single-writer invariant

The pre-existing design already had one: before this feature, only `runActiveTick`
wrote `targetOid`/observer among the tick loops; `onTick` did agent-locked `startCombat`
only. The fix restores that invariant instead of adding a lock:

```cpp
// SimHunterController::onTick()
if (phase == HUNTING)
    return;                       // runActiveTick owns HUNTING combat

// travel legs only: interceptor-only self-defense, never selectTarget()
ManagedReference<CreatureObject*> attacker;
ManagedReference<AiAgent*> attackerAgent;
if (selectActiveCombatAttacker(strongAgent, attacker, attackerAgent))
    defendAgainstInterceptor(strongAgent, attacker.get());
else
    resetInterceptorCombat();
```

Why this is sound, not just convenient:

- `targetOid` is **HUNTING-scoped** — it is set only by `selectTarget` during HUNTING and
  cleared before the phase is left (the code already gates on `phase == HUNTING && targetOid != 0`).
  So in every phase where `onTick`'s interceptor code runs, `targetOid` is 0 and `onTick`
  cannot clobber a mission target.
- During travel legs, `runActiveTick` returns early *before* its combat block, so it isn't
  touching combat state there — `onTick` is the sole actor. During HUNTING, `onTick` returns
  early — `runActiveTick` is the sole actor. **No phase has both writing.**

The trade-off is explicit and documented: a species creature killed incidentally on a
travel leg is fought as an interceptor and does not credit the mission quota. That is an
acceptable (arguably more correct) behavior change, recorded in the plan's *As-Built
Deviations* section — the discipline is to name the deviation, not hide it.

## 4. Two supporting concurrency details worth internalizing

**Snapshot before you iterate a shared vector.** `getDefenderList()` returns a
`DeltaVector` whose `getSafe(i)` read-locks its *own* internal `ReadWriteLock` — but does
not bounds-check a stale index. Reading `size()` unlocked and then `getSafe(i)` can go
out of bounds if a concurrent `removeDefender` shrinks the vector. Because
`add`/`removeDefender` are `@preLocked` (they require the object lock), holding
`Locker agentLock(hunter)` across the copy gives a consistent snapshot:

```cpp
Vector<ManagedReference<CreatureObject*> > candidates;
{
    Locker agentLock(hunter);
    const DeltaVector<ManagedReference<SceneObject*> >* dl = hunter->getDefenderList();
    for (int i = 0; i < dl->size(); ++i) { /* copy strong refs */ }
}
// filter/distance math runs on retained ManagedReferences, outside the lock
```

Note the subtlety confirmed during review: the `DeltaVector`'s lock is *separate* from
the CreatureObject's object lock, so `Locker agentLock(hunter)` + `getSafe()` is **not**
a self-deadlock (`shedAllDefendersBilaterally` relies on exactly this).

**Respect asynchronous handoffs when you advance shared state.** Kill credit is delivered
by a queued `OBJECTDESTRUCTION` observer (`onHuntDestruction`), which is rejected once
`targetOid != destroyedTargetOid`. Proactive retargeting can advance `targetOid` before
that queued handoff runs, silently dropping a legitimate kill. The guard defers
retargeting for one tick while the current target is a fresh corpse — but only when an
observer is actually installed (`observerTargetOid == targetOid && targetObserver != nullptr`),
because `targetOid` is set while *approaching* and the observer isn't registered until
weapon range. Without that gate, a target that died before observer registration would
have no handoff coming and would suppress combat forever. The lesson: a "wait for the
async result" guard must be bounded by proof that the async result is actually coming.

## 5. The other half of the release: a symmetric predicate

Bug 1 is a smaller but sharp lesson in *symmetry of guards*. The branch had already added
a guard so a real player cannot attack a neutral sim bot
(`AiAgentImplementation::isAttackableBy`). The missing mirror was the other direction — a
neutral (faction-0) player was still attackable *by* a faction-0 sim bot, so the hunter's
grenade AoE (routed through `CombatManager::getAreaTargets`, which gates each splash
victim on `tano->isAttackableBy(attacker)`) damaged the player. The fix mirrors the guard
on the player side of `CreatureObjectImplementation::isAttackableBy`:

```cpp
// this == player, creature == attacker
if (creature->isAiAgent()) {
    AiAgent* agentCreo = creature->asAiAgent();
    if (agentCreo != nullptr && agentCreo->getSimPlayerBot() && agentCreo->getFaction() == 0)
        return false;   // a neutral sim bot never attacks a player
    ...
}
```

One predicate at the right chokepoint covers direct attacks, AoE splash, and retaliation
lock — because both `startCombat` and `getAreaTargets` consult it. When you add a
directional guard to a predicate, ask immediately whether the reverse direction needs the
mirror.

## Takeaways

1. Two self-rescheduling `Task`s dispatching lambdas onto the pool are **concurrent**, not
   serialized — shared mutable state between them needs a single writer or a lock.
2. Prefer restoring an existing single-writer invariant over bolting on a broad lock; name
   any resulting behavior change in the plan's deviations.
3. Snapshot a `DeltaVector` under the owner's `@preLocked` lock before iterating; its
   internal lock is not the object lock.
4. A "wait for the async handoff" guard must be gated on evidence the handoff will arrive.
5. Directional attackability guards usually need their mirror.
