# ROADMAP P.10 — PlayerBot player-parity groundwork

**Line**: P.10 (new feature line, versions `0.9.x`)
**Branch**: `feat/p10-playerbot-parity`, cut from `miner-ai` at `266e6d02e7` (v0.8.2)
**Status**: ROADMAP — planning only. First chunk plan: `F_0.9.0_p10a-progression-store.plan.md`.
**Written**: 2026-09-02 (project week 8)

## 0. Mandate

Owner decision 2026-09-02: this line is the **explicit economy-phase approval**
that CLAUDE.md and ARCHI §12 gated on. PlayerBots MAY perform real mutations
for XP, credits, loot, skills and bazaar transactions — but only

- behind a **per-capability Lua gate**, default-off, reversible;
- **after that chunk's live harness passes** (TRIP-verify receipt);
- for **roster PlayerBots only** (identities in `simbot_identities`). Miners
  and PvP squad bots have no roster identity today and stay simulation-only
  until they are migrated onto the roster (P.8.5, not in this line).

Everything before this line was bot-internal. **Chunks 8 and 9 (bazaar) are
the first surface shared with humans**: listings are visible to real players
and bot credits spent there are real credits entering the economy.

## 1. What the engine provides vs. what a PlayerBot lacks

A PlayerBot body is an `AiAgent` (IDL, `AiAgent.idl`) spawned from a mobile
template, carrying `simPlayerBot=true`, `ObjectFlag::PLAYER | ATTACKABLE`,
faction 0, and bound to a roster identity through
`SimPlayerManager::pveBodyIdentityIds` (`SimPlayerManager.h:1695`). It has
**no `PlayerObject` ghost**. That single fact is the root of every gap below,
because the engine stores XP on the ghost and gates most reward paths on
`isPlayerCreature()`.

| Capability | Engine provides | Where a PlayerBot falls off | Consequence for P.10 |
| --- | --- | --- | --- |
| **XP per kill** | `CreatureManager::notifyDestruction` copies the corpse's `ThreatMap` and calls `PlayerManager::disseminateExperience` (`CreatureManagerImplementation.cpp:675`). `ThreatMapEntry::addDamage(weapon->getXpType(), dmg)` (`ThreatMap.cpp:18-22, :69-102`) already keys **every attacker's** damage by the weapon's XP type — a bot with the T-21 accrues `combat_rangedspecialize_rifle` entries. Formula at `PlayerManagerImplementation.cpp:2225-2290`: `baseXp = ai->getBaseXp()` (`:2087`) × damage share, capped at `playerLevel*300`, × group multiplier, plus `combat_general` at 10 %. | `disseminateExperience` only visits `isPlayerCreature()` attackers (`:2225`), and `awardExperience` returns 0 without a ghost (`:2703-2707`). XP lives in `PlayerObject.experienceList` (`PlayerObject.idl:128`). | The threat-map data is already correct; we need a **bot branch at the same chokepoint** writing to a manager-owned store, and a bot-level source for the cap. |
| **Credits per mission** | Real missions pay in `MissionObjectiveImplementation::awardReward` (`:317-364`): reward split by `max(rewardCreditsDivisor, groupCountInRange)`, `player->addBankCredits`. Reward amount from `MissionManagerImplementation.cpp:920` (`destroyMissionBaseReward + factor*difficultyLevel + random`, config on `MissionManager`). | Bot missions are **mimetic** (P.8.7): a `PveBotMissionOffer` (`SimPlayerManager.h:197`) with `difficultyLevel`, no `MissionObject`. Completion is counted at `pveMissionsCompletedTotal++` (`SimPlayerManager.cpp:10649`, `:12781`) and pays nothing (F_0.5.0 "no credits at all this slice"). | Compute the reward from the same formula inputs at the existing completion site; credit the store, not the body. |
| **Loot from kills** | `notifyDestruction` creates loot into the corpse inventory via `LootManager::createLoot` (`CreatureManagerImplementation.cpp:684-705`), adds cash (`calculateLootCredits`), and keeps the corpse 300 s (`:728`). Players loot via `PlayerManager::lootAll` (`:4410`). Group rules FFA / MASTERLOOTER / LOTTERY / RANDOM in `GroupLootTask.h` + `GroupManager::createLottery/doRandomLoot/transferLoot`. | Loot is created **only** when the highest-damage group leader `isPlayerCreature()` (`:684`). A bot kill yields an empty corpse despawned in 10 s. LOTTERY is a player UI session (`LootLotterySession`). | Bot branch at the same chokepoint creates loot exactly as for a player, then a bot "loot step" moves items into a real container. Bot-group dispersal must be manager-side (RANDOM/MASTERLOOTER semantics); LOTTERY cannot apply to bots. |
| **Group membership** | `GroupObject::addMember` accepts any `CreatureObject`; P.6.3c already lets a squad-leader NPC be an inviter (`GroupManager.cpp:193`). Group XP multiplier and mission split are group-size driven. | `inviteToGroup`/`joinGroup` are player-choreographed (system messages, chat rooms, LFG bits at `:259-300`); bot-only groups need a manager-formed group with the same lock choreography (`Locker glock(group); Locker clocker(creature, group)`). Hunters currently hunt **solo**. | Group dispersal ships behind a harness-formed bot group; production group hunting is not a P.10 deliverable. |
| **Skill training** | `SkillManager::awardSkill` (`SkillManager.cpp:326`) checks prerequisites, XP cost, 250 skill points; `CreatureObject::addSkill`/`addSkillMod` (`CreatureObject.idl:919/:961`) exist on **every** creature — NPC templates use them. Skill data (xpType, xpCost, points, prereqs) is readable from the skill map without a ghost. | `canLearnSkill` returns false without a ghost (`:867-869`); XP withdrawal, skill points, abilities and schematics are ghost operations (`:360-386`). Skill mods DO affect AiAgent combat; command abilities do not (bot attacks come from the mobile template's attack maps). | Training decision lives in the store (XP, points, prerequisites checked against `simbot_skills` with the skill map's `getSkillsRequired()` — `fulfillsSkillPrerequisites` itself rejects ghostless creatures at `SkillManager.cpp:938-940`), body application via `addSkill`/`addSkillMod` at every (re)spawn, with the `AiAgent::getSkillMod` replace-not-add caveat (§5/F_0.9.5). |
| **Bazaar sell** | `AuctionManager::addSaleItem(player, objectid, vendor, …)` (`AuctionManagerImplementation.cpp:519`), item must be `isASubChildOf(player)`; owner recorded as `ownerID`/`ownerName` on the `AuctionItem` (`AuctionItem.idl:159/:271`). Seller paid in `doInstantBuy` via `PlayerManager::getPlayer(ownerName)` (`:1060`, null seller **aborts** at `:1207-1215`); expiry mails the owner and requires the owner to `retrieveItem` (`:1514`, owner check `AuctionItem.idl:331`). | Ownership is the **body OID** and the name lookup is the **player map**. A bot body dies and is rebuilt with a new OID, so proceeds are lost and expired items are unreachable. | Bot listings need a **durable owner mapping** (`simbot_listings`) and sim-aware branches in `AuctionManager` for payout and retrieval (Core3, no engine3). |
| **Bazaar buy** | `doInstantBuy` (`:1038`) debits `player->subtractBankCredits` from the buyer's `CreditObject`, marks SOLD, buyer retrieves at the terminal. `AuctionsMap` is queryable without UI packets. | Bot bank credits live in the store, not on the body's `CreditObject`; the buyer is recorded and retrieval enforced by body OID (`:1442-1452`); the listing fee is also charged to the body's bank (`:739-760`). | Identity-aware sim entries in `AuctionManager` that settle straight against the store and vault; never stage credits or items on the body. |
| **Inventory / bank** | `CreatureObject` has inventory slot + lazily-created `CreditObject` (`CreatureObject.idl:59`, `CreatureObjectImplementation.cpp:108`). | Body is **destroyed on death** and rebuilt (`SimHunterController.cpp:2024` → `CLONE_HOME`, `spawnPveIdentityBody` `:8729`); nothing on it survives. Cash on a bot corpse is lootable by whoever kills it (stock NPC behaviour). | Scalars in MySQL; items in a **hive item vault** (persistent container) with an identity-keyed ledger. |
| **Persistence** | MySQL roster `simbot_identities` (schema 1009, `ServerDatabase.cpp:144`), loaded/minted/flushed on the PvE maintenance thread only (`SimPlayerManager.cpp:8508/:8574/:8677`, driven from `:12925-12956`). BerkeleyDB object store for real objects. | Roster holds lifetime stats only (`SimBotIdentity`, `SimPlayerManager.h:102`). | The progression store extends the roster pattern: MySQL tables keyed by `identity_id`, same thread discipline. |

