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

## P.8.1c — Acquisition demand-ledger (substitution-pool family signals)

Live verification 2026-07-17 found Phase 2 hunters correct but permanently
`IDLE_HOME`: the matchmaker was strict demand-pull reading only each crafter
profile's single collapsed `activeResource.type` (always a mined veggie/mineral),
so the meat the species produce was invisible. Root gap: chef supply recognized
only `water`; hunter harvest landed in a separate accumulator that never fed the
demand supply. Plan: `docs/1-plans/F_0.4.1_p81c-acquisition-demand-ledger.plan.md`
(Codex plan-review, 7 rounds, 14 findings → APPROVED).

**Owner architecture:** `Crafter needs → shared demand ledger → acquisition
opportunities {sample/harvester, solo hunt, group hunt, loot camp, mission}`.
The ledger must expose shortages for **every eligible satisfier family** (a
crafter's active opportunity governs what the *crafter* makes but must not erase
demand for the other families the recipe can use); PvE bots pick by **expected
economic value per unit time**; **no** dedicated meat-consumer profile
(overproduction); speculative floor deferred.

**Model (shared substitution-pool, stable per-family targets):** each ANY-OF
profile is one pool; per creature family the signal has two independent
components — an **allocation** share of the shared gap (largest-remainder,
capped by a *stable* `effectiveCeiling = min(units, fraction·desiredReserve)`,
default 25%) plus an **independent capped reserve** floor (diversity, own
`reservePressureFloor`). `signalUnits = alloc + reserve`. Self-limiting:
simulated harvest is fed back into chef `totalKnownSupply`, so producing meat
shrinks the gap and the signal quiets at the ceiling even while veggies still
fill the pool.

**Consistent, restart-durable accounting:** `familySupply = immutable boot
baseline + monotonic session tally`. Baseline captured once (guarded
`pending→capturing→ready`, single-winner, slow durable scan
`AiEconomyManager::snapshotStockpileTotalsByFamily` — includes `exact_type`
lots, which `persistentConceptualTotals` omits — outside the lock, atomic
publish; failure→`pending` + bounded backoff, never publish empty; no signals
until `ready`). Session tally (`pveSessionHarvestByFamily`, `pveMutex`)
increments in `recordPveHunterHarvest` atomically with the order's
`harvestedUnits`; reservations + session tally snapshot under **one** `pveMutex`
so a harvest can't read as both inbound and supply. No double-count with the
miner/stockpile paths (hunters never write `conceptualMinerTotals`; exact_type
excluded from `persistentConceptualTotals`).

**Matchmaker (value/time + intra-pass reservation):** candidates = creature
`familySignals` with `signalUnits>0` matched by `species.harvestKind ==
signal.family`; score `= signal.pressure · estYieldUnits / huntTimeEstimate`;
after each assignment the selected `(profileKey, family)` signal is
saturating-decremented by the order's `expectedYieldUnits` and remaining
identities re-scored, so N hunters in one pass spread across real need (one
bounded overshoot then drop). Strict-pull preserved (no signal → no dispatch).
Reservations scoped per `(profileKey, family)`; global harvested supply serves
every meat-accepting profile.

**Gating:** all of the above is behind `pveConfig.acquisitionLedger.enabled`
(default **off**); off reproduces the prior `activeResource`-only matchmaker and
skips harvest injection exactly. Simulation-only. v1 covers the `meat` family
only (all configured species are `harvestKind=meat`; hide/bone and the
loot/mission value terms are documented additive hooks for a later slice).

**Live-verification contract:** with the flag on, `pveActivity.demandFamilies`
shows a non-zero `meat` signal, `allocComponent` shrinks as `familySupply` rises
and hits 0 at `effectiveCeiling` (leaving only `reserveComponent`),
`reservedInboundSupply` nets in-flight hunts same-pass, `baselineState=ready`,
and hunters leave `IDLE_HOME` → announce/travel/hunt → typed harvest climbs.

## P.8.1d — Creature-resource turf split

The PvE acquisition ledger makes the `organic>creature_resources` subtree
hunter territory. When the ledger's turf split is active, SimMiners skip
creature-resource entries during demand-weighted target selection, while
hunters remain the producer for the configured creature families (`meat` in
v1). This prevents mined `meat_*` resources from satisfying the hunter supply
view; hide, bone, milk, egg, and seafood remain intentionally unfilled until a
matching hunter harvest kind is implemented.

