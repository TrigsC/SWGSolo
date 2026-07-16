# P.8 — PvE PlayerBot Program

Owner vision (2026-07-15): a lived-in world of persistent, named, profession-
bearing PlayerBots whose activity is driven by the real economy but expressed
as individual, in-world agency — "basically players, just PlayerBots." PvP is
untouched. This doc is the living plan; the P.8.1 implementation plan is
`docs/1-plans/F_0.4.0_p81-pve-hunter-foundation.plan.md`.

## Phase map

- **P.8.0** — PvE combat/presence spike (diagnostics; DONE, see §Spike).
- **P.8.1** — identity roster + solo hunt loop (roster + presence SHIPPED;
  hunt loop pending, see §Phase 2 combat).
- **P.8.2** — group content (in-world ask-for-group, boss hunts, shared reward).
- **P.8.3** — service professions (doctor/entertainer bots, real buffs).
- **P.8.4** — player interaction depth (trade, chat).
- **P.8.5** — miner identity migration + population scale.

## Owner decisions

- V1 = identity roster + SOLO hunt loop; groups = P.8.2.
- Persistence = lightweight MySQL identity roster (bodies respawn/reattach).
- Coordination = manager-matchmade, handshakes acted out in-world.
- Realism = REAL combat vs real creatures; SIMULATED loot/rewards.
- **Bot appearance/gear = custom styled template + EXPLICITLY EQUIPPED gear**
  (owner 2026-07-16, like the Jedi-robe work). Bots should later swap
  armor/weapons based on what their persona "buys" — gear is a persona
  attribute, not inherited from a mob template. The identity roster is the
  natural home for "owned gear".

## SHIPPED (miner-ai)

- **P.8.1 Phase 1** (schema v1009 `simbot_identities`, roster load/mint/flush,
  identity↔body attach/respawn, async spike state machine, OBJECTDESTRUCTION
  observer machinery, pveMutex, runtime-refreshable pveConfig). Commits
  559c029b44, and code-review fixes.
- **P.8.1 Phase 1b — PlayerBot world presence** (P.8.0b). Bots become
  "players" to the spawn/AI systems via three flag-gated core relaxations at
  the exact `isPlayerCreature()` sites, behind a manager presence-OID set
  (opt-in per bot class; v1 = hunters + spike only) read via an atomic
  immutable snapshot. Lifecycle-balanced (publish-before-insert,
  remove-after-exit + 10s grace, disable=drain), spawn-attributed
  (`recordSimPresenceSpawn`), dashboard `pveActivity`. Commit 4ed8a62969 +
  fixes.

## Spike — findings (P.8.0, VERIFIED LIVE 2026-07-16)

The spike ran through many restarts as a pure instrument-first diagnostic.
What it PROVED (its redefined PASS boundary, owner-accepted 2026-07-16):

1. **The wilderness is empty without players.** Spawn areas, creature AI
   wake, and despawn are all `isPlayerCreature()`-gated. No humans → no wild
   creatures anywhere. (Spike v1: `targetOid=0`, timeout.)
2. **Presence works.** A presence-flagged bot triggers the spawn system like a
   player — lairs spawn around it (`spawnsTriggeredNearby>0`), attributed to
   its OID. This is the whole P.8 premise validated.
3. **A NEUTRAL bot can target a wild creature.** `AiAgentImplementation::
   isAttackableBy` refuses a **factioned** AI attacking a **faction-0** wild
   creature ("faction AI attacking non-faction AI" → false). So a hunter MUST
   be neutral (`setFaction(0)`), like a player. A stormtrooper (imperial)
   made every womprat `notAttackable`; the neutral artisan targeted fine.
4. **The kill-attribution observer machinery is wired** (registers on the
   target, verifies participant, one-shot, handoff outside the target lock).

What it could NOT prove, and why that's correct (moved to Phase 2):

5. **A few manager-side setter calls do NOT drive AiAgent combat.**
   `setTargetObject`+`addDefender`+`setCombatState` did not sustain combat:
   heartbeat showed `hunterCombat=false`, `hasDefender=false` immediately, the
   bot **unarmed** (`unarmed_default` — the mob template's weapon was never
   equipped by `createCreature`+`loadTemplateData`), and **128m from the
   target, not closing** (`setFollowObject` did not pursue). Landing damage
   needs controller-driven **move-to-target** + an **explicitly equipped
   weapon** — exactly Phase 2's `SimHunterController`. The spike was never the
   place to prove combat execution; forcing it would have duplicated Phase 2.

## Phase 2 combat — required approach (informed by the spike)

`SimHunterController` (the hunt loop) must own real combat, NOT rely on the
spike's manager-side setter shortcut:

1. **Neutral bodies.** Hunter identity bodies spawn with `setFaction(0)` (the
   spike rule) or the world rejects their attacks on wildlife.
2. **Explicit gear.** Spawn a custom styled base template and EQUIP a weapon
   object we control (do not depend on a mob template's built-in weapon — the
   create/place path does not equip it). Aligns with the owner's buy-your-gear
   vision; later swap armor/weapon by persona purchases.
3. **Move-to-target.** Use the shared `SimPlayerController` movement pipeline
   to close to weapon range (the bot must pursue; combat state alone does not
   move it). Same overland/`moveTo` primitives the miners prove.
4. **Sustained combat + kill.** Once in range and armed, the `simPvp`-style AI
   map (IDLE-only override, combat slots default) drives swings — as it does
   for PvP squads at close range. Reuse the OBJECTDESTRUCTION observer +
   `recordSimPresenceSpawn`-style attribution for the kill → typed
   `ResourceSpawn` harvest into the hive (simulation-only).
5. **Fast tick while active.** The 30s roster-maintenance cadence is far too
   coarse for a live hunt; the hunt loop needs a ~2s tick during active phases
   (the spike's adaptive-reschedule pattern).

Live-verification lesson throughout: **instrument first.** Every spike restart
narrowed a specific cause (empty world → area-enter fires → trySpawn reached →
permit sub-reason → target scan reject → combat heartbeat) rather than
guessing. Diagnostics in core files (SpawnArea, PlanetManager) were stripped
after use (commit e7aea12814); the permanent presence predicate stays.
