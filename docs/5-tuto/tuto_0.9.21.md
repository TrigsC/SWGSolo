# Tutorial 0.9.21: Reuse stock rules where they live

v0.9.21 gives roster bots real Scout harvesting. The three hardest bugs in this release were alike. Each time, the
bot path rebuilt a rule that the stock path already had, and the copy drifted. This tutorial walks through those
three bugs and the pattern each fix follows.

## 1. Who may harvest: ask the rule, not a proxy

The first version offered a group-owned corpse to the bots in the corpse's **threat map**, on the assumption that
anyone who did damage may harvest. On a human-tanked kill the threat map held only the human, so no bot ever got a
claim.

Stock decides with `Creature::canHarvestMe`:
- the harvester is within range and not in combat;
- it has the skill and hasn't harvested this corpse yet;
- the corpse-inventory owner is the harvester **or the harvester's group**.

A grouped kill sets the owner to the group ID. So the real rule is group membership, and damage plays no part.

The fix (`CreatureManagerImplementation.cpp:727`) walks the owning group's members, keeps the ones that are roster
bots with `getGroupID() == ownerID`, and queues a harvest-only claim for each. When the bot arrives,
`harvestPlayerBotCorpse` still calls `corpse->canHarvestMe(body)`, so stock keeps the final say. The corpse hook
only picks who gets a claim.

**Lesson:** when you need a list of who stock would allow, enumerate the population stock checks (the group) and
let stock's predicate decide. Don't substitute a related structure, like the threat map, that is usually a superset.

## 2. When to make room: copy stock's order of side effects

A full bag produced `bagFailed`, and the corpse was lost anyway. Stock marks the harvester **before** the deposit:

```text
corpse->addAlreadyHarvested(body);   // SimPlayerManager.cpp:22533, mirrors stock
...
depositPlayerBotResource(...)        // may fail on a full bag; the units are already gone
```

So the room has to be made before the harvest starts. `makePlayerBotBagRoom` (`SimPlayerManager.cpp:9555`) is the
first thing `harvestPlayerBotCorpse` (`:22441`) and `lootPlayerBotCorpse` (`:22098`) do after their gate checks. It
holds no object lock when it starts, and it follows the established lock order:

1. Read the bag's free volume under the bag lock alone. Free room is the smaller of
   `getContainerVolumeLimit() − getCountableObjectsRecursive()` and the `maxItemsPerBot` headroom.
2. Collect the protected IDs (loadout weapons, armor, consumables). Those collectors take `progressionMutex`
   themselves, so call them **outside** it.
3. Pick candidate rows under `progressionMutex`: HELD, source `loot`, not protected, not a buy-list template, no
   pending or live listing.
4. Release the mutex. Lock each object to read its junk value and flags, then sort: unsellable first, then the
   lowest junk value.
5. Destroy through `destroyPlayerBotHeldItem` (`:9491`).

`destroyPlayerBotHeldItem` was extracted from `removePlayerBotConsumedItem` (`:9479`) instead of being written a
second time. It locks the item, then cross-locks the bag (`Locker bagLock(bag, item)`), removes and destroys the
object, and only then takes `progressionMutex` to drop the row, decrement the held count and append the `-1` ledger
row with the caller's reason (`consume` or `discard`). One path means one reconciliation story: the item reconciler
sees the same final state whichever reason removed the item.

**Lesson:** before adding a step that frees resources, find where stock commits the irreversible side effect and
put your step before it. When an object-destroy path already exists, parameterise it, keeping its lock and
persistence order.

## 3. One policy, two copies: keep them mirrored

Scout XP stopped at 2000. `effectiveDeferredXpTypes()` already removed `scout` while the harvest gate was on, and the
dashboard said scout wasn't deferred. The trainer disagreed.

`computePlayerBotNextTrainingStepLocked` runs under `progressionMutex`, and the gate helpers take
`playerBotParityTestMutex`, so it can't call `effectiveDeferredXpTypes()`. It had an inline copy that only knew about
`medical`. The fix (`SimPlayerManager.cpp:8276`) builds the same list from the raw flags. That reproduces exactly
what the helper decides, without taking the helper's lock:

```text
harvestUndefers = progressionEnabled && progressionHarvestEnabled && progressionLootEnabled
healingUndefers = progressionEnabled && progressionHealingEnabled && healingPolicy.botHeals
```

**Lesson:** a lock-order constraint often forces a second copy of a policy. When it does, write down where both copies
are: the comment at the call site, and in this project the memory note "two deferred-XP copies". Change them
together; a test that only reads the dashboard's copy won't catch a mismatch.

## A fourth, smaller case: preemption lanes

`runGroupTick` (`SimHunterController.cpp:3260`) is a preemption lane: heal, spin, assist, member defence, then
directives and follow. A step returning `true` owns the whole tick (the G5 lesson). The loot step goes **after** the
defence tiers and before follow, and `runPlayerBotLootStep` returns `true` only while it is walking or harvesting. A
grouped bot therefore defends first, harvests between fights, and follows again as soon as the corpse is done.

## Listing reserve: parity first

Stock caps a player at `AuctionManager::MAXSALES` (25) bazaar listings, and `listSimBotItem` doesn't enforce it, so
the bot config clamps `maxListingsPerBot` to it. The reserve divides those 25 slots instead of adding more. Loot
counts against `max − reserve`, and resources (rows with source `harvest`, see `isPlayerBotResourceListingLocked`)
may use all 25. A bot is never allowed more listings than a player could have.