Harvest provenance is retained end to end. Miner and hunter yields use
composite in-memory accumulator keys `(resourceSpawnObjectID, origin)` and
durable exact-type lot keys `(resourceSpawnObjectID, acquisitionSource)`.
Miner deposits remain `conceptual_miner`; hunter deposits are `pve_hunter`, so
the boot family baseline can count only hunter-origin creature lots without
deleting or rewriting the existing miner stockpile.

The turf split is a restart-latched gate: the first `applyPveConfig` captures
`acquisitionLedger.enabled && minerCreatureResourceExclusion` as the effective
value used by miner exclusion, baseline capture, and the dashboard. Runtime
config refreshes update the configured value but never change effective
behavior; a mismatch is surfaced as `turfSplitPendingRestart`. Dashboard
verification fields include `turfSplitEffective`, `familySupplyOrigin`, and
`bootBaselineHunterMeat` so the discounted miner bank can be distinguished from
new hunter production.

## P.8.2 — Mission-terminal hunting

P.8.2 is independently gated by `pveConfig.missionHunt.enabled` (default
`false`). When off, the existing P.8.1c/d hunter path remains
`BUFF_UP → TRAVEL_OUT → AWAITING_WORLD → HUNTING` and no terminal or lair work
is performed.

When enabled, a hunter visits the nearest resolved mission terminal in its home
city, dwells for the configured acceptance interval, and the manager creates a
non-persistent real lair at a bounded, spawnability-checked wilderness point.
The controller then travels to that lair and scans locally. A pending terminal
city is retried per city for `terminalResolveWaitCycles`; only that hunt then
falls back to a hunt-ground-adjacent spawn. A ready city with no terminal is
`absent` and takes the same adjacent-spawn fallback. The shared terminal state
is changed only by the post-load city scan.

The mission phases are `TRAVEL_TO_TERMINAL`, `ACCEPT_MISSION`,
`TRAVEL_TO_LAIR`, and `MISSION_CLEANUP`. Combat remains one-target-at-a-time.
For social/herd targets, the hunter's defender count is compared with
`maxSimultaneousAdds`; an over-cap pull uses the existing retreat/heal path and
repeated over-cap cycles abandon the lair. Completion, abandonment, death, and
timeout queue lair destruction off the hunter tick.

`pveActivity.roster` exposes the current phase, terminal target, lair OID and
position/alive state, and engaged adds. Root counters expose
`missionLairsSpawned` and `missionLairsCleaned`, alongside the existing live
body position and hunter kill/harvest counters. Temporary target-scan logging
is not part of the controller; any future diagnostics should use the manager
logger so they are visible in `core3.log`.

## P.8.3 — Hunter combat and movement realism

The P.8.3 realism pass keeps the hunter economy loop simulation-only while
making the in-world activity read like player activity. Hunter combat uses the
real mutual-combat entry point and the explicitly equipped rifle; creatures
can retaliate, so the existing retreat, clone, and wound-recovery paths remain
part of the live acceptance check.

Hunter travel is an opt-in hybrid controller mode. `SimHunterController` opts
in through `usesNavmeshHybridMovement()`; miners and PvP controllers retain
the base false default. The mode is latched from the agent's current
`isInNavMesh()` state with a small debounce. On-mesh legs use the boolean
provenance returned by `getRecastPath` and preserve navmesh node heights. A
city-to-wilderness leg resolves and ground-snaps its exit boundary, validates
an actually off-mesh egress point, then switches to terrain-following
overland movement. A navmesh failure retries within the bounded
`missionHunt.navmeshRepathTries` budget and never falls back to in-city
overland movement.

Phase announcements now broadcast the controller's phase-specific detail
string as-is. The configured site key is only humanized for an empty-detail
fallback, so identifiers such as `tatooine_womprat_meat` never get concatenated
onto a return message. Hunter buffs explicitly fill the current HAM pools to
their modified maxima, and each refresh removes the prior CRC before adding
the replacement effect.

The read-only `#/wilds` dashboard route renders `pveActivity.roster[]` live
positions, phase, planet, and copy-paste `/way x y z` coordinates. Missing
coordinates are shown as unavailable during body or zone transitions; the
route adds no server mutation or new data source.

### P.8.3 live-verification hardening

In-world testing surfaced several combat-realism gaps that were fixed on the
branch (still simulation-only for loot):

- **Two-way combat requires `ATTACKABLE`.** A creature only retaliates against a
  target its `isAttackableBy` accepts, and that check rejects any
  `pvpStatusBitmask` without `ObjectFlag::ATTACKABLE`. Hunter bodies are spawned
  `PLAYER | ATTACKABLE` so wildlife fights back. To keep the neutral bot from
  becoming player-attackable, `AiAgentImplementation::isAttackableBy` returns
  false for a real player attacking a faction-0 `getSimPlayerBot()` — mirroring a
  neutral player — while creature/NPC AI still falls through to the normal
  faction-0 rules. It reads blue and is not hover-attackable, yet wildlife
  engages it.