## 2. Design invariants (every chunk inherits these)

1. **No progression state on `AiAgent`.** No new IDL field, no flag, no
   blackboard key. Precedent: the `simPlayerBot` sticky flag leaked onto wild
   creatures through engine object reuse (root-caused 2026-07-20, reset added in
   `AiAgentImplementation::loadTemplateData` `:358-367`). A persistent record
   leaking the same way would survive restarts and accumulate forever.
2. **Store is SimPlayerManager-owned and keyed by roster identity id** — the
   `AUTO_INCREMENT BIGINT` from `simbot_identities`, minted once, never reused.
   Never by object ID. Body → identity resolution goes through
   `pveBodyIdentityIds` under `pveMutex`; a body with no mapping is not a
   PlayerBot for award purposes, whatever its flags say.
3. **Records are created by exactly one C++ function**,
   `ensurePlayerBotProgressionRecord(identityId)`, which refuses any id not
   present in the loaded roster. It is invoked from the roster mint path
   (`mintPveIdentitiesIfNeeded`, `:8574`) and, because `simbot_identities`
   is MyISAM (no transaction spans the identity INSERT and the record INSERT),
   as an **idempotent boot/enable repair** for roster identities that have no
   record — the only way an incomplete mint or a mint made while the store was
   disabled can be healed. That is two call sites of one function, never a
   creation from an object, and it replaces the earlier idea of a
   schema-migration backfill (flagged to the owner as a deliberate reading of
   "one place"). Every award API requires an existing record: **missing
   record ⇒ skip and count** (`awardsRejected.noRecord`), never create.
4. **Nothing durable lives on the body.** Scalars (XP, credits, skill points)
   are MySQL rows; skills are rows re-applied to each new body; items are real
   objects in a manager-owned persistent container, ledgered per identity.
   Death → clone must lose nothing (scenario class inherited from F_0.8.0
   scenario 22 and the F_0.8.1 destroyed-body carry).
5. **Every mutation is gated** per capability in Lua, default-off. Gate-off
   must be byte-for-byte legacy at every touched decision point.
6. **Locking**: a new `progressionMutex` follows the `pveMutex`/`pvpSquadMutex`
   contract — never held while locking an agent, never nested with `pveMutex`
   (resolve identity under `pveMutex`, release, then take `progressionMutex`).
   SQL only on the PvE maintenance task (one SQL lane; harness SQL is queued
   to it). The flush **swaps the dirty batch out atomically** before I/O and
   merges it back on failure — the roster's clear-after-write shape
   (`SimPlayerManager.cpp:8687-8719`) would drop an award landing during SQL
   and is not copied. `dbAvailable=false` recovers by bounded re-probe, not
   by restart.
7. **Dashboard first**: the store is inspectable (chunk 1) before any award
   logic exists. Orphan and "roster-without-record" counters ship in chunk 1.
8. **Test oracle asserts per-identity, never on global counters** (F_0.8.1
   lesson). Harness identities are real roster rows with `profession='harness'`,
   excluded from population governors, and deleted at cleanup — which also
   exercises the reaper.

## 3. Structural PlayerBot vs. runtime boolean — evaluation

**Question**: should "is a PlayerBot" be a subclass or dedicated template
family rather than the `simPlayerBot` boolean?

| Option | Mechanism | Pros | Cons |
| --- | --- | --- | --- |
| A. Runtime boolean (today) | `AiAgent.simPlayerBot`, set after spawn, reset in `loadTemplateData` | Zero cost; leak class already closed | A flag is still a flag: any future reuse path that bypasses `loadTemplateData` re-opens the leak; no compile-time dispatch |
| B. Dedicated **template family** | Mobile templates `sim_playerbot_<profession>` (owner already wants custom templates + explicit gear); `loadTemplateData` derives the marker from the template (`simBot=true` field) instead of a setter | Marker becomes **template-derived state**, recomputed on every template load ⇒ cannot leak by construction; cheap; no IDL change; aligns with the profession-template work in chunk 9 | Still a boolean at runtime; `asSimBotAgent()`-style dispatch not available |
| C. IDL **subclass** `SimBotAgent extends AiAgent` | New IDL class + `ObjectManager`/`TemplateManager` class-binding for a new template type | Type is fixed at construction: impossible to leak, engine hooks (`notifyDestruction`, `isAttackableBy`, `AuctionManager`) can dispatch on type | Touches object-type registration (moderate engine surface, Core3 not engine3); every spawn path and the Lua wrapper layer (`LuaAiAgent`) need the new class; autogen regen risk |

