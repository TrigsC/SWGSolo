# P.6 - AI PvP Gameplay: SimPvP Squad Rework, Scouts, Player Comms, GCW Presence

Status: **P.6.1a SHIPPED** (2026-07-05, compiled clean `-Werror`, **PENDING
RESTART + RE-VERIFY** - see §11 for the live findings it fixes). The P.6.1
first live soak (113 min) proved the architecture (squads formed, 26 travels,
teleports landing exactly on pads, rosters intact, zero deaths/wipes/orphans,
names + player dots correct) but found 2 real bugs, fixed in P.6.1a. Owner
approved all §9 recommendations: bot-vs-bot ON after the soak (shipped
default-off, flip `allowBotVsBotCombat` at runtime), 2 squads/faction × 4,
GCW v1 = combat presence only. P.6.2+ not started.
Owner request: replace the legacy solo PvP-bot loop with a robust, feature-flagged
foundation for player-mimetic PvP squads; then scouts/gank convergence, player-facing
communication (no new client commands), and staged GCW base attack/defense.

Related docs: `ai-miner-navigation-design.md` (P.4 travel primitives this reuses),
`ai-hive-inventory-design.md` / `ai-crafter-output-ledger-design.md` (P.5).

---

## 0. Vision mapping

PvP players do two things this system mimics:

1. **Starport loops** - groups of 1–10 (up to a full group of 20) shuttle into main
   cities (Theed, Mos Eisley, Coronet), run shuttle→starport looking for attackable
   enemies, post up for a while, then move on. Sometimes 1–2 **scouts** run the loop
   and the main group converges ("gank") when PvP breaks out.
2. **Base battles** - factional PvP bases flip planet control; they go vulnerable at
   known times and full groups stage/attack while defenders respond.

End goal: players consistently *see and hear* this going on, and can join in - via
group chat callouts from NPC squad leaders, and by joining NPC squads. Constraint:
**no new client commands** (TRE changes) - integrate spatial chat keywords, the normal
`/join` flow, chat rooms, and mail only.

---

## 1. Current state (legacy loop, fully traced)

### 1.1 What exists

- Lua (`bin/scripts/managers/sim_player_manager.lua`):
  - `shuttleports` - per-planet list `{name, spawn={x,y,z}, hangout={x,y,z}}`
    (4 cities: moenia, theed, coronet, mos_eisley). `spawn` = shuttleport pad,
    `hangout` = starport approach.
  - `spawnGroups` entry `{type="pvp_solo", totalCount=0, templates={"rebel_trooper",
    "stormtrooper"}, minStaySeconds, maxStaySeconds}` - totalCount=0 is the only gate.
- `SimPvPController` (`src/server/zone/objects/creature/simplayer/SimPvPController.cpp`,
  380 lines): solo bot; `startSimLoop` sets faction + `setPvpStatusBitmask(OVERT|
  ATTACKABLE)` then `moveTo(hangout)`; `onArrived` → loiter 30–180s (`SimPvPBehaviorTask`);
  `onTick` (1s, driven from `SimPlayerController::checkArrival`) → dead ⇒ 15s-delayed
  recycle; else `scanForTargets()` - CloseObjectsVector CREOTYPE scan, **enemy-faction
  players only**, <40m, outdoors, `isAttackableBy(agent)` ⇒ setTargetObject/addDefender/
  setCombatState; `finishLoitering` → `returnToShuttle` → `requestCycleToNextStop`.
- Manager side (`SimPlayerManager.cpp`):
  - `spawnFromConfig` (:20545) spawns at shuttleport, wires controller + cycle context.
  - `cyclePvPBotWhenShuttleReady` (:20641) - polls `isNearestShuttleBoardable` (:20739,
    `getNearestPlanetTravelPoint(creature,128)` + `checkShuttleStatus`) every 5s, ~2min
    timeout.
  - `cyclePvPBot` (:20767) - picks a different random shuttleport, **spawns a brand-new
    bot there and destroys the old one** (world+database).
  - `toggleBot` (:~6057) - admin path; infers faction from template name, default route.
  - Dashboard: only `activePvpBots` count + `pvpStatus:"experimental"`.

### 1.2 Confirmed defects / legacy smells

1. **Dual movement drivers.** PvP bots call `setCustomAiMap("patrol".hashCode())` but
   **no "patrol" entry exists** in `customMap` (`bin/scripts/ai/templates.lua`) - the
   lookup falls through to the default trees: `idleDefault` walks the controller's
   patrol queue at WALK and `GeneratePatrol` wanders the bot ~25m off its post. This is
   the exact class of bug that broke the miners (fixed there with the no-op `simMiner`
   map). Explains the "mostly works" flakiness.
2. **Player-dot wipe.** `spawnFromConfig` applies `applySimNpcPresentation(agent,
   ATTACKABLE|OVERT)` (adds `ObjectFlag::PLAYER` 0x10 so clients render a player-style
   dot, SimPlayerManager.cpp:13920), then `SimPvPController::startSimLoop` re-sets the
   bitmask to `OVERT|ATTACKABLE` **without** the PLAYER bit - presentation lost.
3. **Identity destroyed every hop.** Cycling = destroy + respawn elsewhere. Blocks
   squads (roster continuity), leader persona, and any player-facing communication.
   Also risks the orphan-style bugs we've fought before - two object lifecycles per hop.
4. Hardcoded Moenia route in the constructor; raw `SimPlayerManager*` back-pointer;
   duplicated context "self-heal" blocks in `onArrived`/`onTick`; magic numbers (40m
   scan, 3.0 runSpeed, 15s recycle, 30–180s loiter) not in config; no dashboard detail;
   no recovery/watchdog integration at squad level.

**Decision: rewrite `SimPvPController` and remove the destroy/respawn cycle** in favor
of persistent bots that *travel* (same primitives as P.4.5). No legacy code retained.

---

## 2. Engine building blocks (verified, with sources)

