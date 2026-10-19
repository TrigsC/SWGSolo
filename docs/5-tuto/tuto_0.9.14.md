# Tutorial 0.9.14 — Three truths: the server's, the client's, and the test's

F_0.9.13 gave roster bots armor. Every server-side check passed in the first live run: 55 of 55.
The owner then looked in game and saw nothing on the bots. This release is a lesson about the
three places a feature has to be true, and how each one surprised us.

## 1. The server's truth: mitigation is a choice of code path

Stock `CombatManager::getArmorReduction` never reads worn armor for an NPC. An `AiAgent` defender
is mitigated by its template armor and resists and returns before the player armor blocks. So
"put armor on the bot" was never enough; combat had to be told.

The seam is deliberately narrow:

- The player PSG and armor code moved, unchanged, into
  `CombatManager::applyWornArmorReduction` (`CombatManager.cpp:2683`). The player path calls it at
  `:3014`, so player math is identical by construction.
- An AI defender takes that helper only when the transient flag `AiAgent::simBotWornArmor` is set
  (`:2912`). That flag is written by sim-layer equip code alone and reset in `loadTemplateData`.

Why a flag, and not "has an armor object in a slot"? Because stock NPC outfits already contain
armor objects (`mobile/outfits/*.lua`). Keying on the object would have rebalanced every
stormtrooper on the server. **Gate behavior changes on intent, not on incidental state.**

Two server-side traps surfaced only live:

- `ObjectManager::cloneObject(item, true)` marks the clone transient and *then* reads the source's
  serialized fields, restoring `persistenceLevel 1`. The fix is one line after the clone
  (`SimPlayerManager.cpp:9679`). **A helper's name is not its contract; read what it does after
  the flag you passed.**
- `LuaObject::getBooleanField` reads the table on top of the Lua stack. The config parser read
  `armorLoadout` while the child `loadout` table was pushed, so `armorLoadout = true` parsed as
  false. Every harness scenario armed the gate per identity, so only the production stage, with
  the real configured gate, could see it (`:10744`).

## 2. The client's truth: what the server holds is not what the player sees

The server had the chest in the `chest2` slot, in the wearables list, in the protection map, and
absorbing damage. The client drew nothing.

The novice bodies were stock `dressed_commoner_*` templates. Their client data file (`.cdf`)
bakes the clothing into the appearance, and that baked outfit masks a server-sent chest piece.
Weapons are not masked, which is exactly why Castu's knuckler always showed and made the problem
look like it could not be the container path.

P.7 had already met this with robes on ranked Jedi and left a note in its dress group: add a body
only after an in-client check that its `.cdf` bakes nothing. The fix follows that precedent:

- 20 clothing-free vendor bodies (`sim_playerbot_bodies.lua:4` onward), one line each, subclassing
  `object_mobile_vendor_shared_<species>_<gender>` (`clientDataFile = ""`).
- Clothing comes from the server instead, chosen by species and filtered by the player wear rule
  (no shoes on a Trandoshan), and armor stows clothing off its slots and puts it back
  (`equipPlayerBotArmorCopyLocked`, `:9683`).

**Lesson:** a dashboard can prove the server's state, never the picture. When a feature is
visual, the receipt is not done until someone has looked.

## 3. The test's truth: a harness assumes a world

Five live runs failed on the amended build, and none was a product bug. Each one was a harness
assumption the new world broke:

| The harness assumed | The world now had |
|---|---|
| it is the only seller on the bazaar | the owner's real listings, one of which a harness bot bought and the cleanup then destroyed |
| every bot is human | ten species, two of which cannot wear padded armor |
| a chest plate displaces a shirt | separate slots: players wear a shirt under a chest plate |
| one armored hit happens in any fight | a random hit location per attack |

The fixes are the general pattern:

- **Isolate what the test may touch.** A harness buyer now considers only listings the harness
  created (`:14680`). Production buyers are unchanged.
- **Pin what the test does not measure.** `setBodyTemplate` fixes a human body for armor
  scenarios. The appearance scenario keeps the seeded body, because the seeded body is what it
  measures.
- **Remove chance from assertions.** The mitigation scenario wears a full set, so every hit
  location is armored.

The knuckler incident is the costly one. A test that can act on shared state must be scoped to
the state it created. A cleanup pass that deletes "its" identities is only safe if those
identities never acquired anything real.

## 4. Derived, and stable

A bot now keeps one face for life without storing anything. `SimBotAppearance::chooseBody`
(`SimBotAppearance.h:59`) scores each body by `splitmix64(identityId ^ hash(template) ^ salt)`
and takes the highest. That is rendezvous hashing: removing or reordering bodies moves nobody
whose winner remains, and adding one moves about 1 in n+1 bots. An index into a list would have
re-rolled everyone on any edit.

Outfit variants use the same rule, keyed by their full content (templates plus customization),
because two variants can share garments in different colors. This is the v0.9.13 idea once more:
**if it can be derived from durable inputs, derive it**, and choose a derivation that stays
stable when the inputs grow.