**Recommendation**: the P.10 invariant that actually closes the leak class is
**§2.2/§2.3 — records keyed by roster identity, award-requires-record**. A
leaked flag on a wild gorg can never acquire a record because it has no roster
mapping. So structural typing is a *hardening*, not a prerequisite.
Do **B in chunk 9** (profession templates are being introduced there anyway),
and **defer C** unless a later hook needs type dispatch; record the decision
in that chunk's plan. Chunk 1 does not depend on either.

## 4. Existing simulated stores — disposition

| Store | Today | P.10 disposition | Chunk |
| --- | --- | --- | --- |
| **P.5 hive stockpile** (`AiEconomyManager`, `AiEconomyStockpileLot`, resource lots + finished goods) | Persisted ledger of *simulated* resource units; miners deposit, conceptual crafters consume | **Stays a shadow mirror** for resources through P.10 (it is not backed by `ResourceContainer` objects; making it real is the crafting economy phase). **Gains a real sibling**: the **hive item vault** — a persistent container object holding real loot/bazaar `TangibleObject`s, OID stored on `AiEconomyData`, ledgered per identity in `simbot_items` with an explicit **operation state** (`pending → held → listed → consumed`) so BDB containment and the MySQL ledger have one authority: an object is owned by an identity only when a `held` row says so; boot reconciliation checks **every** row state against vault containment (and `AuctionsMap` for `listed`) — a `held`/`listed` row whose object is missing or contained elsewhere is moved to a system-owned quarantine state `orphaned` (`identity_id NULL`, counted, never blocks reaping), and an unattributed vault object gets an `orphaned` row of its own. Bot-held items live there, never on a body. | vault + journal: **F_0.9.3**; listing states **F_0.9.6**; hive lots unchanged |
| **P.8.1c acquisition ledger** (family supply/signals, `pveSessionHarvestByFamily`) | In-memory demand signals driving hunter dispatch | **Stays shadow, unchanged.** Drives *where* bots hunt; P.10 changes what a kill *pays*. Retire/replace only when a consumer phase lands (documented debt since F_0.6.0). | — |
| **Demand state** (`demandStateSimulationConfig`, profiles, pressure) | Conceptual demand engine | **Stays shadow, unchanged.** Chunk 9's allocation policy reads its per-profile pressure as one input, read-only. | read in **F_0.9.8** |
| **Roster lifetime stats** (`simbot_identities.hunts/kills/deaths/harvest_units`) | Real MySQL rows | **Stay as-is**; the progression tables reference the same `id`. | — |
| **Simulated credits** | None exist (F_0.5.0 pays nothing) | **Becomes real** from the first credit award. | **F_0.9.2** |
| **Simulated harvest** (`recordPveHunterHarvest` → hive lots) | Simulated creature-resource units | **Stays shadow** in P.10 (real resource containers = crafting phase). | — |

## 5. Chunks

Each chunk is one TRIP release (plan → Codex plan review → harness-first
implementation → Codex code review → TRIP-verify receipt → TRIP-3). Version
numbers are the proposal; TRIP-1 may adjust. **Deployment order is strictly
numeric**: `alterDatabase` skips every version at or below the deployed
`schema_version` (`ServerDatabase.cpp:63-65`), so a chunk that carries a
schema block may only be deployed after every lower-numbered chunk's schema
has landed — the dependency graph below shows functional dependencies, and
the numeric rule is an additional release dependency on top of it (in
particular F_0.9.3 (schema 1013) must deploy before F_0.9.5 (schema 1014)
and F_0.9.6 (1015)).

```
F_0.9.0 store+dashboard+reaper+harness ─┬─► F_0.9.1 XP per kill ──► F_0.9.5 skill training ──► F_0.9.8 allocation policy
                                        │                          ▲ (release dep: F_0.9.3 schema 1013 first)
                                        ├─► F_0.9.2 mission credits ─┐                              ▲
                                        └─► F_0.9.3 loot (solo)+vault ┼─► F_0.9.4 group dispersal   │
                                                                      ├─► F_0.9.6 bazaar SELL ───────┤
                                                                      └─► F_0.9.7 bazaar BUY ────────┘
```

### F_0.9.0 — Progression store foundation (P.10a)

- **Scope**: MySQL tables `simbot_progression` (identity_id PK, bank_credits,
  cash_credits, skill_points_spent, level_hint, created_at, updated_at),
  `simbot_experience` (identity_id, xp_type, amount; PK both),
  `simbot_skills` (identity_id, skill_name, trained_at; PK both). Load at
  roster boot (and on runtime enable); create in the mint path + boot repair
  (§2.3); atomic-swap dirty-batch flush on the PvE maintenance task; harness
  SQL queued to that same task. Award API
  surface (`grantExperience`, `grantCredits`, `spendCredits`, `recordSkill`)
  gated `playerBotProgression.enabled`, **no production caller yet** — the
  harness is the only caller. Orphan reaper (count-only default; delete
  gated). Dashboard section `playerBotProgression`. Harness
  `playerBotParityTest` (matrix + `SimParityTestController` + oracle) with
  the scenario vocabulary later chunks extend. Docs: this roadmap, CLAUDE.md
  policy, ARCHI §12 note, memory file.
- **Engine touchpoints**: `ServerDatabase.cpp` (schema 1010–1012, `CREATE
  TABLE IF NOT EXISTS`; later chunks continue at 1013+),
  `sql/swgemu.sql`, `SimPlayerManager.{h,cpp}` (store, API, dashboard,
  harness runner), `SimPlayerController.{h,cpp}` (harness controller, after
  `SimTraversalTestController` `:783`), `sim_player_manager.lua`,
  `bin/scripts/ai/templates.lua` (+ harness AI tree), `app.js` (page/card).
  No stock-manager edits, no IDL, no engine3.
