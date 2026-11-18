# Tutorial 0.9.22: Who owns a state change

v0.9.22 puts roster artisans on the miner controller. Most of its review rounds and live fixes came down to one
question: **which thread, and which object, is allowed to change this state?** This tutorial covers the four places
that question decided the design. It also covers the identifier mix-up that the harness couldn't see.

## 1. Draw order is part of stock behaviour

The stock sample in `ResourceSpawner::sendSampleResults` draws `System::random` three times, each draw only on the
branch that needs it:
- the sample rate;
- max units;
- the gamble roll, only while gambling.

Moving the arithmetic into `ResourceSampleYield.h` must not change which draws happen or in what order, or a player's
next roll changes. So the helper takes the draws as inputs (`roll100`, `roll3`, `gambleRoll`), and the caller still
makes each draw at its stock place. For example, `roll3` is drawn at `ResourceSpawner.cpp:1009`, before the gamble draw
and before `calculate()` at `:1013`.

**Lesson:** when you extract stock math, the RNG belongs to the caller. A pure helper that drew its own numbers would
pass every unit test and still change gameplay.

## 2. A durable receipt must expire with the thing it receipts

A survey tool is an object, and the one-time grant receipt is a ledger row. They persist on different schedules:
- the ledger row flushes in seconds;
- the object survives a restart only after the next 5-minute delta save.

A restart in between left a receipt with no tool. The boot reconcile correctly wrote the tool off, but the receipt said
"already granted", and nobody sells survey tools.

The fix keeps one rule: a receipt counts only while a granted tool survives.
- The reconcile's write-off revokes the receipt (`revokePlayerBotSurveyToolReceiptLocked`, `SimPlayerManager.cpp:23220`,
  a `receipt` −1 row).
- The load computes grants − max(revocations, reconciled losses of granted objects), so losses recorded before the fix
  also count.

**Lesson:** when two stores persist at different speeds, write down which one is the evidence. Here, the object's fate
(reconcile rows) decides; the receipt alone does not.

## 3. Requests, not claims, across threads

Retiring a legacy miner went through three designs:
1. A maintenance-thread check (`isSafeForLegacyRetirement`) raced the miner's own tasks.
2. An atomic claim (`0 idle → 1 sampling → 2 retiring`) still published "idle" before the miner's finish code had
   finished.
3. **The miner retires itself.** The manager only sets a flag (`requestLegacyRetirement`, `SimPlayerController.cpp`),
   and the miner acts on it in its own work loop.

In the third design:
- A generation-gated `LEGACY_RETIREMENT_CHECK` task acts only on an `IDLE`/`WAITING` miner with no assignment.
- A busy miner consumes the flag at its next `startSimLoop` boundary (`:5899`), where the state left by the action that
  just finished is ignored.
- Both paths re-run the one surplus rule (`legacyMinerSurplus`, `SimPlayerManager.cpp:41497`) right before teardown, so
  a stale request keeps the miner.

**Lesson:** if another thread decides when an object changes, every field that object reads is a race. Let the owner
apply the change, and re-check the decision when it does.

## 4. Type versus name

`SimMinerController::targetResource` holds a resource **type** (`steel_duralloy`). The intelligent assignment keeps the
spawn **name** (`Citeian`) in `intelligentResourceName`.

The harness passed every sampling scenario because its op passes a name. Every real artisan sample came back `noSpawn`.
The fix passes the name, and `samplePlayerBotResource` (`SimPlayerManager.cpp:23052`) falls back to
`getCurrentSpawn(type, zone)` exactly as corpse harvesting does.

**Lesson:** an identifier that crosses from one subsystem into another needs a written contract (type or name). A
harness that builds its own inputs can't test that contract.

## 5. Parity includes what the client sees

Stock `sendSample` also broadcasts a `PlayClientEffectLoc` with the active survey tool's `sampleAnimation`. The artisan
knelt, but showed no effect until `broadcastPlayerBotSampleEffect` (`SimPlayerManager.cpp:23233`) sent the same packet
from the held tool of the spawn's class. Only the owner's client pass could catch this.

The same is true for the auction subtype detail in ARCHI.md. An auction stores `ORGANICHIDE` or `INORGANICMINERAL`,
never the generic `RESOURCECONTAINER`, so a filter written against the generic type silently matched nothing.
