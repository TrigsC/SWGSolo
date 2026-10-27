# Tutorial 0.9.19: Every path that can undo a decision must know about it

G4 gives a group leader three tactics: "Paki, pull", spin mode, and "take foods". The scoped harness passed early.
The owner's client passes then found eleven defects in a row. Almost every one had the same shape: one part of
the code made a decision, and another part, on another thread or in older code, undid it without knowing. This
tutorial walks through four of them, using the code as it shipped.

## 1. Two threads defend the same bot

A hunter bot is driven by two independent tasks (ARCHI §12): `runActiveTick` (the decision tick) and `onTick`
(the arrival check). Both run self-defence: if something attacks the bot, fight it.

The pull's walk back is a deliberate exit from a fight. `runGroupTick` learned to skip self-defence while the
pull is returning (`SimHunterController.cpp`, the `pullReturning` check), and the bot still did not move. Boot 8's
log showed a `TRAVEL_ENGAGE` on the pulled creature every few seconds: that was `onTick`, the other thread,
re-aiming the bot at it.

The fix is the same exemption on the second path, through an atomic each thread can read:

```cpp
if (groupPullReturnActive.load(std::memory_order_acquire) ||
        groupBackOffMoveActive.load(std::memory_order_acquire))
    return;
if (groupSpinEngaged.load(std::memory_order_acquire))
    return;
```

The spin flag shows the ordering detail Codex caught. It is raised **before** `CombatManager::startCombat` in
`engageGroupLair`, and cleared again if the start fails. Raised after, there is a window where `onTick` sees the
bot in combat with its lair, sees no spin flag, and sheds the fight (`resetInterceptorCombat`). That is exactly
what the owner saw as "attack the lair" doing nothing.

**Rule:** when you add a mode that changes how a bot reacts to combat, list every path that reacts to combat,
not only the one you are editing.

## 2. An arrival radius does not stop a path

The holder leads the group to its revealed lair. G4 first set `arrivalMeters` to 48 and aimed the walk at the
lair, and the bot ended 2.7 m from it. `driveGroupTravel`'s arrival radius only decides when to stop issuing new
paths; the path already issued runs to its end.

The G2 hold-short already did it right: aim at a point short of the target. The shipped lead does the same:

```cpp
Vector3 approach = hunter->getWorldPosition() - context.position;
approach.setZ(0.f);
approach.normalize();                       // UNIT_X when degenerate
target.position = context.position +
    approach * groupingConfig.missions.leadStopMeters;   // 40 m
target.arrivalMeters = groupingConfig.rallyArriveMeters;
```

The same lesson shows up twice more in G4. The pull approach and the spin walk-in both had to replace the bot's
in-flight path (the G1 follow leg) on their first tick, because the "re-path only when the mover is idle" rule
meant the new walk was never issued at all. A `moveTo` replaces the path in flight; waiting for idle never comes.

## 3. A refresh that republishes can undo an order

The stock "Nearest mission for group" task keeps refreshing the plan. In G3, every refresh re-published a mission
directive to every bot (`applyGroupNearestMission`). That was harmless until other orders existed:

- after "follow", it put the bots back into `MISSION_WAIT`, where they stand still;
- after "get buffs", it replaced the buff trip with `MISSION_TRAVEL`;
- mid-fight, it switched the current mission to one whose advertised point was nearer than the relocated lair.

Each fix narrows when the refresh may speak: only while the plan runs, never over a G2 trip
(`stage < MISSION_GRAB_TRAVEL`), and never away from a revealed, standing lair. A periodic publisher must know
which decisions it does not own.

## 4. Completion must not depend on who remembers

A group-mission entry completed only when the holder's controller saw its lair die. "follow" clears that
controller's mission context, so the lair died, the reward split, and the entry stayed open forever: the second
mission never started. The manager already knows the moment a holder's mission lair is destroyed. It now completes
the entry there, and `onGroupMissionEntryDone` returns early for an entry that is already closed, so the
controller's later report is harmless.

**Rule:** put a state transition where the fact is known for certain, and make it idempotent, so a second
reporter cannot double-count it.

## Process note

Every client-pass fix started from `missiondiag.log`. When a decision could not be seen (why did spin not start?),
the fix began with a rate-limited reason line instead of a guess:

```
GROUP_SPIN id=849 spin_check lair=... should=false hasArea=true leaderInCombat=false lairAggro=false go=false leaderToLair=4.9 botToLair=0.5
```

That one line settled the owner's design question (start on aggro or on command, never on proximity) in a single
round.