- **Persists**: the three tables. Nothing on any object.
- **Risk**: low–moderate. New mutex; SQL thread discipline; the mint path
  gains a second INSERT (must stay on the maintenance thread and be
  idempotent under `INSERT IGNORE`). Harness identities must be provably
  excluded from `governPvePopulation`.
- **Test strategy**: scenarios — record-per-roster invariant; award to
  unknown identity rejected + counted; award to a leaked-flag non-roster body
  rejected; body destroy → respawn keeps scalars; flush → restart → values
  resume (durable two-boot probe); natural flush cadence; orphan injected →
  counted → reaped (gate on) / not reaped (gate off); **and the repaired
  failure windows**: an award landing during SQL I/O is persisted by the next
  flush, a flush failure merges back and the re-probe recovers, and a roster
  identity whose progression row is missing (the crash-between-inserts case)
  is repaired by reconciliation. Oracle reads per-identity snapshots under
  `progressionMutex`. Receipt + `docs/4-unit-tests/live_p10a-*.md`.

### F_0.9.1 — XP per kill (P.10b)

- **Scope**: bot branch beside `disseminateExperience` in
  `CreatureManager::notifyDestruction` (`:675`): for each `copyThreatMap`
  attacker that resolves to a roster identity, compute per-xp-type awards from
  the entry's damage map (same formula, same cap shape, group multiplier when
  grouped), plus `combat_general` at 10 %, and call `grantExperience`. XP caps: `defaultXpLimits` is **private**
  (`SkillManager.h:51`), so F_0.9.1 adds a `const` read accessor
  (`getDefaultXpLimit(xpType)`) to `SkillManager` (Core3) and applies the
  cap as max trained `Skill::getXpCap()` else that default (finalised in
  F_0.9.5 once trained skills exist). Gate
  `playerBotProgression.awardKillXp`.
- **Engine touchpoints**: **CORRECTED IN F_0.9.1's PLAN** — the branch went
  inside `PlayerManagerImplementation::disseminateExperience` rather than at the
  `CreatureManagerImplementation.cpp:677` call site sketched here.
  `disseminateExperience` has two callers (single-creature death and
  `DisseminateExperienceTask` for lairs); hooking at the CreatureManager site
  alone silently omits every lair kill, and would have to recompute `baseXp`.
  The P.7.4b FRS block already lives in that function and set the precedent.
  Later chunks should inherit this decision, not the original sketch. Also
  `SimPlayerManager`.
  Bot level for the cap: derived from trained skills in the store (fallback
  `skillTier`), never from the body template's level (a wraith is level 178).
- **Persists**: `simbot_experience` rows.
- **Risk**: moderate — runs inside the destruction path under the corpse lock
  choreography; the branch must take **no** agent locks (resolve identities by
  OID under `pveMutex`, then write under `progressionMutex`), and must be a
  no-op for non-roster attackers so wildlife-on-wildlife kills cost nothing.
- **Test strategy**: harness bot with a harness identity kills a spawned
  creature (existing attacker-spawn machinery) → `combat_rangedspecialize_rifle`
  and `combat_general` deltas > 0 and consistent with damage share; a
  harness bot **without** identity → zero rows, `noRecord` counter +1; cap
  reached → no further growth; miner/PvP bodies never appear in the store.

### F_0.9.2 — Credits per completed mission (P.10c)

- **Scope**: award **only offer-backed completions** — the mission-board
  site (`SimPlayerManager.cpp:10649`), where the completed
  `PveBotMissionOffer` carries `difficultyLevel`. The legacy non-board site
  (`recordPveHunterCompleted`, `:12764-12781`, active only when
  `missionBoard` is disabled) has no difficulty on `PveHuntOrder`
  (`SimPlayerManager.h:275`) and pays nothing. Reward from `MissionManager`'s
  destroy-mission inputs (`MissionManager.idl:68` family; add read accessors
  if private), the `awardReward` divisor rule (solo divisor = 1 now; group
  split lands in F_0.9.4), idempotent per `offerId`, `grantCredits(bank)`.
  Gate `playerBotProgression.awardMissionCredits`.
- **Engine touchpoints**: `SimPlayerManager` completion sites; possibly
  `MissionManager.idl` getters (Core3 IDL, regen). No mission objects.
- **Persists**: `simbot_progression.bank_credits`.
- **Risk**: low. Pure bookkeeping at an existing event.
- **Test strategy**: harness op `completeMission{difficultyLevel}` drives the
  same completion function → bank delta within the formula's bounds; twice for
  the same offer id → credited once; unknown identity → rejected.

### F_0.9.3 — Loot from kills, solo, and the hive item vault (P.10d)

- **Scope**: (a) `AiEconomyManager` creates once and persists a **hive item
  vault** container (`ObjectManager::createObject(…, persistenceLevel 1, …)`),
  OID stored in a new `AiEconomyData.idl` field; `simbot_items` ledger table.
  (b) bot branch at the loot chokepoint (`:684`): when the highest-damage
  group leader is a roster bot, create loot + cash into the corpse **exactly
  as for a player** (corpse kept 300 s, container owner = body/group so a
  passing player cannot loot it). (c) controller **loot step**: walk to the
  corpse, `lootAll`-equivalent for bots — cash → store, items →
  vault + ledger row (identity nullable, object oid, source, acquired_at,
  **state**). Item moves are journaled: write `pending` row → transfer
  object → mark `held`; boot reconciliation applies a **per-state predicate
  and recovery action**, not one containment rule: `pending` — object still
  in the corpse (corpse gone after a restart) ⇒ row → `lost` (counted,
  nothing to recover), object already in the vault ⇒ row → `held`;
  `held` — object must be in the vault, else → `orphaned`; `listed` — object
  must be in `AuctionsMap` with the recorded operation, else → `orphaned`;
  `consumed` — terminal, object is expected to be gone and is never checked
  (a `consumed` object still in the vault gets a new `orphaned` row);
  unattributed vault objects get an `orphaned` row. `orphaned`/`lost` are
  system-owned (`identity_id NULL`), counted on the dashboard, and never
  block reaping. The
  reaper gains an item teardown step: an identity is never deleted while it
  owns `held`/`listed` rows; `orphaned` rows never block it. Gate
  `playerBotProgression.lootEnabled`.
- **Engine touchpoints**: `CreatureManagerImplementation.cpp` (bot branch),
  `AiEconomyData.idl` + `AiEconomyManager`, `SimHunterController` (phase
  `LOOT_CORPSE`), `ServerDatabase.cpp` (schema 1013).
