# Tutorial 0.9.15: Adding to a value you did not compute

F_0.9.14 gave roster bots food. The parts that took longest were not the stomach or the bazaar.
They were three places where new code added something to a value that other code computes, and
the other code computed it differently than it looked. This release is about reading the whole
function you are adding to, and about schedules that wake only when something can change.

## 1. The value had a hidden branch

Crispic adds `attack_accuracy`. For a player, stock combat adds that mod to the accuracy sum. For
an NPC, `CombatManager::getAttackerAccuracyModifier` takes a different branch: it calls
`AiAgent::getSkillMod("attack_accuracy")` and, if the result is positive, uses it as the bot's
whole accuracy. Otherwise it falls back to `getChanceHit() * 100`.

A food buff therefore replaced bot accuracy instead of adding to it. The fix looked simple: compute
the stock value without the food, then add the food. The first attempt subtracted the buff from
`getSkillMod`'s result. Live verification measured accuracy falling from 212 to 42.

`AiAgentImplementation::getSkillMod` (`AiAgentImplementation.cpp:639`) is not one value but a
chain. It returns the creature's skill-mod total when that is nonzero; otherwise the template
statistic; otherwise, for anything containing "accuracy", `100 + 2 x level` (`:710`). A level-56
bot's stock accuracy is 212 from that last rule. One point of food makes the total nonzero, and the
whole fallback chain disappears behind it.

The fix splits the function without changing it. `getSkillMod` keeps the base read, and the rest
moved verbatim into a new `getSkillModFallback` (`:663`, declared `@read` in `AiAgent.idl`). The
seam (`SimBotConsumables::foodAdjustedAiAccuracy`, `SimBotConsumables.h:266`) restores the fallback
when the food is the only skill mod, then adds the food:

- unbuffed, the input already holds the fallback, so the result is exactly stock;
- buffed with nothing else, the fallback comes back, plus the buff;
- buffed on top of other mods, the fallback stays hidden, as it does in stock.

**Lesson:** before you add to a value, find every return in the function that produces it. "Total
minus my part" is only correct if your part cannot change which branch runs.

The same release hit this rule in a mild form on defense. The player block reads `dodge_attack`
through `getSkillMod`, which on an `AiAgent` would also consult the fallback (20 for level > 80).
The flagged AI branch (`CombatManager.cpp:2146`) reads `getSkillModOfType(..., BUFF)` so only food
counts.

## 2. The value had a hidden owner

Players eat through `ConsumableImplementation::handleObjectMenuSelect`, which needs the item inside
the creature and a `PlayerObject` for the stomach. A bot has neither, so the bot path had to build
the same buff itself. The first cut had a shared method return the new `Buff*`. That buff was
created in a local `ManagedReference` and returned as a raw pointer. In engine3, the strong count
reaching zero destroys the object, so the caller could receive freed memory.

`applyConsumptionBuff` (`ConsumableImplementation.cpp:124`) keeps ownership in one scope: it
builds the buff, locks it, sets its modifiers and adds it to the consumer before the reference goes
out of scope. Both the player path and the bot path call it, so player behavior is identical by
construction (the v0.9.14 `applyWornArmorReduction` pattern).

**Lesson:** a method that creates a distributed object should finish the handoff itself, or return
a reference type, never a raw pointer to something only a local holds.

## 3. The value had a hidden population

Restock candidates come from every listing on the planet. The buy-list path filters by item type
before loading anything; the first consumable need did not, because its comment reasoned about dose
counts. In the harness this was invisible: a harness buyer sees only harness listings, three or
four of them. On the live bazaar there were 17, sorted by price. The shared 6-load budget was spent
loading the owner's cheapest non-food items, and production bots never reached the food.

The fix (`SimPlayerManager.cpp:15529`) matches the field the listing index actually stores, the
template's client game object type, exactly as buy-list entries do.

**Lesson:** a test fixture that isolates the subject also removes the crowd the subject must work
in. The F_0.9.13 isolation rule ("a harness buyer sees only harness listings") is right for safety,
and it is why a production stage is required, not optional.

## 4. Sleep until something can change

The owner asked for a timer so bots would not spam food. A timer would still retry when nothing
could succeed: buffs still running, stomach still full, no doses at all. Instead, after each attempt
`SimBotConsumables::nextCheckAtMs` (`SimBotConsumables.h:206`) computes the earliest moment any
blocker could lift:

- the soonest buff expiry, read from the buffs on the body after eating;
- the time digestion frees enough room (`msUntilRoom`, `:68`, exact to the next 18 s step because
  the stomach keeps its partial interval, `digest`, `:38`);
- a no-dose retry, during which the bazaar can restock;
- a 30 s floor, so a fault can never spin.

`consumePlayerBotFoodBeforeEngage` (`SimPlayerManager.cpp:9027`) returns after one map read until
that moment (`:9183`), so the three call sites can sit on hot engage paths. A spawn clears the
deadline, because the buffs it was based on died with the old body.

The stomach follows the v0.9.13 rule, "if it can be derived, derive it": a fill and a timestamp,
digested on read, with no task and no schema.

## 5. The rule had a hidden assumption

The trigger the owner chose was "eat before fights the bot starts". It was correct and live-verified
in the harness. Production then showed bot 850 fighting 21 times in one boot without starting one:
mission bots are ambushed on the way to the lair, and many die before arriving. The owner added a
third call site, when a bot turns to fight an attacker on a travel leg
(`SimHunterController.cpp:6948`), placed after the "already fighting this attacker" return and
before the hunter lock.

**Lesson:** a behavior rule describes the world its author pictured. Production evidence is how you
learn which world the bots actually live in.