| Capability | Verified mechanism |
|---|---|
| Squad formation | GCW `spawnSecurityPatrol` (`managers/gcw/GCWManagerImplementation.cpp:820`): leader gets `ObjectFlag::SQUAD`, members get `ObjectFlag::FOLLOW` + `setFollowObject(leader)` + `setMovementState(FOLLOWING)` - engine-native following, proven in live GCW patrols. `SquadObserver` = thread-safe roster + despawn helpers. |
| Per-slot AI override | `AiMap::getTemplate` (`managers/creature/AiMap.h:313`): a custom map overrides only the slots it defines; all other slots fall back to bitmask defaults. ⇒ a `simPvp` map can no-op IDLE (kill wander) while inheriting default ATTACK/TARGET/AWARE combat trees. |
| Intra/inter-planet travel | P.4.5a/b: run to `PlanetTravelPoint` departure, `isNearestShuttleBoardable` wait, `switchZone(zone, X, Z, Y, 0)` to OUTDOOR arrival - proven, no orphan risk. `PlanetManager::getNearestPlanetTravelPoint` (`PlanetManagerImplementation.cpp:699`). |
| Runtime config | `refreshMinerPlanetDispatchConfig` pattern (`SimPlayerManager.cpp:13518`): re-run the lua each task interval → toggles apply without restart. |
| NPC speech | `ChatManager::broadcastChatMessage(npc, msg, …)` (`chat/ChatManagerImplementation.cpp:1045`) - works for any CreatureObject; screenplays already use `spatialChat(npc, …)`. |
| NPC → player mail | `ChatManager::sendMail(senderName, header, body, playerFirstName)` - sender is a plain string; no object needed. |
| NPC-led groups | `GroupManager::inviteToGroup(npcLeader, player)` works (sets the player's `groupInviterID`, sends "@group:invite_target"). **But `joinGroup` (`managers/group/GroupManager.cpp:185`) rejects non-player inviters** (`!object->isPlayerCreature()`) - needs a small gated relaxation for sim bots. `GroupObject::addMember` accepts any CreatureObject (pets use it); NPC can hold leadership. |
| NPC posts in group chat | Group chat room broadcast takes a forged `ChatRoomMessage(senderName, galaxy, text, roomID)` - no player sender object required (`ChatRoom::broadcastMessage`). |
| Reading player text | Spatial: `SPATIALCHAT` observer fires on an AiAgent for player speech within 25m (`ChatManagerImplementation.cpp:1215`), payload = message text + player OID. Group chat: **no observer exists** - `handleGroupChat` (`ChatManagerImplementation.cpp:1594`) would need a small gated hook. |
| GCW bases | `gcwBaseList` per-zone on `GCWManager`; `isBaseVulnerable` (:1240); timing via `DestructibleBuildingDataComponent` (`nextVulnerableTime`/`vulnerabilityEndTime`); vulnerability window = `vulnerabilityDuration` 3h every `vulnerabilityFrequency` 48h (`bin/scripts/managers/gcw_manager.lua`). **All 5 destruction steps (uplink band, jam, security slice, DNA, power regulator) are player-SUI-gated** - no safe programmatic path for NPCs to advance base state. |

---

## 3. Architecture

### 3.1 Squad model (manager-side)

New clearly-delimited "SimPvP" section in `SimPlayerManager` (matching how all other
subsystems live there), holding a `Vector<SimPvpSquad>`:

```
struct SimPvpSquad {
    uint64 squadId; String faction;            // "imperial" / "rebel"
    uint64 leaderOid; Vector<uint64> memberOids;
    int desiredSize;                            // 1..20 (solo = 1)
    String role;                                // "patrol" | "scout" (P.6.2)
    String currentPlanet, currentCity;
    int state;                                  // squad state machine (below)
    uint64 stateSinceMs, loiterUntilMs;
    int deaths, kills, travels;                 // dashboard counters
    uint64 lastContactMs; String lastContactCity; // P.6.2
};
```

Squad state machine (leader-driven):
`FORMING → MOVING_TO_HANGOUT → LOITERING → MOVING_TO_SHUTTLE → WAITING_FOR_SHUTTLE →
BOARDING(switchZone all members) → arrive next city → MOVING_TO_HANGOUT → …`
Combat pauses the machine (existing checkArrival already yields while `isInCombat`).

### 3.2 Controller rework

- **`SimPvPController` rewritten** (leader role): owns route movement exactly like
  today's controller but with states above, config-driven loiter/scan values, no
  destroy/respawn - on cycle it *runs back to the shuttleport, waits for the shuttle*
  (reuse `isNearestShuttleBoardable` polling as a scheduled task, not busy-wait), then
  the manager `switchZone`s the whole squad to the next city's shuttleport OUTDOOR
  point and the loop continues. Anti-stuck: the existing stuck-watchdog escalates to
  "board anyway from current position" (same `stuckFallback` contract as P.4.5b).
- **Members**: spawned with the leader, `ObjectFlag::FOLLOW` creature-bitmask +
  `setFollowObject(leader)` + `FOLLOWING` (the proven GCW patrol pattern) - engine owns
  member movement; **no controller-issued moveTo for members** (no dual-driver by
  construction). A lightweight `SimPvPMemberController` handles only: target scan
  participation, death detection, and re-follow after combat/teleport.
- **AI map**: new `simPvp` entry (`bin/scripts/ai/simPvp.lua` + `templates.lua`):
  overrides IDLE with the no-op Wait (kills `GeneratePatrol` wander); NONE/ATTACK/
  TARGET/AWARE etc. inherit defaults so bots actually fight. (Verify in P.6.1 that
  default AWARE doesn't fight the controller when out of combat; if it does, override
  AWARE too, exactly as diagnosed for miners.)
- **Presentation fix**: controller never touches `pvpStatusBitmask`; faction +
  `applySimNpcPresentation(agent, ATTACKABLE|OVERT)` (PLAYER dot preserved) applied
  once at spawn by the manager.
- **Target scan**: config-driven radius; enemy-faction players (as today) and, behind
  `allowBotVsBotCombat`, enemy **sim bots** (`isSimPlayerBot()` + enemy faction +
  `isAttackableBy`) so opposing squads visibly fight at starports. Locking mirrors the
  existing scan (agent lock, cross-lock target).
- **Death**: member dies → corpse remains (normal loot/decay), squad continues; the
  squad "recruits a replacement" only at its **next city arrival** (fresh spawn at the
  shuttleport joins the roster - reads as a new player shuttling in). Leader dies →
  senior member promoted (roster order); if the whole squad wipes, it re-forms at a
  random city after `respawnDelaySeconds`. Cleanup of dead bots stays delayed (≥15s)
  and never runs while a player is looting.

### 3.3 Recovery

`SimPvpRecoveryTask` mirroring `minerRecoveryConfig` semantics: detects (a) member
farther than N m from leader while not in combat → re-follow, then (gated) teleport to
leader; (b) squad stuck in a state past a TTL → force next state / re-form at next
city; (c) orphaned controller (agent gone) → drop + replace at next arrival. All
actions rate-limited + logged, dry-run flag first, exactly like miner recovery.

---

## 4. Phases (each: build clean `-Werror`, owner restarts, verify on dashboard)

### P.6.1 - Core rework: squad starport loops  ← first build
Scope: everything in §3, one squad type ("patrol"), both factions. Removes
`cyclePvPBot`/`cyclePvPBotWhenShuttleReady`/solo constructor route; `toggleBot` now
forms a solo squad (size 1). Extends lua `shuttleports` per-city with optional
`hangouts = { {x,y,z}, … }` (multiple posts; pick randomly) while accepting the old
single `hangout` key.
Verify: squads run loops across all configured cities; travel = run-to-shuttle → wait
→ appear at next city's pad; PLAYER dot visible; zero stuck squads over a multi-hour
soak; scan engages an overt enemy player; dashboard `pvpActivity` populated; miners
unaffected.

### P.6.2 - Scouts + gank convergence
Scout squads (size 1–2) run the same loop with a wider scan that only *reports*:
on enemy player contact → `SimPvpContactReport{city, planet, faction, count, ageMs}`
to the manager → nearest idle patrol squad of that faction travels there (existing
travel), converges on the contact hangout. Cooldowns per city + per squad; contacts
expire. Verify: staged contact (owner's overt char) pulls a squad within N minutes;
no ping-pong thrash between cities.

### P.6.3 - Player-facing communication (sub-gated, in order of increasing risk)
- **a. Spatial announcements** (no core changes): leader speaks on state transitions -
  arrival ("Squad posting up at the Theed shuttle - eyes open."), contact, departure.
  Rate-limited per squad + global. `broadcastChatMessage`.
- **b. Faction rooms** (no core changes): persistent chat rooms (e.g. `GCW.Rebel` /
  `GCW.Imperial`) created at startup; leaders post arrivals/contacts/base callouts as
  forged `ChatRoomMessage`s; players join the room from the normal chat-room browser.
  This is the "pretty consistently informed" channel.
- **c. Join-a-squad + group-chat commands** (two small gated core patches):
  1. `GroupManager::joinGroup` - allow the inviter to be a sim bot (flag-gated check:
     `isAiAgent() && asAiAgent()->isSimPlayerBot()` alongside `isPlayerCreature()`),
     so the stock invite → `/join` flow works with an NPC leader. NPC squads convert
     to a real `GroupObject` when the first player joins.
  2. `ChatManagerImplementation::handleGroupChat` - after normal delivery, if the
     sender's group leader is a sim bot, forward `(senderOid, text)` to
     `SimPlayerManager::onPvpGroupChat` (keyword intents: `status`, `where`, `next`,
     `attack <city>`, `wait`, `leave`). Free-text keywords only - **no new client
     commands anywhere**.
  - Entry point without any patch: player says "invite" near the leader (25m
    `SPATIALCHAT` observer) → leader invites → player `/join`s (needs patch 1 only).
  - Leader replies in group chat via forged room messages; on member death/contact it
    calls out ("Rebs at Coronet starport, 3 strong - converge!").
Verify: player joins/leaves cleanly; group disbands safely when squad travels
(players are NOT teleported - leader announces destination and waits at the arrival
city per `groupRegroupWaitSeconds`); no chat spam beyond limits.

### P.6.4 - GCW base presence (combat only, no base-state mutation)
`SimPvpGcwWatchTask` (interval ~300s) enumerates each zone's `gcwBaseList` snapshot:
for a base inside/near a vulnerability window (lead time config), dispatch an
**attacker squad** of the opposing faction and (if the owning faction has an idle
squad) a **defender squad** to outdoor staging points near the base; they fight
whatever's attackable (defenders, players, each other under the bot-vs-bot flag) for
the window, then resume city loops. Faction-room callouts ("Imperial base on Naboo
vulnerable - staging attack") make it a player magnet from P.6.3b. **v1 never touches
base state** - all 5 destruct steps stay player-driven (they're SUI-gated anyway); an
"NPCs can progress the destruct sequence" simulation would be a separate owner-approved
phase. Verify: squads appear at a vulnerable base, fight, leave at window end, bases
unharmed by NPCs.

---

## 5. Feature flags (lua `pvpConfig`, C++ defaults ALL off/false)

```lua
pvpConfig = {
    enablePvpBots = false,          -- master gate (P.6.1); nothing runs when false
    squadsPerFaction = 1,           -- patrol squads per faction
    squadSize = 4,                  -- members incl. leader (1 = solo)
    scanRadiusMeters = 40,
    loiterMinSeconds = 60, loiterMaxSeconds = 180,
    runSpeed = 0,                   -- 0 = template default (no speed hack)
    allowBotVsBotCombat = false,    -- opposing sim squads fight each other
    respawnDelaySeconds = 120,      -- full-wipe squad re-form delay
    recovery = { enabled=true, dryRun=true, memberFarMeters=64,
                 stateTtlSeconds=600, maxActionsPerInterval=2 },
    scouts = { enabled=false, squadsPerFaction=1, reportOnly=true,   -- P.6.2
               convergeCooldownSeconds=600, contactTtlSeconds=300 },
    comms = { spatialAnnouncements=false, announceCooldownSeconds=90, -- P.6.3a
              factionRooms=false,                                     -- P.6.3b
              playerGrouping=false,      -- gates the joinGroup relaxation (P.6.3c-1)
              groupChatCommands=false,   -- gates the handleGroupChat hook (P.6.3c-2)
              groupRegroupWaitSeconds=120 },
    gcw = { basePresence=false, leadTimeSeconds=900,                  -- P.6.4
            maxSquadsPerBase=1, checkIntervalSeconds=300 },
    -- routes reuse the existing `shuttleports` table (+ optional hangouts list)
}
```

Runtime refresh via the dispatch-config pattern; `spawnGroups.pvp_solo` is removed
(replaced by `pvpConfig`), with a startup warning if the old key is still present.

## 6. Core-file touchpoints (only two, both default-off)

1. `GroupManager.cpp::joinGroup` - one condition widened, guarded by
   `SimPlayerManager` flag (`comms.playerGrouping`). Zero behavior change when off.
2. `ChatManagerImplementation.cpp::handleGroupChat` - post-delivery, flag-guarded
   forward to the manager. Zero behavior change when off.

Everything else lives in `simplayer/` + lua + `templates.lua` (one new customMap
entry, additive).

## 7. Safety analysis

- **Simulation posture**: no economy/persistence mutation; bots remain transient
  AiAgents (persistence 0), destroyed via the existing delayed-cleanup path.
- **Locking**: combat engage keeps the existing agent-lock + cross-lock pattern; group
  ops follow GroupManager's documented pre/post lock contracts; squad roster in a
  mutex-guarded structure like `SquadObserver`; no lock held across `switchZone`.
- **Travel**: only the proven `switchZone(zone, X, Z, Y, 0)` outdoor reposition - never
  interiors, never RIDER slots, no vehicle lifecycle.
- **Players**: never teleported, never force-grouped; joining is always player-initiated
  (`/join`); chat rate-limited (per-squad + global caps); mail not used for broadcast
  (opt-in faction rooms instead).
- **Reversibility**: master flag off ⇒ subsystem inert; each phase independently
  gated; the two core patches are single-condition, flag-guarded diffs.
- **Blast-radius**: miners untouched (separate controller classes; shared base class
  changes limited to additive virtuals).

## 8. Dashboard (`pvpActivity` section)

`enabled`, per-squad rows (faction, role, size, alive, state, planet:city, stateAge,
kills/deaths/travels), contacts (P.6.2), comms counters (announcements, roomPosts,
playersGrouped) (P.6.3), gcw rows (base, window, squadsDispatched) (P.6.4), recovery
counters. Same JSON-builder style as `stationTravel`/`planetDispatch`.

## 9. Owner decisions (all approved 2026-07-04; #4 still open for P.6.3)

1. **Bot-vs-bot combat** - recommend ON (after P.6.1 soak): it's the visible "PvP is
   happening" signal with zero players online. Default-off flag either way.
2. **Squad sizes/counts to start** - recommend 2 squads/faction × 4 members on the
   existing 4 cities.
3. **P.6.4 v1 = combat presence only** (no NPC-driven base destruction) - recommend
   yes; an NPC destruct-sequence simulation is a later, separately-approved phase.
4. **Faction room names** + whether scout contact reports also go to the room.

## 10. P.6.1 implementation notes + verify checklist (added at ship time)

**As-built (deviations from §3/§4 are minor):**
- Controllers (`SimPvPController.h/.cpp`, full rewrite): shared
  `SimPvpBotController` base (faction, 1s-tick death report → manager, target
  scan incl. gated bot-vs-bot via `getSimPlayerBot()`), `SimPvPController`
  leader (phases FORMING/TO_HANGOUT/LOITERING/TO_SHUTTLE/AWAITING_SHUTTLE;
  `startSimLoop()` re-drives the current phase so the base path-fail retry
  resumes it; 3-strike `pathFailStreak` → `forceAdvancePhase` = post up where
  it is / board from where it is), `SimPvPMemberController` (never moveTo;
  engine FOLLOW owns movement; per-tick `assertFollow()` self-heal).
- AI maps: leader = new `simPvp` customMap entry overriding **IDLE only**
  (no-op Wait; root/ATTACK fall back to defaults so bots fight - this differs
  from simMiner, which also no-ops the root). Members = `creatureBitmask
  FOLLOW` + `customAiMap 0` + `formationOffset` blackboard + cross-locked
  `setFollowObject`/`FOLLOWING` (exact GCW spawnSecurityPatrol recipe).
- Manager (`SimPlayerManager`): `SimPvpSquad` roster under `pvpSquadMutex`
  (never held while locking agents); `SimPvpMaintenanceTask` 30s (config
  refresh → despawn-if-disabled → population ramp (≤1 new squad/faction/tick)
  → wipe re-form → leader promotion (re-profiles the member to the simPvp
  map, `controllers.put` replaces its controller, others `setLeader`) → state
  TTL escalation → member-far recovery (re-follow always; switchZone-to-leader
  only when >2× far AND `recovery.dryRun=false`)); `SimPvpShuttleWaitTask`
  5s×24 (`isNearestShuttleBoardable`) then **board anyway**; boarding =
  switchZone leader+alive members to the next city's pad (+jitter) + fresh
  spawns refill dead slots; `SimPvpBotCleanupTask` destroys a corpse after
  60s (never destroys a live bot). Bots get NameManager human names.
- Removed legacy: `cyclePvPBot`, `cyclePvPBotWhenShuttleReady`,
  `spawnSimPlayerWithRoute`, `startControllerForAgent`, the phantom
  `"patrol"` customAiMap sets, the pvp branch of `spawnFromConfig` (+ a
  startup warning if a lua `pvp*` spawnGroup still has totalCount>0).
  `toggleBot` on a rebel/imperial template now forms a **solo squad** at the
  nearest configured city on its zone. `isNearestShuttleBoardable` kept.
- Dashboard: `pvpActivity` (totals + per-squad rows incl. leaderPhase/age);
  `population.pvpStatus` = "squads"/"disabled".
- Lua shipped: `enablePvpBots=true`, 2×4 squads, `allowBotVsBotCombat=false`
  (flip at runtime after soak), `recovery.dryRun=true` (observe first),
  `logStateTransitions=true` (verbose for verify).

**Verify after restart (dashboard `pvpActivity` + logs):**
1. 4 squads appear (2 imperial, 2 rebel), 4 bots each, distinct cities,
   player-style radar dots, human names, OVERT+ATTACKABLE.
2. Leaders cycle phases: movingToHangout → loitering → movingToShuttle →
   awaitingShuttle → `SimPvpSquadTraveled` log → next city; `travelsTotal`
   climbs; members arrive with the leader (FOLLOW formation).
3. No stuck squads over a multi-hour soak: `leaderPhaseAgeSeconds` never
   approaches stateTtl (600s) except while awaiting a shuttle;
   `boardAnywayTotal` stays low; miners unaffected.
4. Kill a bot (overt char): corpse persists ~60s then cleans up; roster
   refills at the next city; kill a leader → `SimPvpLeaderPromoted`; wipe a
   squad → `SimPvpSquadWiped` → re-form after 120s.
5. An overt enemy-faction player near a hangout gets engaged within ~40m
   (`playerEngagementsTotal`).
6. Then flip `allowBotVsBotCombat=true` (runtime, no restart) and watch
   opposing squads fight (`botEngagementsTotal`); flip
   `recovery.dryRun=false` once member-far behavior looks sane.

## 11. P.6.1 first live soak (2026-07-05, 113 min) + P.6.1a fixes

**What verified GOOD:** 4 squads (2/faction) spawned with human names +
player-dot bitmask (0x15); 26 travels with `boardedMembers=3 replacements=0`
every time; switchZone landings observed EXACTLY on destination pads (live
position sampling); phases cycling; 0 deaths/wipes/promotions/orphans; miners
unaffected; runtime config refresh working.

**Bug 1 - stale engine movement after interruption (the big one).** Live
position sampling showed every leader kilometers from its claimed city,
running dead-straight at ~4.8m/s toward the coordinates of a PREVIOUS city
(e.g. teleported to the Mos Eisley pad, then immediately ran a bearing that
extrapolates exactly to Moenia's pad - on Tatooine). Mechanism: the AiAgent's
own movement event keeps walking whatever patrol-queue/next-step state is
left over from an interrupted leg, regardless of controller work-loop
generations - and nothing stopped the agent at teleport time. Compounded by a
silent `CALCULATING_PATH` stall: after boarding, the fresh moveTo's path
request sometimes never resolves (no onPathFound, no onPathFailed, no retry
logs), so the correction never lands and the stale run continues until the
600s state TTL force-advances the phase (~20 TTL escalations in 75 min;
17/26 boardings were board-anyway timeouts because the leader was never
actually at the pad).
**Fix (P.6.1a):** hard-stop the agent at every interruption/teleport -
`clearPatrolPoints + clearSavedPatrolPoints + setMovementState(OBLIVIOUS)`
under the agent lock - in boardPvpSquad (leader + members, before
switchZone), in the recovery teleport, and in `forceAdvancePhase` (new
`haltAgentMovement()`; the TO_SHUTTLE/AWAITING case now also goes through
drivePhase's AWAITING branch so the generation/chain reset is uniform). Plus
a maintenance rescue: movement phase + `isAwaitingPathResult()` (new base
getter for state==CALCULATING_PATH) + age >90s → re-drive the phase
(`SimPvpPathRequestLost` log - watch for it to confirm/deny the lost-path
mechanism). onPathFailed now always logs squad/phase/streak.

**Bug 2 - uint64 TTL underflow at spawn.** `runPvpMaintenanceTask` captures
`nowMs` before spawning squads; a just-spawned leader's `phaseSinceMs` is
NEWER than nowMs, so `nowMs - phaseSinceMs` wrapped and the TTL forced the
first hangout leg 37ms after spawn (observed at t=94s). **Fix:** clamp the
age to 0 when `phaseSinceMs > nowMs`.

**Also:** boarding destination pick now prefers cities without a same-faction
squad (both imperial squads had piled onto Theed; the avoidance previously
applied only at initial spawn).

**Re-verify after restart (delta to §10):** leaders should now be AT their
hangouts/pads (spot-check with `/v1/object/{leaderOid}` positions vs city
coords); `stateTtl` force-advances should drop to ~zero; `boardAnywayTotal`
should fall well below travels (some timeouts are normal - shuttle landing
cycles); watch `SimPvpPathRequestLost` count; no forceAdvance within seconds
of `SimPvpSquadSpawned`; same-faction squads on distinct cities.

## 12. Second soak (2026-07-05, post-P.6.1a) + P.6.1b fix

**P.6.1a verified partially:** underflow TTL at spawn gone; halts firing;
rosters/travels healthy (147 travels, 0 deaths); squad 4 observed loitering
7m from its hangout (the good path works end-to-end). **But the stale-run
persisted** (~44% of legs still TTL'd; squads observed running from the new
pad on the exact bearing to the PREVIOUS city's pad). With `pathFailed=0` and
`SimPvpPathRequestLost=0`, the surviving mechanism is a **work-loop
generation race**: `workLoopGeneration` is bumped/read unsynchronized from
multiple threads, so a chain-thread IDLE-resume `moveTo(old destination)`
racing the boarding thread's `moveTo(new hangout)` can leave the OLD-pad path
task holding the CURRENT generation - the new path is dropped as stale, the
old path is accepted, and `onPathFound` overwrites `destination` with the old
pad (making every later check self-consistent with the wrong target).

**P.6.1b (compiled clean `-Werror`, PENDING RESTART):**
- `SimPlayerController::prepareForRelocation()` - generation bump + state
  WAITING + `destination=(0,0,0)` (disarms the IDLE-resume) + simPath clear;
  called in `boardPvpSquad` BEFORE the teleport and in `forceAdvancePhase`
  interruptions (loiter case re-schedules its own scan chain).
- New virtual `acceptFoundPath(pathEnd)` checked in `onPathFound` before any
  mutation: default true (miners unchanged - they rely on partial paths);
  `SimPvPController` override rejects a path ending >96m from the moveTo
  target and logs `stalePathRejected` → onPathFailed retry recomputes. This
  makes stale-path acceptance structurally impossible for PvP legs no matter
  who wins the race.
- Follow-up noted: the underlying unsynchronized-generation race in
  SimPlayerController affects miners too in principle (they've never
  triggered it - single-threaded moveTo callers); consider a mutex around
  moveTo's bump+capture in a later pass.

**Re-verify:** `stateTtl` ≈ 0 (except genuine shuttle-wait `boardAnyway`);
`stalePathRejected` count > 0 confirms the race diagnosis; leaders at pads/
hangouts on spot-checks; `travelsTotal` cadence ~1 per squad per 5-8 min.

## 13. Third soak (2026-07-05, post-P.6.1b): FIXES DID NOT WORK - diagnostics added

**P.6.1b did NOT fix it.** Live forensics on the running server (uptime ~30min):
`stalePathRejected=0` (my acceptFoundPath gate never fired) and `pathFailed=0`,
yet TTL force-advances continued at the same ~44%/leg rate. Decisive new
evidence from live position sampling of all 4 leaders:
- ALL four leaders walk in a **straight line at runSpeed (~4.8 m/s) AWAY from
  their phase target**, distance climbing monotonically (e.g. moenia squads
  500m→2740m over ~90s), then **stop OBLIVIOUS (movementState=0) ~3km out**
  while the phase stays `movingToHangout` (no arrival detected) until the 600s
  TTL fires.
- Squads 1 & 4 (both on the naboo:moenia leg) are **pixel-identical**
  (2260≈2261m, 2380≈2381m …) - the wrong movement is fully DETERMINISTIC, not
  a random race. Moenia squads walk due WEST from the pad (4963,-4892) to
  ~(1958,-4726); the hangout (4807,-4700) is ~250m NW, so this is not a
  near-miss.
- This invalidates BOTH prior hypotheses (stale-path-acceptance,
  generation race) - a rejected/raced path would show in the counters and
  wouldn't be bit-identical across squads.

**Leading hypothesis now:** the arrival-check tick chain dies (or the target
is computed wrong deterministically) so the leader walks one long straight
(2-node/direct) segment to a wrong point and stops without re-feeding patrol
points or detecting arrival. REST can't expose the controller's `destination`,
patrol queue, or home location, so this needs in-process logging.

**P.6.1c (diagnostics-ONLY, compiled clean, PENDING RESTART - no behavior
change):** three log-only instruments on SimPvPController, gated by
`pvpConfig.logStateTransitions` (already true):
1. `moveTo <label> from=(..) to=(..)` at each leg start - the exact target.
2. `pathFound accept=.. pos=.. pathEnd=.. destination=..` on EVERY path - what
   target the path was actually computed against.
3. `heartbeat pos=.. target=.. distToTarget=.. moveState=.. patrolPts=..` every
   ~8 ticks during movement phases - reveals chain-alive, direction, and
   whether patrol points are being fed.
**Next:** restart, let it run ~10min, read the SimPvpLeader logs - this will
pinpoint whether (a) moveTo gets a wrong target, (b) findPath returns a wrong
path, or (c) the tick chain dies mid-leg. THEN fix with certainty (no more
hypothesis-driven fixes). The stuck squads are cosmetic-only (no crashes, no
orphans, rosters intact, miners unaffected) so running with diagnostics is safe.

## 14. ROOT CAUSE FOUND (2026-07-05) + P.6.1d fix - the real bug

The P.6.1c heartbeat/path logs nailed it. For a broken leg the logs show the
CONTROLLER is 100% correct: `moveTo hangout to=(3467,-4890)` and
`pathFound accept=true pathEnd=(3467,-4890) destination=(3467,-4890)` - the
right target, the right path. Yet the heartbeat shows the agent marching the
opposite way, `patrolPts` pegged at 18, straight toward **(4963,-4892)** -
which is exactly the PREVIOUS leg's shuttle target (Moenia's pad), now chased
on the wrong planet. Working legs show `patrolPts` counting DOWN (4→3→1) to
arrival; broken legs show it pinned at 18.

**Mechanism (definitive, `AiAgentImplementation::findNextPosition`
:3800-3811):** the agent caches its A* route in `currentFoundPath`. While
`movementState == PATROLLING`, findNextPosition **reuses that cache without
re-checking it still matches the current patrol target** (the re-pathfind at
:3805 only covers FOLLOWING/PATHING_HOME/…). So the leftover route to the old
leg's target survives the teleport - `switchZone`, `clearPatrolPoints`, and the
new `onPathFound` all leave `currentFoundPath` intact - and the engine keeps
walking the stale cached path to the OLD destination on the NEW planet until it
"arrives" there (~3km away) and stops. Intermittent because it only bites when
a cached path existed at boarding time. This is why P.6.1a (halt patrol queue)
and P.6.1b (stale-path gate on the CONTROLLER's path) both missed - neither
touched the AGENT's `currentFoundPath`.

**P.6.1d fix (compiled clean `-Werror`, PENDING RESTART):** call
`AiAgent::clearCurrentPath()` (nulls `currentFoundPath` → forces a re-pathfind
to the current patrol[0]) at every point the destination changes:
1. `SimPlayerController::onPathFound` - right after clearPatrolPoints/
   clearSavedPatrolPoints, BEFORE queueing the new path. This is the primary,
   general fix: any new controller path now invalidates the agent's stale
   cache. Benign for miners (just forces one re-pathfind when the path changed).
2. Relocation halts (boardPvpSquad leader + members, recovery teleport,
   `haltAgentMovement`): reordered to `setMovementState(OBLIVIOUS)` FIRST (so
   `clearPatrolPoints` doesn't stash the queue into savedPatrolPoints), then
   clear patrol + saved + `clearCurrentPath()`.

**Re-verify after restart:** heartbeat `distToTarget` should DECREASE toward 0
each leg; `patrolPts` should count down to arrival; `stateTtl` force-advances
≈ 0; squads reach hangouts/pads and loiter. Once confirmed, set
`logStateTransitions=false` to silence the heartbeat and flip
`allowBotVsBotCombat=true` + `recovery.dryRun=false` for the real soak.

## 15. P.6.1d VERIFIED LIVE (2026-07-05) - movement fixed; bot-vs-bot working

Post-restart with the currentFoundPath fix + `allowBotVsBotCombat=true`:
- **Movement bug FIXED.** Heartbeats now show `distToTarget` counting DOWN to
  arrival (e.g. squad 6: 200→184→…→45, patrolPts draining 16→0). Recent-window
  tally: **14 loiterComplete (reached hangout) vs 1 stateTtl** - down from ~44%
  of legs timing out. `stalePathRejected=0`, `pathRequestLost=0`. Squads reach
  hangouts, loiter, run to the pad, board, repeat across cities.
- **Bot-vs-bot combat working** (owner toggled on): `botEngagementsTotal=79`,
  deaths=13, promotions=3, reforms=2, wipes=1. Squads fight at shared cities,
  leaders die and get promoted, fully-wiped squads reform after the delay, and
  the population backfills (squads 5/6 spawned to replace wiped rebel squads).
- **Known minor edge:** a leader locked in combat chasing a target (moveState
  FOLLOWING) can sit ~stationary until the 600s TTL force-advances it (safe:
  halts + moves on). That accounts for the lone stateTtl. Acceptable for now; a
  tighter combat-stuck guard is a possible follow-up.

**Remaining cleanup (owner's call):** set `logStateTransitions=false` to silence
the per-tick heartbeat once satisfied (applies at runtime via the 30s config
refresh, no restart), and `recovery.dryRun=false` to let member-far recovery
teleport stragglers. P.6.1 is otherwise COMPLETE and verified. Next feature
work = P.6.2 scouts + gank convergence.

## 16. P.6.2 scouts + gank convergence - SHIPPED (2026-07-05, compiled clean
`-Werror`, PENDING RESTART + VERIFY)

**As-built (matches §4 design, refined by P.6.1 learnings):**
- **Scout squads** (`pvpConfig.scouts`, lua enabled=true / C++ default off):
  1 per faction, size 1, running the SAME city loop as patrols. Their scan is
  wider (`scanRadiusMeters=64`) and, with `reportOnly=true`, a qualifying
  enemy (player always; enemy sim bot only while allowBotVsBotCombat) is
  REPORTED (throttled per bot by `reportIntervalSeconds`) instead of engaged
  - `SimPlayerManager::reportPvpContact`. Scouts still defend themselves via
  the default combat trees. Scout role propagates through spawn, boarding
  replacements, promotion, and wipe re-form (`SimPvpSquad.scout`).
- **Player fights auto-report:** `recordPvpEngagement(targetWasPlayer=true)`
  from ANY squad also registers a faction contact - a patrol that finds a
  real player calls in the gank. Bot-vs-bot engagements do NOT auto-report
  (only scouts report those) to keep convergence churn down.
- **Contact registry:** one `SimPvpFactionContact` per faction (latest wins),
  guarded by pvpSquadMutex, expiring after `contactTtlSeconds` (300). Contact
  city = the reporter's assigned city.
- **Convergence dispatch** (maintenance task step 3, `dispatchPvpConvergence`):
  for each live unexpired contact - respecting a per-(faction,city) cooldown
  AND a per-squad cooldown (`convergeCooldownSeconds`, 600) - pick a responder
  (same faction, patrol only, healthy, not already at the contact city,
  same-planet preferred), stamp `convergePlanet/City/ExpiresAtMs` on it,
  consume the contact, and nudge the leader via the new
  `SimPvPController::interruptForConvergence()` (halt + prepareForRelocation +
  straight to TO_SHUTTLE; skipped while the leader is mid-fight - it finishes,
  then travels). `boardPvpSquad` consumes the pending destination instead of
  the random pick (logged `convergence=true` on SimPvpSquadTraveled).
- **Dashboard:** `pvpActivity.scouts` (config + contactsReportedTotal +
  convergencesTotal + activeContacts rows) and per-squad `role` /
  `convergePending`.

**Verify after restart:** scout squads spawn (`role=scout` in SimPvpSquadSpawned
and dashboard rows; population = 2 patrols + 1 scout per faction = 20 bots);
with botVsBot on, scouts near enemy squads emit `SimPvpContactReported` →
`SimPvpConvergenceDispatched` → responder logs `interruptForConvergence` →
`SimPvpSquadTraveled ... convergence=true` into the contact city → big fight;
cooldowns keep dispatches ≤1 per city per 10min; owner staged test: stand
overt near a scout's hangout → a patrol squad should arrive within ~2-5min
(travel time), confirming the player-gank path.

## 17. P.6.2 first soak (2026-07-05) - mechanism works, phantom-combat freeze blocks it; P.6.2a fix

**Scouts + convergence FIRE correctly:** scouts spawn (`role=scout`), report
(15 contacts / `SimPvpContactReported`), and dispatch (9 / `SimPvpConvergence
Dispatched`) with per-city + per-squad cooldowns holding. BUT **0 convergence
travels** - responder squads never board to consume the pending destination.

**Root cause (live-traced): phantom combat freeze.** Squad 3 sat 1770s frozen
at (3468,-4746), 113m short of its shuttle, `movementState=PATROLLING`, heartbeat
STILL current (tick chain alive), `targetID=0` - yet `defenderList` non-empty
(updateCounter 51) + `lastSuccessfulCombatAction` set. So `isInCombat()` was
stuck TRUE after the enemy died/left (defenderList never cleared). That both
(a) freezes the controller (checkArrival: isInCombat → IDLE, no move) and
(b) blocks the stateTtl force-advance (gated `!isInCombat`). Result: permanent
freeze. Convergence guarantees this by design (it sends squads INTO enemy
cities → pileups → fights → stale defenders). Also churny: 22 reforms / 211
bot-engagements in ~50min.

**P.6.2a fix (compiled clean `-Werror`, PENDING RESTART):**
1. **Phantom-combat guard** in `SimPvpBotController::onTick`: while
   `isInCombat()`, check `getMainDefender()`; if it's null/dead/incapacitated/
   different-zone/>128m for 6 consecutive ticks (~6s), `clearCombatState(true)`.
   Self-correcting (a live enemy resets the counter; a still-attacking enemy
   re-engages next scan). This resumes the loop naturally (IDLE-resume moveTo).
2. **Hard backstop** in maintenance: a movement phase stuck > 2× stateTtl
   (~20min) is force-advanced EVEN IF isInCombat - clears combat first, then
   advances. Guarantees no permanent freeze even if (1) is somehow evaded.

**Verify after restart:** `clearedPhantomCombat` log lines appear; squads no
longer sit >600s frozen; responder squads complete `SimPvpSquadTraveled ...
convergence=1`; `pvpActivity.scouts.convergencesTotal` and convergence travels
both climb; reform churn drops. If churn is still high, consider: fewer
squads, or convergence skipping a city that already has a same-faction patrol
present.

## 18. P.6.2a VERIFIED + P.6.2b loiter-stalemate fix (2026-07-05)

**P.6.2a works:** phantom-combat guard fired 13× (no permanent freezes, 0
hardStuck, 1 stateTtl over a 2.5h run), and **convergence travels CONFIRMED** -
4 `SimPvpSquadTraveled ... convergence=true` matching the 4 interrupts (e.g.
rebel squad 63 Mos Eisley→Coronet, squad 56 Theed→Coronet toward a scout's
contact). Session totals: 71 contacts, 39 convergences, 55 travels, no city
pileups. (Earlier "0 convergence travels" was a grep typo - the flag prints
`convergence=true`, not `=1`.)

**Residual bug found + fixed (P.6.2b):** two opposing squads (imperial 3 +
rebel 40, both Moenia) stuck in `loitering` 100min / 79min, both alive=3 - a
combat STALEMATE (flagged in combat, near each other, but not landing hits →
neither loses members → `finishLoitering` reschedules every 5s forever). The
phantom-combat guard correctly leaves it (the enemy IS a live, near defender),
and the hard-stuck backstop only covered MOVEMENT phases, so loitering never
broke. **Fix:** the hard-stuck backstop now covers `PVP_LOITERING` too and
trips at 1× stateTtl (600s; max legit loiter ~180s) - clears combat, then
force-advances (LOITERING → TO_SHUTTLE), so the squad breaks off and (if it has
a pending convergence) completes it. Compiled clean `-Werror`, PENDING RESTART.

**Verify:** squads 3/40-style loiter freezes gone; `SimPvpHardStuck ...
action=clearCombat+forceAdvance` appears for genuine stalemates; no squad sits
>~10min in any one phase. **Possible follow-up if churn/stalemates persist:**
detect stalemate faster via `lastSuccessfulCombatAction` age (break combat
after ~30s of no landed hits) instead of the 600s phase cap; and/or lower
squad counts. P.6.2 is otherwise functionally COMPLETE.

## 19. P.6.2b VERIFIED LIVE (2026-07-06) - stalemates broken; P.6.2 COMPLETE

8.2h run: **loiter stalemates fixed.** `SimPvpHardStuck` fired 14× (5
`loitering`, 5 `movingToHangout`, 4 `movingToShuttle`), each breaking at
~600-629s (the 1× stateTtl cap), and broken squads recover + continue (squad 79
broke out then completed a `convergence=true` travel). No squad currently stuck
(max phase age 139s; the prior 100-min freezes are gone). Phantom-combat guard
177 clears; convergence 58 travels (129 total). **P.6.2 scouts + gank
convergence is COMPLETE and stable.**

**Tuning observation (owner's call, not a bug):** high combat churn - 367
deaths / 161 reforms over 8.2h, and some squads (e.g. squad 3) hit hardStuck
repeatedly, i.e. they keep landing in stalemates and wait the full 600s each
time. Everything self-heals, but for a cleaner feel the noted follow-up would
help: detect a stalemate in ~30s via `lastSuccessfulCombatAction` age (break
combat when no hit has landed recently) instead of the 600s phase cap; and/or
reduce squad counts / scan radius so fewer squads pile into the same city.
These are polish, not correctness - defer unless the owner wants them.

## 20. P.6.2c - squad members no longer stack (2026-07-06, compiled clean,
PENDING RESTART)

Owner screenshots showed a squad's stormtroopers standing on top of each other
(players never do). Cause: members are engine FOLLOWers that stand at
`leaderPos + formationOffset` rotated by leader facing (AiAgentImplementation.
cpp:4577), and the offsets were the tiny GCW column values (~1-3m: (0.5,-1),
(-0.5,-1), (0.5,-3)) → visually overlapping, especially while loitering.

**Fix:** new `pvpFormationOffset(memberIndex, squadSize, ...)` helper fans
members out - lateral ±7m (spread evenly across the squad) and 5-8m behind the
leader - so they read as players loosely grouped. Applied at BOTH member-spawn
sites (initial squad spawn + boarding replacements) for the persistent follow
formationOffset AND the initial spawn position; per-member spawn Z is now
recomputed via `zone->getHeight(x,y)` so spread bots aren't sunk/floating.
Kept modest (5-8m) so followers stay on open hangout ground. Not-stuck by
construction: the follow pathfinder already routes around buildings and leashes
out of private structures (:4562), and a follower is never permanently wedged -
it re-follows the instant the leader moves on. Boarded SURVIVORS keep their
formationOffset in the blackboard, so they re-fan on arrival with no extra code.
Formation only applies out of combat (:4577) - in a fight they spread toward
enemies naturally, which is correct.

**Verify after restart:** a loitering squad stands in a loose 4-wide group
(~5-8m apart), not stacked; members don't sink into terrain or wedge on
starport walls; they still regroup and travel normally.

## 21. P.6.3a - squad-leader spatial announcements (2026-07-06, compiled clean
`-Werror`, PENDING RESTART + VERIFY)

First of the three P.6.3 comms sub-phases (a spatial → b faction rooms →
c join-a-squad + group-chat keywords). NO core changes.

**As-built:** squad LEADERS speak in spatial "say" chat on four events so
nearby players hear the PvP as live squad chatter:
- ARRIVAL (posting up at a starport hangout) - `startLoitering`
- DEPARTURE (area clear, moving on) - `finishLoitering`
- CONTACT (enemy spotted / engaging) - `scanForTargets` on engage
- CONVERGE (breaking off to reinforce) - `interruptForConvergence`
`SimPlayerManager::announcePvpEvent(squadId, event)` is the single entry point:
config- and cooldown-gated, resolves the leader agent, picks a random
faction-flavored line (3 per faction/event, plain custom text - no client stf
needed), and `ChatManager::broadcastChatMessage(leader, line, 0,0, moodID)`
(spatial, players in range only). Rate limits: per-squad `announceCooldown
Seconds` (45s) + global `globalMinGapSeconds` (4s), stamped under pvpSquadMutex
so a burst of member engagements yields exactly one leader shout. Dashboard
`pvpActivity.comms` (enabled + announcementsTotal). Lua `pvpConfig.comms`
(shipped `spatialAnnouncements=true`; C++ default off).

**Verify after restart:** stand near a squad's hangout - the leader says an
arrival line on posting up, "Contact!" on engaging, a departure line on
leaving; `pvpActivity.comms.announcementsTotal` climbs; no chat spam (≤ ~1
line/squad/45s). **Next:** P.6.3b faction chat rooms (GCW.Rebel/GCW.Imperial,
players join from the chat browser; leaders post arrivals/contacts) then P.6.3c
the two gated core patches (`GroupManager::joinGroup` inviter relaxation +
`ChatManagerImplementation::handleGroupChat` keyword hook) for join-a-squad.

## 22. Radar-dot color flicker fix - factionStatus OVERT (2026-07-06, compiled
clean `-Werror`, PENDING RESTART)

Owner reported PvP bots' radar dots flickering red<->blue on DISTANT bots while
the one being fought stayed solid red (fellow rebels correctly purple/grouped).

**Root cause:** bots were spawned with `setFaction(hash)` + the OVERT
*pvpStatusBitmask* bit + `ObjectFlag::PLAYER` (for the player-style dot), but
`factionStatus` was left at its default ONLEAVE (0, TangibleObject.idl:125).
Because the PLAYER flag makes the client con/color them via the player-vs-player
path, `CreatureObjectImplementation::isAttackableBy` requires OVERT-vs-OVERT
(line 3658) / factionStatus > ONLEAVE (3592) for a red result. With
factionStatus ONLEAVE they were only attackable TRANSIENTLY (in combat / TEF),
so as combat churned - and the P.6.2a phantom-combat guard cleared stale combat
every ~6s - distant bots flipped attackable<->not => dot flickered red<->blue.
The bot under active fire stayed in combat continuously, so it stayed red.

**Fix:** `agent->setFactionStatus(FactionStatus::OVERT)` at both PvP spawn sites
(spawnPvpBotAgent + toggleBot), right after setFaction - exactly what GCW base
defenders do (BuildingObjectImplementation.cpp:1728). setFactionStatus is a
plain field set for AiAgents (non-player), carried in the spawn baseline, and
satisfies both the covert-overt and classic attackability checks, so opposing
overt players now see a stable RED. No effect on same-faction (still purple/
friendly) or on miners (different spawn path, no faction). Include added:
server/zone/objects/player/FactionStatus.h. **Verify after restart:** distant
enemy bots hold solid red (no blink) whether or not they're in combat.

## 23. P.6.3b - faction chat rooms (2026-07-06, compiled clean `-Werror`,
PENDING RESTART + VERIFY)

Second P.6.3 comms sub-phase. Adds two galaxy-wide command channels so players
follow the PvP feed, gated so the enemy faction can't read yours.

**As-built:**
- **Rooms:** `ensurePvpFactionRooms()` creates `GCWRebel` / `GCWImperial` under
  the galaxy room (`getChatRoomByFullPath("SWG."+galaxy)`) - chat-browser path
  `SWG.<galaxy>.GCWRebel` / `.GCWImperial` - CUSTOM + canEnter + **moderated**
  (players read the feed; only our system posts). Created lazily + at the top
  of the maintenance task so they exist ~within 30s of boot. Mirrors the stock
  Auction / PvPBroadcasts rooms.
- **Posting:** `postPvpFactionRoom(imperial, sender, text)` forges a
  `ChatRoomMessage(leaderName, galaxy, text, roomID)` and `room->broadcastMessage`
  (no player object needed; moderation doesn't block a direct broadcast). Hung
  off `announcePvpEvent`: ARRIVAL/CONTACT/CONVERGE post with city context
  ("Holding the starport at Theed.", "Contact at Coronet - enemy engaged!",
  "Reinforcing Mos Eisley - squad inbound!"); DEPARTURE skipped (keeps the feed
  signal-heavy). Shares the spatial announce cooldown (one shout = one post).
- **Faction gate (the one core patch):** flag-guarded block in
  `ChatManagerImplementation::handleChatEnterRoomById` (before checkEnter
  Permission, NOT bypassed by admin bypassSecurity) → for our two room IDs,
  `SimPlayerManager::isPvpFactionRoomJoinAllowed(player, roomID)` requires
  `player->getFaction() == roomFaction` (+ OVERT if
  `factionRoomRequireOvert`), else NOTINVITED. `isPvpFactionRoom` returns false
  unless the feature+gating are on, so the hook is inert when disabled. Owner
  note acknowledged: a SAME-faction alt can still lurk - that's inherent and
  accepted.
- Dashboard `pvpActivity.comms` += factionRooms/created/room ids/postsTotal/
  joinsBlockedTotal. Lua `pvpConfig.comms.factionRooms=true`,
  `factionRoomRequireOvert=false`.

**Core touch:** 1 file - `ChatManagerImplementation::handleChatEnterRoomById`
(one gated block + include). Everything else in simplayer/ + lua.

**Verify after restart:** chat browser shows `SWG.<galaxy>.GCWRebel/GCWImperial`
(`SimPvpFactionRoomsCreated` log); a same-faction player joins and sees leader
posts (`factionRoomPostsTotal` climbs); an ENEMY-faction char is refused
("not invited", `factionRoomJoinsBlockedTotal` climbs); players can't chat in
the room (moderated). Optionally flip `factionRoomRequireOvert=true` and confirm
a non-overt same-faction char is refused. **Next:** P.6.3c join-a-squad +
group-chat keywords (the 2 remaining gated core patches: GroupManager::joinGroup
+ ChatManagerImplementation::handleGroupChat).

## 24. P.6.3c - join-a-squad + group-chat commands (2026-07-06, compiled clean
`-Werror`, PENDING RESTART + VERIFY)

Final P.6.3 comms sub-phase. A player can join a squad's own group and steer it
with free-text chat. Owner decisions: **group = NPC leader + players only**
(NPC members stay pure AI); **same-faction only** (neutral → nearest).

**Safety (owner's core concern - a player must NEVER become group leader):**
- Group leader is always `groupMembers[0]`; `removeMember` shifts, so removing
  the leader would auto-promote the next entry. Verified: **destroying an NPC
  does NOT auto-remove it from its group or transfer leadership** - we own all
  transitions.
- NPC leader dies → squad promotes a new NPC leader → `transferSquadGroup
  Leadership`: `addMember(newNpc)` + `makeLeader(newNpc)` + `removeMember(old)`,
  all under the group lock, so slot 0 is always an NPC. A player is never
  shifted into slot 0.
- Full squad wipe (no NPC left) → `disbandSquadGroup` → `group->disband()`
  (kicks players with a message). Same for a vanished leader.
- Players LEAVE via the stock `/leavegroup` (engine-correct locking); a
  `reconcilePvpSquadGroups` maintenance pass clears a squad's `groupId` once its
  group drops below 2 (disbanded). No leave-from-hook code (avoids fragile
  manual unlock/wlock).

**Join / commands:**
- Spatial "join pvp group" near a same-faction squad (within `joinRangeMeters`
  48) → nearest such squad's leader invites you. "join group with <name>" →
  the squad containing a bot by that name. `addPlayerToSquadGroup` uses
  `inviteToGroup(npcLeader, player)` + `joinGroup(player)` (proven path;
  createGroup for the 1st player, addMember after), capped at
  `maxPlayersPerSquad` (5).
- In group chat: "status" / "where" → the leader posts a reply into group chat.
- Squad callouts (arrivals/contacts/reinforce) post to the group chat too, so
  grouped players get the feed.

**Core patches (3, all flag-gated, inert when grouping off):**
1. `GroupManager::joinGroup` - accept a sim-bot inviter when
   `SimPlayerManager::isPvpSquadLeaderNpc(oid)` (grouping on AND oid is a
   current squad leader).
2. `ChatManagerImplementation::broadcastChatMessage` (spatial) → forward a
   player's line to `onPlayerSpatialChat` (cheap early-out unless text has
   "join").
3. `ChatManagerImplementation::handleGroupChat` → after delivery, forward a
   PvP-squad-group message to `onPvpGroupChat`.

Lua `pvpConfig.comms.playerGrouping=true` (+ maxPlayersPerSquad, joinRange
Meters). Dashboard `comms` += playerGrouping/groupsFormed/playersJoined/
groupsDisbanded/activeSquadGroups.

**Verify after restart (needs a real player):** get near a same-faction squad,
say "join pvp group" → invited + joined + welcome in group chat; squad callouts
appear in group chat; "status"/"where" get a leader reply; **kill the squad
leader → confirm the group leader becomes the NEW npc leader, NEVER you**; wipe
the squad → group disbands with a message; `/leavegroup` works and the dash
`activeSquadGroups` reconciles. Edge (documented): join + leader death in the
same ~30s tick before `groupId` is recorded could orphan a group - player can
`/leavegroup` out; negligible. **P.6.3 comms COMPLETE after this verifies.**

## 25. P.6.3c CRASH FIX - ChatRoom::addPlayer null ghost (2026-07-06, compiled
clean, PENDING RESTART)

First live join attempt SIGSEGV'd. Stack: onPlayerSpatialChat →
addPlayerToSquadGroup → joinGroup → **createGroup(leader=NPC)** → startChatRoom →
createGroupRoom → handleChatEnterRoomById(NPC) → `ChatRoom::addPlayer(NPC)` →
`player->getPlayerObject()->addChatRoom()` with **this=0x0**.

**Root cause:** `createGroup` adds the group LEADER to the new group chat room,
and `ChatRoomImplementation::addPlayer` (room/ChatRoomImplementation.cpp:62)
dereferenced the leader's PlayerObject ghost with no null check. Our leader is
an NPC (no ghost) → crash. My "createGroup is null-safe for an NPC leader"
assumption was wrong for the chat-room step. (Notably `removePlayer` and both
`broadcastMessage` variants in the SAME file already null-guard the ghost -
`addPlayer` was the lone omission, a latent bug for ANY NPC added to a room.)

**Fix:** null-guard the ghost in `addPlayer` exactly like `removePlayer`
(`if (ghost != nullptr) ghost->addChatRoom(...)`). The NPC still sits in the
room's playerList harmlessly - `broadcastMessage` sends via `sendMessage` (no-op
without a client) and the ignore-list variant skips null-ghost entries. Verified
the rest of the NPC-leader group flow is safe: `addMember` guards all
player-only work behind `isPlayerCreature()`; `sendTo`/`sendSystemMessage`
no-op without a client. This was the only unguarded deref. **Re-verify:** join a
squad → no crash, group + group chat work; then the full §24 checklist
(leader-death transfer, wipe disband, /leavegroup).

## 26. P.6.3c live-feedback fixes (2026-07-06, compiled clean, PENDING RESTART)

Owner tested joining; three fixes:

1. **Whole squad in the group** (owner reversed the earlier "leader+players
   only" call - expected the full squad visible). `syncSquadGroupMembers(squadId)`
   reconciles the group's NPC roster == the squad's live members: idempotent,
   group-locked add/remove, never touches the leader or player members. Called
   at join and each maintenance tick (via reconcile) for squads with a group,
   so new spawns appear and dead/replaced members drop within ~30s. Player
   cap now counts only PLAYER members (NPC members don't count). SAFETY BONUS:
   with members in the group, if the leader is removed the next slot-0 is an
   NPC member, not the player - extra buffer on top of the explicit transfer.
2. **Join targeting = nearest bot, not nearest leader.** `onPlayerSpatial
   Chat` "join pvp group" now picks the squad whose nearest bot (leader OR any
   member) is closest within `joinRangeMeters` - so standing among a squad's
   members joins THAT squad, not a distant leader's squad. Also fixed
   `addPlayerToSquadGroup` to always record the group actually joined (corrects
   a stale `groupId` from a prior disband so the new group is tracked).
3. **Combat leash** (owner: attacked/chased from 100m+, squad engaged from
   100m - beyond the ~64m ranged range). The phantom-combat guard's fixed 128m
   became config `combatLeashMeters` (72, just above effective ranged range):
   a bot whose target flees beyond it drops combat (after the ~6-tick grace)
   instead of chasing across the map. Caps chases ~72–100m and keeps fights
   local; stalemate/hard-stuck handling unchanged (near targets still count).

**Verify after restart:** joined group shows the whole squad (members appear/
drop as they spawn/die); standing among a squad's members and "join pvp group"
joins that squad; disband + rejoin tracks correctly; a bot stops chasing/hitting
a target that outruns ~72m. Known: member roster lags up to ~30s (maintenance
cadence) - acceptable; can add an immediate on-death group-remove later if the
churn reads poorly. **Still owner-verify the §24 leader-death/wipe safety with
members now in the group.**

## 27. P.6.3c invite-only join (2026-07-06, compiled clean, PENDING RESTART)

Owner: asking to join auto-added the player AND popped the accept/decline
dialog - the player should just get the invite and decide. Fix: split the flow.
`addPlayerToSquadGroup` now sends ONLY `inviteToGroup(npcLeader, player)` (the
client pop-up) and returns - no `joinGroup` auto-add. When the player ACCEPTS,
the stock client join runs `GroupManager::joinGroup` (already patched to accept
a sim-bot inviter); both of its success exits (createGroup / addMember) now call
back `SimPlayerManager::onPlayerJoinedSquadGroup(leaderOid, player)`, which
records the squad's `groupId` (under pvpSquadMutex) and defers the roster sync +
welcome to a 0.5s task (so no group/agent locks are held inside joinGroup).
Decline = joinGroup never runs = no group formed. **Verify:** ask to join → only
the pop-up appears; Accept → joined, squad pulled in, welcome; Decline → nothing.