- **Persists**: vault container + contents (BDB), `simbot_items`.
- **Risk**: moderate–high. First real object movement out of a corpse; must
  mirror `lootAll`'s cross-lock (`Locker locker(ai, player)`) and the
  container transfer choreography; a vault that fails to load must fail the
  gate closed (never loot into a null container). Death mid-loot must leave
  the corpse lootable and the ledger consistent.
- **Test strategy**: kill → corpse has loot → bot loots → vault count and
  ledger rows +N, cash delta = corpse cash; corpse then despawns; bot killed
  between kill and loot → no ledger row, no orphan object; gate-off → corpse
  empty and 10 s despawn (legacy).

### F_0.9.4 — Group loot dispersal and group XP/credit split (P.10e)

- **Scope**: manager-formed **bot-only** `GroupObject` (create/addMember with
  the `GroupManager` lock choreography; no chat room/LFG). Loot rule mapping
  for bot groups: FFA → looter's identity; MASTERLOOTER → leader; RANDOM and
  LOTTERY → per-item random among members in range (LOTTERY is a player UI
  session and cannot apply). Credits split like `awardReward`; XP group
  multiplier via the F_0.9.1 branch. **Hard rule**: if any real player is in
  the group, bots never loot and never claim the split — the player path is
  untouched. Gate `playerBotProgression.groupLoot`.
- **Engine touchpoints**: `GroupManager` (a bot-group formation entry point
  beside the P.6.3c exception), `SimPlayerManager`, and the loot-winner
  calculation: `ThreatMap::getHighestDamageGroupLeader` **breaks out** when a
  grouped attacker's leader is not `isPlayerCreature()`
  (`ThreatMap.cpp:329-334`), so a bot-only group never reaches the loot
  branch. F_0.9.4 adds a roster-aware winner computed over `copyThreatMap`
  (manager-side, scoped to validated bot groups) and uses it only in the bot
  branch; the stock function is untouched. Solo bots are unaffected (the
  early-out is inside the grouped branch).
- **Persists**: `simbot_items`, `simbot_progression`.
- **Risk**: moderate. Group locks + corpse locks; production hunters still
  hunt solo, so this ships harness-exercised only (like `exitStructure` did).
- **Test strategy**: two harness identities grouped by the runner, one kill →
  items split per rule, both credited XP × group multiplier, mission credits
  halved; a player-in-group scenario (harness spawns nothing; asserts the
  branch is skipped by predicate).

### F_0.9.5 — Skill training and profession template build-out (P.10f)

