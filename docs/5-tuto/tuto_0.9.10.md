# Tutorial 0.9.10 — Who owns it? Objects, records, and callbacks

F_0.9.3 lets a roster PlayerBot loot its kills into a persistent vault. Most of the work
was bookkeeping. Almost every defect, though, came from one question asked in two places:
**who owns this?** One place is an item between a corpse and a database row. The other is
a movement callback that arrives after the movement it belonged to was replaced. This
tutorial follows both through the code that shipped.

## 1. Two stores, one fact

A looted item lives in two stores that are saved at different times:

- the **object store** (BerkeleyDB), which holds the item, its stats and its parent
  container, and which delta-saves every 5 minutes;
- **MySQL**, which holds the `simbot_items` row saying which roster identity owns it,
  written by the progression flush about every 60 seconds.

The roadmap's first design was write-ahead: write a `pending` row, move the object, mark
it `held`. That fails here. The row is not durable when it is written; it sits in a
batch that a flush picks up later. A flush can copy `pending` just before the transfer
fails, and a crash after the transfer but before the flush leaves an object with no
recorded owner.

The fix makes the **object store carry the owner**. Each identity gets its own bag
inside the vault (`AiEconomyManager::getOrCreateItemBag`,
`managers/aieconomy/AiEconomyManager.cpp:230`). Moving the item into the bag
(`SimPlayerManager.cpp:12387`) is a single object change: the parent pointer and the
owner are the same fact, so they cannot disagree. The MySQL row follows, and at boot
the reconciliation table (`SimBotItemReconcile.h:46`) treats the bag as the recovery
source:

| Row | Object found | Result |
|---|---|---|
| none | in X's bag | new `held` row for X (an unflushed deposit) |
| `held` for X | missing | `orphaned`, −1 journal row for X |

The second line is the one live verification actually hit. A restart came less than one
delta save after a loot: the row had been flushed, but the object had not been saved, so
the bag came back empty. That is the same window a player has, and the code handles it
honestly. The ARCHI rule that follows: **the loss window for an item is the object-store
save, not the MySQL flush.**

The rule extends to the vault itself. `creature_inventory.iff` turned out to be a
`GENERIC` container (type 4), not the `VOLUME` (type 2) the plan assumed, so the first
boot failed to create the vault. It failed **closed**: `playerBotLootGateEnabled` also
requires `isItemVaultAvailable()`, so the stock loot path ran unchanged. Bags are now
recognised by template, not container type (`AiEconomyManager::isItemBag`, `:297`),
because a looted item can itself be a container.

## 2. Lock order is a property of the whole code base

A transfer requires both the destination and the object to be locked
(`SceneObject.idl:348-357`), and the stock path also locks the source parent
(`TransferItemMiscCommand.h:357`). The loot step therefore takes one chain:

```
body → corpse → corpse inventory → item → bag
```

Each `Locker` crosses the one before it, so engine3's cross-lock precondition (the
crossed object is already held) always holds. Every other path that touches the vault
uses a subset of the same order: bag creation takes bag → vault, deletion takes
item → bag, and reconciliation locks one container at a time. Manager mutexes
(`progressionMutex`, `lootMutex`) are taken only before or after the chain, never
inside it. One mutex rule was not new but bit anyway: `playerBotLootGateEnabled` takes
`playerBotParityTestMutex`, which must never nest inside `progressionMutex`. So the
owner check at the chokepoint takes the three in sequence and holds none of them while
taking the next.

## 3. Callbacks have owners too

The second half of the chunk came from production. With the gate on, real bots queued
corpse claims and never looted one. They kill in `ENGAGING_LAIR`, and a destroyed lair
starts the next travel leg immediately, so the bot was never standing idle. The fix lets
the loot step start **while walking** on a travel leg: `moveTo(corpse)` replaces the
leg, and the leg is resumed afterwards (`finishPlayerBotLootStep`,
`SimHunterController.cpp:3726`).

Replacing movement raises a question the code base had never had to answer: when an
arrival or path failure comes in, which movement does it belong to? Movement is
asynchronous. `ArrivalCheckTask::run` (`SimPlayerController.cpp:866`) checks the
work-loop generation, then `checkArrival` takes the agent lock, sets `WAITING`, releases
the lock, and calls `onArrived()`. Between the check and the call, the loot step can
finish on another thread. If the stale corpse arrival then reaches
`TRAVEL_TO_TERMINAL`'s arrival branch, the bot "arrives" at the terminal while standing
at a corpse.

Codex found five versions of this race, one per attempted fix:

1. guarding on `lootStepActive`, which is cleared before the callback reads it;
2. cancelling only when `MOVING`, but arrival has already set `WAITING`;
3. counting in-flight callbacks, which misses a callback that hasn't entered yet;
4. recording the *live* generation at dispatch, which the finish has already advanced;
5. guarding arrivals only, while path failures had the same gap.

The design that holds has two parts:

- **The callback carries its own birth certificate.** The task lambdas publish the
  generation they captured in a thread-local (`SimPlayerController.cpp:50`, set at
  `:759` and `:879`). `onArrived` runs synchronously on the same thread, so
  `getCallbackDispatchGeneration()` returns the generation of the movement that produced
  this callback, not whatever the controller holds now.
- **One mutex orders the check against the finish.** Under `lootArrivalMutex`, the
  finish advances the generation, records the corpse walk's range of generations as
  stale, and only then clears the guard. Under the same mutex,
  `classifyLootWalkCallback` (`SimHunterController.cpp:2318`) sorts every callback into
  one of three answers: the current loot walk's, stale, or the phase's own. If the
  callback is checked first, the guard is still up. If the finish runs first, the
  callback's own generation is below the finish's advance. Either order is correct.

Both callbacks call the classifier before any other branch
(`SimHunterController.cpp:2343`, `:2558`). The flee-arrival branch had sat above the
first guard and was one of the five escapes.

What is left is a pre-existing gap: a *successful* path result can still be applied
after its movement was replaced. Every controller that replaces movement has this, so it
is recorded as mover debt rather than fixed here.

## 4. Test the code you ship

The parity harness passed 13 of 13 while production looted nothing. Harness bodies run
`SimParityTestController`, so no harness scenario ever executed
`SimHunterController::runPlayerBotLootStep`. The owner's suggestion closed the gap:
spawn a creature with loot next to a *real* roster hunter on a travel leg and let it
attack. `provokeLootKill`, `awaitBotLooted` and `assertHunterResumed` do that, and the
trace shows the whole contract:

```
TRAVEL_HOLD  id=84 phase=TRAVEL_TO_MISSION state=begin attacker=jawa
LOOT_CLAIM   id=84 corpse=281475055493039
LOOT_STEP    id=84 begin phase=TRAVEL_TO_MISSION dist=5 walking=true
LOOT_RESULT  id=84 result=looted
LOOT_STEP    id=84 end outcome=looted resumeLeg=true
```

The lesson matches the one F_0.8.1 drew about adoption: **a harness that drives the
manager API directly proves the API, not the controller that is supposed to call it.**