- **Self-defense against non-target attackers.** The hunter previously fought
  only its scan-acquired mission target, so anything else that attacked it (an
  interceptor mid-travel, or a creature that aggros at the lair before a target
  is acquired — `scanForTarget` early-returns while in combat) went unanswered
  and it died passively. `SimHunterController::onTick` — the arrival-cadence hook
  that stays live through both travel and the lair stand — now, whenever the bot
  is in combat with no live acquired target, fights whatever is actually
  attacking it (`defendAgainstInterceptor`). It never moves the bot and steps
  aside once a real mission target is engaged, so the active-tick mission combat
  is unchanged.

- **Combat cadence.** Attack timing is driven by the AI behavior tree, which
  otherwise sits on the long idle `Wait` schedule and only fires opportunistically
  (a target could be "aimed at" but unshot for minutes). Both engage paths now
  call `activateAiBehavior(true)` after establishing combat, matching the working
  PvP controller, so the weapon fires promptly and sustained combat
  self-reschedules.

- **Buff magnitude and body template.** Hunter enhancement buffs were correct but
  set to a token `+100`; they now apply a doctor/entertainer-tier `+2500` so the
  HAM change is meaningful. The body template moved off `artisan` (no combat
  skills or attacks) to a real combat template with matching ranged attack maps
  and much higher HAM/resists; the equipped rifle is aligned to that template's
  attack family, and the C++ spawn still overrides faction to 0 so the neutral
  hunter attacks and is attacked by wildlife regardless of the template's origin.

### P.8.4 combat-targeting hardening

Two owner-observed hunter combat bugs shared a target-state gap. A faction-0
hunter could damage a nearby real player with an area attack because the player
side of `CreatureObject::isAttackableBy` had no sim-bot exception; the existing
reverse guard only stopped a real player from attacking the neutral bot. The
player-side predicate now mirrors `AiAgentImplementation::isAttackableBy` and
rejects a faction-0 `getSimPlayerBot()` attacker. This shared chokepoint covers
direct attacks and area-of-effect splash victims while leaving factioned PvP
sim bots and normal NPCs unchanged.

The controller now selects the nearest qualifying creature from the hunter's
defender list, excluding players, sim-presence bodies, other sim bots, dead or
unreachable creatures, and anything the hunter cannot attack. The defender list
and current follow target are snapshotted under the hunter lock (add/remove
defender are `@preLocked`) before filtering, so a concurrent defender removal
cannot cause an out-of-bounds read. A bounded follow-target hysteresis prevents
thrash without allowing a fleeing first target to mask a materially nearer
attacker.

Combat-target ownership follows a single-writer rule so the two independently
scheduled tick loops (the arrival-cadence `onTick` and the `SimHunterActiveTickTask`
`runActiveTick`) cannot race `targetOid`/the mission observer. `targetOid` is
HUNTING-scoped (only `selectTarget` sets it, and only during HUNTING). The HUNTING
active tick is the sole writer: it runs the full dispatcher where a species-valid
attacker is promoted through the mission-target/observer path (so its kill credits
the quota) and an off-species interceptor is fought transiently without changing
the mission target. `onTick` defers entirely to the active tick in HUNTING and, on
the travel legs, only self-defends via the interceptor path — it never promotes a
mission target, so it never writes `targetOid`/the observer. A consequence is that
a species creature killed incidentally on a travel leg is fought as an interceptor
and does not credit the mission quota; lair aggro (HUNTING) is still promoted and
credited by the active tick. Before retargeting, the dispatcher also waits out a
just-killed mission target whose asynchronous destruction handoff is still pending,
so a legitimate kill is not lost to an early `targetOid` advance.

If no valid attacker remains, bilateral defender cleanup removes stale
relationships from both sides while preserving a live mission target and its
observer, or fully clears stale combat when no mission target remains. This
keeps combat real and player-safe without adding inventory, credit, market, or
persistence mutation.

## P.8.6 — Player-mimetic real buffs (F_0.4.6)

The `BUFF_UP` placeholder (loiter at a cantina/med-center point, then stamp
synthetic config `Buff`s every mission cycle) is replaced with real, need-gated
buffs obtained from the owner's in-world buffer NPCs. All of it sits behind
`pveConfig.realBuffs.enabled` (default **off**); with the gate off the legacy
synthetic `applyHunterBuffs`/`pveConfig.buffs` path is byte-for-byte unchanged.