- **Scope**: `trainSkill(identity, skill)` in the store: prerequisites
  checked against `simbot_skills` using the skill map's `getSkillsRequired()`
  (NOT `SkillManager::fulfillsSkillPrerequisites`, which rejects every
  ghostless creature at `SkillManager.cpp:938-940`), XP ≥ `getXpCost`
  debited from `simbot_experience`, 250-point budget mirrored
  (`skill_points_spent`); `simbot_skills` row. XP caps (introduced in F_0.9.1,
  finalised here): effective cap per type = max `Skill::getXpCap()` over the
  identity's trained skills, else the default limit — the stock rule at
  `SkillManager.cpp:802-824`; mirroring only `defaultXpLimits` would strand
  progression. Body application `applyProgressionToBody(agent)` at every
  spawn/respawn: `addSkill` + skill mods. **Skill-mod overlay (required in
  this chunk)**: `AiAgentImplementation::getSkillMod` (`:624-664`) returns
  the creature-list value whenever it is nonzero and only falls back to the
  template statistic otherwise, so a raw trained mod would *replace* the
  template's baseline. F_0.9.5 therefore applies `template baseline (from
  npcTemplate->getStatistic) + trained delta` as the creature-list value for
  every touched mod, and the harness asserts actual `getSkillMod` values
  before and after training (never lower). F_0.9.8 may later move baselines
  into profession templates, but F_0.9.5 ships correct on its own. A Lua
  **training plan** per profession drives autonomous training on the
  maintenance tick — **only for identities with an assigned plan**: the roster
  gains `training_plan` (persisted) which is empty for every existing hunter
  until F_0.9.8's allocator (or an explicit owner default in Lua) assigns it,
  so production training cannot pre-empt allocation; the harness assigns plans
  to its own identities directly. Abilities/schematics are ghost-only and out
  of scope (attacks stay template-driven). Gate
  `playerBotProgression.trainingEnabled`.
- **Engine touchpoints**: `SimPlayerManager`, `spawnPveIdentityBody` (`:8729`),
  `SkillManager` read-only, `ServerDatabase.cpp` + `swgemu.sql` (schema
  **1014**: `ALTER TABLE simbot_identities ADD COLUMN training_plan
  VARCHAR(64) DEFAULT NULL`) and the roster load/flush for the new field.
- **Persists**: `simbot_skills`, `simbot_experience`, `skill_points_spent`,
  `simbot_identities.training_plan`.
- **Risk**: low–moderate. Skill mods change combat numbers on the body; the
  plan must prove `addSkillMod` on an AiAgent is read by `CombatManager`.
- **Test strategy**: synthetic XP grant → train → row exists → destroy body →
  respawn → `hasSkill` true and mod present; insufficient XP → refused;
  budget exhausted → refused; gate-off → bodies unchanged.

### F_0.9.6 — Bazaar SELL (P.10g) — first human-shared surface

- **Scope**: city bazaar-terminal enumeration (the P.8.2 terminal scan
  pattern, `isBazaarTerminal()`); controller phase walks the body to the
  terminal for realism, but **no durable value is staged through the body**:
  a sim-aware `AuctionManager` entry (`addSimBotSaleItem(identityId, vault
  item, vendor, price, …)`) lists straight from the vault, records the
  identity as **seller** in `simbot_listings`, and charges the listing fee
  (`:739-760`, normally `player->subtractBankCredits`) from the **store**. On
  sale the seller payout goes to the store instead of the null-seller abort
  (`:1207-1215`); on expiry a manager task retrieves to the vault via an
  identity-mapped path (the OID owner check at `:1442-1452` cannot apply to a
  rebuilt body). **Journal schema** (`simbot_listings`): `operation_id` PK,
  `auction_object_oid`, `seller_identity_id` (nullable — a human seller),
  `buyer_identity_id` (nullable — a human buyer or unsold), `price`, `fee`,
  `phase`, `updated_at`. **The journal row is written first**, before any
  mutation, and every mutation advances exactly one phase: SELL =
  `prepared → fee_debited → auction_created → listed`, then
  `→ reserved → debited → committed → seller_paid → retrieved → done`
  on a sale, or `→ expired → retrieved → done` on expiry, or `→ refunded`;
  each transition is one idempotent step keyed by `operation_id`, so a
  crash between the fee debit and the auction creation replays from
  `fee_debited` (never charging twice, never leaving an unmapped live
  auction), a bot buying another bot's listing is one row with both
  identities, and replay at boot resumes from the recorded phase exactly
  once. Every phase boundary is a harness scenario. Gate
  `playerBotProgression.bazaarSell`.
- **Owner policy line (needed before this chunk's plan)**: may bots
  **undercut human listings**, and should bot prices be floored/ceilinged
  relative to the human market?
- **Engine touchpoints**: `AuctionManagerImplementation.cpp` (two guarded
  branches), `SimHunterController`/a small `SimMarketController` phase,
  `ServerDatabase.cpp` (schema 1015).
- **Persists**: auction items (stock BDB), `simbot_listings`, store credits.
- **Risk**: high (shared surface). Listings are real and visible; a bug here
  is player-visible. Mitigations: listing cap per identity, price floor,
  duration cap, kill-switch gate drains listings on disable.
- **Test strategy**: list → item appears in `AuctionsMap` with bot owner
  mapping; harness "buyer" purchase path (F_0.9.7 primitive or a scripted
  `doInstantBuy` by a harness identity) → seller store credited, buyer
  debited; expiry → item back in vault; gate-off → no listings exist.

### F_0.9.7 — Bazaar BUY (P.10h)

- **Scope**: need-driven purchase (gear from the training plan, consumables):
  query `AuctionsMap`, select by policy, then a sim-aware
  `AuctionManager` buy entry that debits the **store** (never the body's
  `CreditObject`), records the identity — not the transient body OID — as
  `buyer_identity_id` on the listing's journal row (phases `reserved →
  debited → committed → retrieved → done`), and retrieves into the vault
  through the identity-mapped path (stock retrieval enforces `buyerID ==
  player OID`, `:1442-1452`). Journaled and replayed at boot like SELL.
  "Fund the body and reconcile after" is explicitly **not** an allowed design
  (it stages durable credits on a disposable body). Gate
  `playerBotProgression.bazaarBuy`.
- **Owner policy line (needed before this chunk's plan)**: may bots **buy
  human listings** at all, or only bot/seeded (`market_seeder.lua`) listings?
  Recommendation until decided: **bot and seeded listings only**.
- **Engine touchpoints**: `AuctionManagerImplementation.cpp` (buyer branch),
  `SimPlayerManager`.
- **Persists**: store credits, `simbot_items`, auction state.
- **Risk**: high (real credits leave the store into a human seller's bank).
  Spend cap per identity per day; never bid, instant-buy only.
- **Test strategy**: seeded listing → bot buys → store debited by price,
  item in vault + ledger; insufficient credits → skipped; human-listing policy
  predicate asserted; gate-off → zero purchases.

### F_0.9.8 — Profession allocation policy (P.10i)

- **Scope**: population planner deciding which professions/templates each
  identity pursues; feeds F_0.9.5 training plans and F_0.9.7 buy lists;
  dashboard `population by template`. Ships with **option B templates** (§3).
  Gate `playerBotProgression.allocationPolicy = "fixed" | ...`.
- **Options** (owner picks; all read demand state read-only):
  1. **N of every template** — a Lua table of templates × count. Simple,
     deterministic, inspectable; ignores the economy; dead weight when a
     template has nothing to do.
  2. **Combat / crafting / support split** — percentages per role class,
     templates chosen round-robin inside a class. Predictable population
     shape, easy to reason about, still economy-blind inside a class.
  3. **Demand-weighted** — per-profession weight = normalised demand pressure
     of the profiles that profession serves (chef → food, armorsmith →
     hide/composite, …) with a floor per template; rebalanced on a slow
     cadence with hysteresis. Mirrors the P.4.5b "proportional rebalance"
     decision for miners; closes the loop; hardest to verify and can churn if
     hysteresis is wrong.
  4. **Hybrid (recommended)** — option 2's split as a hard envelope, option 3's
     weights inside each class, option 1's per-template floor. Deterministic
     bounds with an economic signal inside them.
- **Assignment semantics (v1)**: the allocator assigns a profession +
  `training_plan` to every identity with **zero skill spend** — which, because
  F_0.9.5 trains only identities that already hold a plan, includes the whole
  existing hunter roster at the moment F_0.9.8 is enabled (one-time
  assignment) as well as every new identity. An identity that has spent
  skills/points is **frozen**; respec is deferred to a later chunk with an
  atomic policy. Production candidate creation: `maxHunters` is generalised to
  a planner-owned population target (`maxPlayerBots`) so the mint loop
  (`SimPlayerManager.cpp:8586-8592`) grows the roster on the planner's
  demand, with the allocator choosing each new identity's profession at mint.
  `profession` and `training_plan` become persisted fields: the roster flush
  today never writes `profession` (`:8697-8707`), so F_0.9.5/F_0.9.8 add
  them to the UPDATE and the dashboard shows the assignment source.
- **Risk**: low mechanically; high in tuning. No object mutation of its own.
- **Test strategy**: planner is pure over a snapshot → unit-testable in
  `core3tests` (first P.10 code that is); harness asserts the roster's
  profession assignments match the planner output after one cadence.

## 5b. Owner's end-goal restatement (2026-09-03) — bots as novice players

The owner's stated end goal, after F_0.9.1 shipped: **rework the PvE bots into true
PlayerBots that begin as a novice (marksman / brawler / artisan) and progress as a
player would.** Most of that is already F_0.9.5 + F_0.9.8 — the training loop,
prerequisite checking against `simbot_skills`, `applyProgressionToBody`, and the
per-profession training plan all exist in those sections and already route around the
ghostless-creature wall. Three things it does NOT yet cover are recorded here.

### 5b.1 Starting state — mint at novice with a profession (new work)

F_0.9.5 trains *upward* from wherever an identity already is; nothing mints one *at*
novice with a profession assigned. Required: `SimBotIdentity` gains a profession
(currently hardcoded `"hunter"`) and the mint path grants the novice box for it
through the same `trainSkill` entry point F_0.9.5 builds — never a second code path,
per the one-creation-function invariant (§2). Existing identities are covered by
5b.3. The profession set the owner named is marksman / brawler / artisan; the
allocation policy that *chooses* between them is F_0.9.8, so until that lands the
assignment is a Lua-configured distribution.

### 5b.2 Body template — a neutral base to build from (new work)

Hunter bodies currently use the `death_watch_wraith` combat mob template. That is
wrong for a novice-profession bot on two counts: it is a level-178 template (the
reason F_0.9.1's cap derives level from `skillTier` and *never* from the body), and
it is a factioned combat mob rather than a plausible starting character. It is also
already an open owner item — the uncommitted nerf on `death_watch_wraith.lua` /
`rifle_t21.lua`, and the "stormtrooper → custom template" thread.

Direction (owner, 2026-09-03): **one default PlayerBot template, built up from
there** rather than a template per profession. Appearance and gear then come from
equipping real objects, not from swapping mobile templates.

### 5b.3 Migration of the existing roster — DECIDED 2026-09-03: retire and start clean

**Owner decision: retire the six existing `hunter` identities and mint fresh
novices.** They keep no history. This is the simpler path and it removes the need for
a backfill that would have had to invent a plausible skill set for a bot with 537
lifetime kills and no training record.

Retirement must go through the roster's existing delete path so the progression rows,
XP rows and any body go with it — `simbot_progression` is keyed by identity id and
F_0.9.0's reaper already counts and removes orphans, so a hunter row left behind
would surface as `orphanRecords` rather than vanish silently.

### 5b.3a First bot — ONE unarmed brawler → Teras Kasi (owner, 2026-09-03)

**Owner's chosen first target: a single brawler training unarmed, aiming at Teras
Kasi**, on the grounds that it is the easiest thing to test. That instinct is right
for a reason worth recording — it removes the hardest unsolved piece from the first
pass entirely.

**Unarmed needs no weapon-equip code at all.**
`bin/scripts/object/weapon/creature/creature_default_weapon.lua:62` declares
`xpType = "combat_meleespecialize_unarmed"`. Every creature spawns holding that
default weapon, and `ThreatMap::addDamage` keys damage off
`tarCreo->getWeapon()->getXpType()` — so an unarmed bot already credits the correct
XP type through the F_0.9.1 path that is shipped and verified, with no change.

Concretely, the brawler body path is the hunter path *minus* work: the hunter body
destroys the stock creature weapon and equips `rifle_cdef`
(`SimPlayerManager.cpp:11795-11815`). A brawler simply **skips that block** and keeps
the default weapon. That also sidesteps §5b.4's equip constraints for the first
profession — nothing needs to be worn for the bot to be correct.

**The skill ladder, verified against the character-builder terminal and trainer data**
(names come from `datatables/skill/skills.iff` in TRE, so they are not greppable in
`bin/scripts/skills/`):

| Stage | Skill boxes |
| --- | --- |
| Novice brawler | `combat_brawler_novice` |
| Unarmed branch | `combat_brawler_unarmed_01` … `_04` |
| Master brawler | `combat_brawler_master` |
| Novice Teras Kasi | `combat_unarmed_novice` |
| Four elite branches | `combat_unarmed_{accuracy,speed,ability,support}_01` … `_04` |
| Master Teras Kasi | `combat_unarmed_master` |

**Naming gotcha**: the Teras Kasi tree is internally `combat_unarmed_*`, **not**
`combat_teraskasi_*`. Searching skill names for "teraskasi" finds nothing — that
string only appears as an *attack-list* name in mobile templates
(`teraskasinovice` in `primaryAttacks`). Anyone wiring the training plan will hit this.

This gives F_0.9.5 a single, fully-specified ladder to prove the training loop against:
one identity, one XP type it already earns correctly, ~26 boxes with real
prerequisites and real XP costs, and a visible end state. Broadening to marksman and
artisan is then a Lua training-plan change, not new C++.

### 5b.4 Equipping real gear — PROVEN, with one constraint that shapes the design

The owner asked whether a bot could buy or loot a piece of armour and equip it, citing
the Jedi robe work. **That precedent is real and it works.** `frs_rank_outfits.lua`
lists genuine client-TRE wearables and `AiAgentImplementation.cpp:1128-1225` equips
one onto an AiAgent: `createObject(iff)` → `transferObject(wearable, 4, false)` →
`addWearableObject(wearable, false)`.

The constraint is documented in that same code and decides how far the idea can go:

> AiAgents use the generic `ContainerComponent`, unlike players. A successful slotted
> transfer therefore establishes containment but does not populate the CREO6
> equipment list. […] This deliberately avoids `PlayerContainerComponent`, so player
> race/certification checks and robe skill-mod handling remain unchanged.

Three consequences:

1. **Appearance works, and is clean.** The wearable is registered without a delta, so
   observers receive it in the creature's *initial baseline*. Equipping at spawn is
   proven. Equipping mid-life is the open question — already-observing clients may not
   see the change without a delta path.
2. **Stats do not follow.** Armour equipped this way establishes containment only;
   its protection values do not flow through the player equipment semantics. A bot
   would *look* armoured without *being* armoured.
3. **Player race and certification checks are bypassed**, not satisfied — the FRS code
   logs `raceListed` but does not enforce it. A bot could wear what a player could not.

**Design consequence.** Consequence 2 means "loot armour → become tougher" should
NOT be built by leaning on the player equipment path. The cheaper and more honest
route is the overlay F_0.9.5 already requires: it must apply
`template baseline + trained delta` as the creature-list skill-mod value because
`AiAgentImplementation::getSkillMod` (`:624-664`) returns the creature-list value
whenever nonzero. Armour protection can ride that same overlay as another delta
source, with the equipped object as its provenance. That keeps one mechanism for "why
is this bot's stat what it is" instead of two, and keeps the wearable purely cosmetic
at the container level where it already works.

**Open question for a spike, not for a chunk plan**: whether mid-life equip can
propagate visually without adopting `PlayerContainerComponent`. If it cannot, bots
re-equip at respawn and the visible loadout lags a body cycle — probably acceptable,
and worth confirming before F_0.9.3's loot chunk assumes otherwise.

### 5b.5 Sequencing consequence

The ghost question (§3) is upstream of more than F_0.9.5. F_0.9.3's hive item vault
exists *because* bots have no player inventory; if bots ever gained a real
`PlayerObject`, that chunk would simplify enormously — and would also inherit
`isPlayerCreature()`, which fences large amounts of engine behaviour both wanted
(loot, missions) and unwanted (the F_0.7.2 player-chat SIGABRT class, who.json player
counts, character rows). §3 already evaluated and rejected structural typing for
F_0.9.0's scope; **that evaluation should be revisited once, explicitly, before
F_0.9.3's plan is written**, because that is the chunk whose shape it changes most.

## 6. engine3 policy and inventory

**Inventory (2026-09-02)**: submodule at `ad80556104012c96378714274222e8b5fe5a6f21`
(`[added] ISO Timestamp Parsing`), gitlink matches. **One dirty file**:
`MMOEngine/src/engine/core/TaskManager.h`, 16 insertions / 16 deletions — every
`executeTask`/`scheduleTask` overload changed `auto taskObject = new LambdaTask`
to `Reference<LambdaTask*> taskObject = new LambdaTask` (lifetime safety).
Predates TRIP, surfaced to the owner 2026-07-15, excluded from every commit
since, decision still pending.

**Policy for P.10**: engine3 is in scope but discouraged. No chunk above needs
it — every touchpoint is Core3 (`CreatureManager`, `AuctionManager`,
`GroupManager`, `AiEconomyData.idl`, `SimPlayerManager`). If a chunk plan
proposes an engine3 change it must (1) justify it with the Core3-only
alternative considered and rejected, (2) be reviewed and committed in the
submodule first, then pinned in Core3. **Before the first such change** the
owner must explicitly commit-to-fork or discard the `TaskManager.h` diff so
the submodule starts clean. Still never hand-edit `src/autogen`; still never
touch `death_watch_wraith.lua` or `rifle_t21.lua`.

## 7. Common harness architecture (`playerBotParityTest`)

Modelled on `structureTraversalTest` (`sim_player_manager.lua:841`,
`StructureTraversalTestScenario` `SimPlayerManager.h:58`, runner under
`structureTraversalTestMutex` with scheduling outside the lock).

- **Identities**: the runner mints harness identities through the real mint
  function with `profession='harness'`; `governPvePopulation`, the
  matchmaker and the `maxHunters` count skip that profession; cleanup deletes
  identity + progression rows (reaper coverage for free), and from F_0.9.3 the
  teardown first releases the identity's items/listings.
- **SQL lane**: harness ops that touch SQL (mint, reload, inject, delete,
  reaper, forced flush) are **queued to the PvE maintenance task** and the
  runner polls for completion, re-arming the maintenance task at zero delay
  through its single-flight guard so scenario latency stays in seconds.
- **Bodies**: `SimParityTestController : SimPlayerController`, spawned by the
  same identity-body path as hunters (so `pveBodyIdentityIds` is populated the
  production way), AI tree IDLE-only override so combat sockets stay live.
- **Ops vocabulary** (grows per chunk): `grantXp`, `grantCredits`,
  `assertStore{...}`, `awardUnknownIdentity`, `destroyBody`, `respawnBody`,
  `injectOrphan`, `runReaper`, `flushNow`; later `spawnAttacker`+`kill`,
  `completeMission`, `loot`, `formGroup`, `train`, `list`, `buy`.
- **Oracle**: per-identity before/after snapshots of the store; per-scenario
  PASS/FAIL(reason) into a dashboard matrix under `playerBotProgression.harness`;
  fails closed when it cannot read the store.
- **Deliverables per chunk**: matrix in Lua, TRIP-verify receipt,
  `docs/4-unit-tests/live_p10<letter>-*.md`, CR in `docs/3-code-review`.

## 8. Owner decisions (ANSWERED 2026-09-02)

1. **Profession allocation policy — DECIDED: option 4, the hybrid.** A
   role-class split is the hard envelope, demand-weighted allocation chooses
   templates inside each class, and every template keeps a floor. F_0.9.8
   implements exactly this.

2. **Human-listing policy — DEFERRED, needs a formula.** Owner's framing:
   bots play forever while players need real time to harvest and add value, so
   the open question is whether bots become the market's *seed* (players
   consume) or its *competitor* (players are priced out). Until a formula
   exists, F_0.9.6/F_0.9.7 ship restricted: bots trade only with bot and
   seeded (`market_seeder.lua`) listings, never undercutting or buying a human
   listing. Those two chunk plans must open with the formula proposal
   (candidate levers: a price floor tied to real gather time, a per-family
   listing cap, a bot-visible market share ceiling, or bots buying only above
   the human ask). **No bazaar chunk starts without this decision.**

3. **engine3 `TaskManager.h` — DECIDED: leave it alone.** It stays
   uncommitted and excluded from every P.10 commit. No P.10 chunk touches
   engine3; a chunk that thinks it needs to must come back to the owner first.

4. **Structural typing — DECIDED by delegation ("best judgement, remember
   reusability, we do not need 20 of the same function").** Resolution: **no
   new type hierarchy and no duplicated logic.** Keep the runtime marker
   (`simPlayerBot`, already leak-proofed in `loadTemplateData`) as the only
   "is a bot" signal, and let the **roster identity** be the authority for
   anything durable — a body with no `pveBodyIdentityIds` mapping is not a
   PlayerBot for progression purposes, whatever flags it carries. `AiAgent`
   gains nothing. F_0.9.8's profession templates are *data* (mobile template
   names in Lua), not new C++ classes. One resolver
   (`resolvePlayerBotIdentity`), one creation function, one award API used by
   every capability chunk; each chunk adds callers, never a parallel
   implementation. The IDL subclass is **dropped**, not merely deferred — the
   identity-keyed store makes it unnecessary.

5. **"Created in exactly one place" — DECIDED by delegation ("best judgement,
   do not break Core3, we just must not end up with orphaned weapons and
   armor").** Resolution: the *intent* is no orphans, so the rule is stated as
   an invariant rather than a line count. **One function**
   (`ensurePlayerBotProgressionRecord`) is the only code that can create a
   progression record, it refuses any id absent from the loaded roster, and it
   is idempotent. It is invoked at roster mint and, because MyISAM cannot make
   the identity INSERT and the record INSERT atomic, again from reconciliation
   to repair a roster identity that has no record. Nothing else — no body, no
   OID, no award path — can create one. The orphan concern the owner actually
   named (weapons and armor) is covered separately by F_0.9.3's item vault:
   every item has a ledger row with an operation state, boot reconciliation
   validates each state against real containment, and an identity is never
   deleted while it still owns items or listings.
