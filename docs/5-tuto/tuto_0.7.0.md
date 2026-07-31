# Tutorial 0.7.0 — Arbitrating two movement drivers: the combat AI-map swap

**Release**: v0.7.0 (P.6.6 / P.6.6b)
**Core principle**: how a C++ controller can take exclusive ownership of an
`AiAgent`'s *movement* while leaving the stock behavior tree in charge of *combat* —
without the two fighting over the same agent every tick.

## The problem: two drivers, one agent

A SimPlayer NPC has **two** things that can move it:

1. The fork's C++ controller (`SimPvpBotController`), which computes paths and calls
   `moveTo`.
2. The stock Lua behavior tree, whose `rootDefault` has a `TreeSocket(slot=MOVE)`
   that keeps running while the agent is `FOLLOWING` (i.e. in combat).

For travel this was already solved: `simPvp.lua` overrides only the IDLE slot, so
the controller drives movement and the stock tree stays out of the way. But P.6.6
needed the agent to be **in combat** (so stock combat fires its weapon) while the
**controller** still owns the approach/hold movement. The moment the agent enters
combat, `movementState` becomes `FOLLOWING`, the stock MOVE socket wakes up, and now
*both* drivers push the agent — the "converged strangely / stacked / clipped"
behavior the owner saw live.

You cannot simply stop the stock tree: you need its combat sockets (attack/target)
live. You only need to neuter its **MOVE** socket, and only **while engaged**.

## The pattern: a scoped, swappable custom AI map

The fix (`SimPvPController.cpp`, `swapCombatAiMap`) dynamically replaces the agent's
AI map for the duration of combat, then restores it:

```cpp
// bin/scripts/ai/templates.lua defines the combat map:
//   simPvpCombat = { IDLE = idleSimPvp, MOVE = moveNoopSimPvp }
// where moveNoopSimPvp (simPvp.lua) is a no-op MOVE tree.

void SimPvpBotController::swapCombatAiMap(bool engaged, const String& baseMapHash) {
    ManagedReference<AiAgent*> strongAgent = agent;
    // ... preserve the live formationOffset across the swap ...
    strongAgent->setCustomAiMap(engaged ? "simPvpCombat" : baseMapName);
    strongAgent->setAITemplate();          // NB: this does blackboard.removeAll()
    if (hasFormationOffset)
        strongAgent->writeBlackboard("formationOffset", formationOffset);
    // On teardown, also stop any half-walked stock path:
    if (!engaged) {
        strongAgent->setMovementState(AiAgent::OBLIVIOUS);
        strongAgent->clearPatrolPoints();
        strongAgent->clearSavedPatrolPoints();
        strongAgent->clearCurrentPath();
    }
    combatAiMapInstalled = engaged;
}
```

Three things make this correct rather than a hack:

- **It swaps the MOVE tree, not the whole brain.** `simPvpCombat` keeps IDLE and
  leaves the combat sockets from `rootDefault` intact, so stock combat still selects
  targets and fires — only the MOVE socket is replaced with a no-op. The controller's
  `driveCombatMovement` is now the *sole* mover.
- **It respects a documented side effect.** `setAITemplate()` calls
  `blackboard.removeAll()`. Formation state lives in the blackboard, so the helper
  reads `formationOffset` **before** the swap and writes it back **after** — a
  concrete example of the project rule "mimic the existing engine contract, don't
  fight it" (ARCHI §10).
- **It is strictly scoped.** The map is installed exactly at
  `acquireControllerCombatTarget` (`swapCombatAiMap(true, ...)`) and removed at
  `teardownControllerEngagement` (`swapCombatAiMap(false, "simPvp")`), which also
  clears any partially-walked stock path so no stale MOVE leg survives the handoff.

## Where the handoff actually happens

`driveCombatMovement` is the per-tick owner while a target is held:

- out of weapon range → `approachTarget` (cell-aware `moveTo`);
- within range → `engageHeldTarget`, which calls `CombatManager::startCombat`,
  `setMovementState(OBLIVIOUS)`, and `advanceWorkLoopGeneration("combatEngage")` to
  reject any late-arriving path result from the just-cancelled approach.

Because the stock MOVE socket is now a no-op (`simPvpCombat`), the agent can be
`isInCombat()` and firing while standing exactly where the controller parked it —
the "engage-and-hold at weapon range" behavior. Fix B builds directly on this: within
range the controller **holds regardless of LOS** (stock combat already gates damage
on LOS), instead of the old clear-combat-and-approach loop that walked a bot into a
wall until it died.

## Why P.6.6b needed almost no new movement code

Squad aggro-sharing (P.6.6b) reuses this lane wholesale. An idle squadmate that
adopts a shared target just calls the *same* `acquireControllerCombatTarget` →
`swapCombatAiMap(true)` → `driveCombatMovement` path. The only genuinely new movement
concern was that a *convergence* leg can be far longer than a 100 m engage approach,
so `driveCombatMovement` widens its outer-bound teardown distance and timeout while
`combatIsConvergenceTarget` is set (300 m / 60 s) — a few lines, not a new mover. That
is the payoff of getting the driver-arbitration primitive right: the second feature is
a thin broadcast on top of it.

## Takeaways

- When two subsystems can drive the same distributed object, don't add a global
  mode flag — **swap the narrowest unit of behavior** (here, one AI-map slot) and
  scope it tightly to the state that needs it.
- Treat engine side effects (`setAITemplate` → `blackboard.removeAll()`) as part of
  the contract: save/restore around them.
- A clean arbitration primitive makes follow-on features cheap: P.6.6b is mostly a
  TTL-guarded shared set plus a wider bound on an existing lane.