**Need detection.** Before the detour, `computeBuffNeeds` snapshots the hunter's
buffs under its lock and compares nine tracked CRCs (six medical HAM enhances +
dance-mind + music focus/willpower) against `reapplyThresholdSeconds` (default
900s / 15 min). A buff absent or below the threshold marks its family
(`needDoctor` / `needEntertainer`) needed; if neither is needed the hunter skips
straight to the mission leg (the recycle is gone). One authoritative CRC source
(`pveTrackedBuffCrcForAttribute` → `BuffCRC`) is shared by the need check, the
`realBuffs.fallbackBuffs` synthetic set, and the real providers, so a synthetic
fallback stamps exactly the CRC the need check and the real buffs use.

**Provider resolution.** `resolvePveBuffProviders` finds the Doctor/Musician/
Dancer buffer NPCs near the hunter's home-city med center and cantina via
`getInRangeObjects` (locks released, `scanForTarget` discipline), keying on the
role-specific custom name (Musician and Dancer share the `entertainer` template,
so the name is the discriminator; template/performance state are validation).
It returns each provider's OID, a strong `CellObject` reference, cell id, world
position, and cell-local position, with mission-terminal-style pending/absent
guards. A home city with no buffer NPC (e.g. Bestine) resolves absent → synthetic
family fallback.

**Interior approach.** Providers stand inside building cells. Because hunters use
the navmesh/overland hybrid mover — whose path pipeline discards cell targets — a
leg-scoped `interiorApproachLeg` latch makes every `usesNavmeshHybridMovement()`
site treat the buff-approach leg as non-hybrid (`isHybridMovementActive()`), so
that one leg routes through the base cell-aware path (submission, result,
arrival, and stuck re-path). Miners, PvP, and the hunter's wilderness legs are
unchanged (the latch is false for them).

**Real interactions.** Entertainers: the controller issues the real
`PlayerManager::startListen`/`startWatch` (owner-patched to accept the
`entertainer` NPC and any `CreatureObject` watcher; `isValidAudienceMember` also
accepts a verified sim bot), firing the genuine `WASLISTENEDTO`/`WASWATCHED`
observers → real performance buffs. Doctor: the hunter emits visible "I need a
buff" spatial chat, and the controller drives the doctor's own negotiation via a
`ScreenPlayTask` into `SmartDoctorBuffer:botBuffRequest` (a bot's chat cannot
reach the player-only `SPATIALCHATSENT` observer, so the handler is invoked
directly). Sim bots auto-confirm on entering NEGOTIATING (initial request and
every queue promotion), the 5k charge is bypassed, and the up-front
`wipeMedicalBuffs` is skipped in favour of a per-step refresh
(`healEnhanceCreatureTarget` now removes just its own CRC then re-applies), so an
interrupted session never leaves a pool stripped-and-unfilled.

**Request token + cancellation.** The controller owns a monotonic per-hunter
generation and an absolute deadline, passed as `<botOid>:<generation>:<deadline>`.
The token is persisted with the doctor's current target so it survives the
thread-local screenplay Lua states that run the deferred steps; `botCancel`
compares against that persisted generation so a stale cancel cannot abort a newer
request. On timeout the controller cancels and falls back. (Doctor *queue*
membership remains in-memory, inherited from the owner screenplay; bot queue
contention degrades safely to synthetic fallback rather than stranding a hunter.)

**Wounds.** Because the no-strip doctor path skips the wipe that used to heal
wounds, `finishPveBuffProviderFlow` clears all HAM wounds + shock after the
heal-up stop (real or fallback) — clones always need the doctor, so their
respawn wounds are cleared.

**Fallback.** Any absent/timed-out provider falls back to
`applyHunterBuffsForFamily` over `realBuffs.fallbackBuffs` (never the legacy
list), so a hunter is never left unbuffed.

**Simulation safety.** Buffs and wound clears apply only to sim-bot bodies; the
credit bypass is scoped to `getSimPlayerBot()` (real players still pay and are
never buffed for free). No inventory/market/persistence mutation.

**Live verification (owner restart with `realBuffs.enabled=true`).** Dashboard
`pveActivity.realBuffs` counters climb (`doctorInteractions`, `dancerWatches`,
`musicianListens`, `buffDetoursSkipped`, `syntheticFallbacks`) and roster rows
show `medicalBuffed`/`entertainerBuffed` with `lastBuffSource=real-doctor`/
`real-entertainer`; a fresh-buffed hunter skips the detour; a Bestine hunter uses
`syntheticFallbacks`; an interrupted doctor session leaves processed medical pools
fresh and unprocessed pools untouched; `hunterKillsTotal` keeps climbing.
