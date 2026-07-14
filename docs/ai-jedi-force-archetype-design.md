# P.7 — Jedi NPC Force Archetypes (Defender / Enhancer) & Force Management

**Status:** P.7.1 + P.7.1a + **P.7.2 (Transfer Force)** + **P.7.3 (Channel
Force)** all SHIPPED 2026-07-06 (compiled clean `-Werror`, **PENDING RESTART +
VERIFY** — checklist §7). The full ladder (rows 1–8) is now implemented. As-built
deltas from the original proposal are marked **[AS-BUILT]** below. **P.7.7
Force-accounting audit implemented 2026-07-13**: all activation, ongoing,
weapon-special, offensive-power, pool/regen, low-force recovery, and FRS rank
paths have been corrected and source/static-verified. Full build/live verify is
pending; the authoritative matrix and checklist are in
`ai-jedi-frs-rank-design.md` §6.9.

**P.7.1a (2026-07-06) — attack-stall regression fix.** First live look: NPCs
cast the powers but then STOPPED attacking / couldn't be attacked while they
walked to their combat destination. ROOT CAUSE: `runJediForceManagement`
executed powers via `enqueueCommand`, dropping non-combat self-buffs into the
SAME command queue the AI drives its attacks through (`enqueueAttack` →
`nextActionCRC`). The queue's `nextActionTime` gating (`CommandQueue.cpp:220`)
then stalled the queued attack behind the buff. The proven pattern is the AI
heal pipeline, which NEVER touches the queue — `HealTarget`
(`SimpleActions.h:688`) does `clearQueueActions(true)` then calls
`healCreatureTarget` DIRECTLY. FIX: self-buffs (target 0) now resolve the
`QueueCommand` and call `doQueueCommand(agent, 0, "")` **synchronously** —
identical command logic (checks, force cost, buff, client effect), zero queue
interaction, so attack pacing is untouched. Safe: `runBehaviorTree` holds the
agent lock (`AiBehaviorEvent.h:67`) and `runJediForceManagement` is
`@preLocked`; self-buffs only mutate the agent, so no cross-lock. Drain Force
(the one targeted *combat* command) KEEPS `enqueueCommand` — attack-type
actions belong in the combat queue and blend with the normal attack rhythm.
LESSON: for an AI to use an ability mid-combat, apply the effect directly
(heal pattern); do NOT enqueue non-combat commands into the AI's own attack
queue.

**Owner change at approval:** Force Resists are NOT reactive casts — they are
**assumed needed, always active, zero force cost**. Implemented as permanent
spawn skillmods on the Enhancer (base player buff value 25 each), so there is
nothing to cast, recast, or spam. Ranked NPCs add the side-correct
`round(force_control × 0.35)` FRS value to each resist (§2.2/P.7.7). Ladder row
2 remains removed and the activation cost remains zero.
**Owner ask:** Jedi NPCs should roll an archetype on spawn — **Defender** (more
defensive stats + Avoid Incapacitation) or **Enhancer** (the full self-buff /
force-economy suite: Force Armor/Shield/Feedback/Absorb, resists, Drain Force,
Transfer Force, Channel Force) — and manage their force pool like a player
(heals + buffs compete for the same force), **without spamming any single
ability**.

---

## 1. Verified engine facts (what already exists — file:line as of 2026-07-06)

The `healerType = "force"` work already built most of the substrate:

| Fact | Where |
|---|---|
| AiAgent has a force pool: `currentForcePoints` / `maxForcePoints` (+ get/set) | `AiAgent.idl:245-246, 1021-1035` |
| Pool is initialized 6850/6850 for **every** AiAgent | `AiAgentImplementation::initializeTransientMembers` (:233) |
| Force regen task exists: `AiForceRegenerationEvent`; current audited value is base 10 + skillmods per tick, reschedules 10s when full | `AiAgentImplementation.cpp`, `events/AiForceRegenerationEvent.h` |
| `JediQueueCommand` is AI-aware: checks/deductions use the exact FRS-modified command cost; legitimate zero-cost commands remain zero | `JediQueueCommand.h` |
| **Drain Force is fully AI-aware both directions** (attacker or target may be player or AiAgent; uses agent pool; force_absorb observer fires) | `DrainForceCommand.h` (whole file) |
| **Transfer Force is fully AI-aware both directions** | `TransferForceCommand.h:36-125` |
| **Channel Force is NOT AI-aware** — hard-requires `PlayerObject` ghost → `GENERALERROR` for NPCs | `ChannelForceCommand.h` |
| Self-buffs (Armor 1/2, Shield 1/2, Feedback 1/2, Absorb 1/2, Resist ×4, AvoidIncap) all route through `doJediSelfBuffCommand` → buff with skillmods via `createJediSelfBuff` | `JediQueueCommand.h:83-118, 166-218` |
| ⚠ `doJediSelfBuffCommand` **TOGGLES**: if the buff is already on, it *removes* it and returns SUCCESS. AI must check `hasBuff(crc)` first or it will strip its own buffs | `JediQueueCommand.h:84-88` |
| AvoidIncapacitation is the exception: it **renews** if present (never toggles off) | `AvoidIncapacitationCommand.h:20-40` |
| Buff mitigation is skillmod-based (`force_armor`, `saber_block`, `avoid_incapacitation`…). P.7.7 made Armor/Shield per-hit handlers pool-agnostic: NPCs pay the manipulation-adjusted cost and the buff drops at depletion | `JediQueueCommand.h`, `ForceArmor*Command.h`, `ForceShield*Command.h` |
| Heal pipeline precedent (the pattern to extend): BT `healDefault` → `CheckIsHealer` → `CheckHealChance` (heal cooldown + force ≥ 200) → `GetHealTarget` → `HealTarget` leaf → `healCreatureTarget` (deducts 200 force, heals level×20, sets `healDelay`) | `default.lua:138-155`, `Checks.cpp:586-616`, `SimpleActions.h:634-735`, `AiAgentImplementation.cpp:3061-3178` |
| HEAL tree socket ticks **in combat or LAIR_HEALING, not knocked down** | `default.lua rootDefault :298-307` |
| AiAgents execute queue commands via `enqueueCommand` (precedents: prone/kneel `SimpleActions.h:564-566`, throwgrenade `AiAgentImplementation.cpp:1902`) | — |
| BT leaves register by name: `_REGISTERLEAF(Name)` | `AiMap.h:38-41, 435, 491-492` |
| Template string fields parse like `healerType` | `CreatureTemplate.cpp:140` |
| Command costs/durations (Lua): Armor2 150/1800s, Shield2 150/1800s, Feedback2 100/60s, Absorb2 100/60s, Resists 250/900s, AvoidIncap 750/30s (renewable), Drain 50, Transfer 200, heal 200 | `bin/scripts/commands/*.lua` |
| Cooldown infra already on agents: `getCooldownTimerMap()` (used for `reaction_chat`) | `AiAgentImplementation.cpp:5921` |

**Historical P.7.1 implication:** the initial archetype phase needed no command
changes. Later live audits found player-only branches in ongoing mitigation,
Absorb, Force-power scaling, and special-attack costs; P.7.7 corrected those
shared command/combat paths for AiAgents.

---

## 2. Design

### 2.1 Archetype selection (spawn-time)

- New optional `CreatureTemplate` string field **`jediArchetype`**:
  `"defender"`, `"enhancer"`, or `"random"`. Empty (default) = feature OFF for
  that template — **zero behavior change for every existing NPC**.
- Parsed in `CreatureTemplate.cpp` exactly like `healerType`.
- New `AiAgent.idl` member `byte jediArchetype` (0=NONE, 1=DEFENDER,
  2=ENHANCER) + getter. Resolved in `loadTemplateData`: `"random"` → 50/50
  roll per spawn (each respawn re-rolls — "decides on spawn", per owner).
- Templates opting in first: `dark_jedi_sentinel.lua`,
  `light_jedi_sentinel.lua` with `jediArchetype = "random"`.

### 2.2 Spawn stat packages

Applied once in `loadTemplateData` after the archetype roll, via
`addSkillMod(SkillModManager::TEMPLATE, mod, value)` (plain skillmods — no
buff objects, nothing to expire, visible to the existing combat math):

| Archetype | Mods (v1 tuning — constants in C++, table kept here) |
|---|---|
| Defender | `saber_block +15`, `melee_defense +15`, `ranged_defense +15`, `force_defense +20` |
| Enhancer | `jedi_force_power_regen +15` over the base 10 (unranked total 25/tick; ranked total also adds the FRS regen bonus) + **[AS-BUILT, owner decision]** all Force Resist lines always active at zero activation cost: base 25 plus the side/rank control-derived bonus for `combat_bleeding_defense`, `absorption_bleeding`, `resistance_disease`, `absorption_disease`, `resistance_poison`, `absorption_poison`, `resistance_states` |

**[AS-BUILT] getSkillMod masking, not stacking:** the custom AiAgent
`getSkillMod` override (`AiAgentImplementation.cpp:556`) returns the
skillModList value whenever it is non-zero and never falls through to the
template `statistics` table. A naive `+15` would therefore have NERFED
dark_jedi_sentinel's `saber_block` from 85 to 15. Each mod is baked as
`npcTemplate->getStatistic(mod) + bonus` (`initializeJediArchetype`,
lambda `applyArchetypeMod`). Double-apply on `reloadTemplate()` is prevented
by the `jediArchetype != NONE` early-return (the roll also survives reload).

### 2.3 The decision brain — `runJediForceManagement()`

One method on `AiAgentImplementation`, invoked from a new BT leaf (§2.5).
Evaluates a **priority ladder top-down and casts AT MOST ONE power per
decision window**, then closes the window. This is the anti-spam core:

**Global pacing (CooldownTimerMap keys — existing infra):**
- `jedi_force_window`: 6–10s jittered between ANY two casts (jitter also
  prevents multiple Jedi in one camp from sync-casting).
- Per-power keys for the abilities that could otherwise chain (e.g.
  `jedi_avoid_incap` 45s, `jedi_drain_force` 20s).
- `jedi_force_threat`: a 20s memory refreshed by each incoming Force attack;
  Shield/Feedback/Absorb are reactive to this signal instead of merely seeing
  that the opponent owns a Force pool.
- `jedi_force_feedback`: 120s and `jedi_force_absorb`: 180s. Their buffs last
  60s, so these cooldowns intentionally prevent continuous maintenance.

**Force floor:** never cast (except the incap emergency) if it would leave
`currentForce < 400` — always reserve two force heals (2×200). Heals stay
king: the existing heal pipeline runs BEFORE this ladder in the tree and
`CheckHealChance` already refuses below 200 force.

**Anti-toggle:** every self-buff is gated on `!hasBuff(crc)` (because
`doJediSelfBuffCommand` toggles). Recast happens only when the buff has
actually expired/been dispelled — the buff's own duration (30 min for
Armor/Shield!) is the natural rate limiter.

**The ladder:**

| # | Condition | Action | Archetype |
|---|---|---|---|
| 1 | health < 35% AND force ≥ 1000 AND `jedi_avoid_incap` cd past (45s) | **Avoid Incapacitation** (renews; 750) | Defender |
| 1' | health < 35% AND Armor2 missing AND force ≥ 150 (may dip into heal reserve) | **Force Armor 2** now (jump the queue) | Enhancer |
| 2 | ~~reactive resists~~ **[AS-BUILT]** always active as spawn skillmods, zero cost (owner decision, §2.2) | — | Enhancer |
| 3 | Armor2 missing | **Force Armor 2** (150/1800s) | Enhancer |
| 3' | Armor1 AND Armor2 missing (Armor2 blocks Armor1) | **Force Armor 1** (75/1800s) | Defender |
| 4 | hit by a Force attack in the last 20s AND Shield2 missing | **Force Shield 2** (150/1800s) | Enhancer |
| 5 | recent Force threat AND Feedback2 missing/cooldown past, then Absorb2 missing/cooldown past | **Force Feedback 2** (100/60s buff, 120s recast) / **Force Absorb 2** (100/60s buff, 180s recast) | Enhancer |
| 6 | own force < 40% of max AND target is a force user with force > 0 AND `jedi_drain_force` cd past (20s) | **Drain Force** on target (already AI-aware) | Enhancer |
| 7 | **[AS-BUILT, P.7.2 SHIPPED]** self force > 65% of max AND `jedi_transfer_force` cd past (20s) AND a friendly force-user ally ≤30m with force < 25% of their max exists | **Transfer Force** on that ally (AI-aware, applied DIRECTLY like a heal — beneficial, non-combat) | Enhancer |
| 8 | **[AS-BUILT, P.7.3 SHIPPED]** force < 20% of max AND `jedi_channel_force` cd past (30s) AND all three HAM pools > 60% | **Channel Force** (HAM → force; self, non-combat, applied DIRECTLY) | Enhancer |
| — | none matched | cast nothing this window | both |

**[AS-BUILT] P.7.8 endurance tuning (2026-07-13, pending build/live
verification).** Incoming Force attacks are recorded by `CombatManager` on the
defending archetype Jedi's cooldown map. This makes the expensive anti-Force
suite threat-driven: saber-only Jedi combat no longer causes speculative
Shield/Feedback/Absorb casts. Generated archetype-Jedi sabers are normalized
from the raw gen-4 template values (40–48) to a crafted-like base cost of 10,
so command multipliers charge 10–30 rather than up to 144. At or below 20% of
maximum Force, P.7.8 originally used zero-Force `defaultattack`; the first live
P.7.9 ledger showed this reserve was unnecessary. P.7.10 now keeps selecting
any exactly affordable skill and uses `defaultattack` only when none can be
paid. Healing and Channel Force remain independent management decisions.
The Imperial Dark Jedi Master template is now correctly fixed at FRS rank 11
(8800 maximum Force, 45 regen per two-second tick) rather than rolling rank
8–10. Full calculations and fight-log diagnosis are in
`ai-jedi-frs-rank-design.md` §6.10.

Build follow-up (2026-07-13): the P.7.8 threat guard in
`CombatManager.cpp` now qualifies the generated AiAgent constant as
`AiAgent::JEDI_ARCHETYPE_NONE`; the first external compile correctly rejected
the unqualified identifier outside AiAgent implementation scope. The following
build reached the final link but its external `SimPlayerManager.cpp.o`
contained the complete `SimPvPController.cpp` implementation. Repository
inspection confirms the two sources are distinct, each is compiled once, and
there is no `.cpp` inclusion; this requires deployed-source verification and a
targeted object/ccache rebuild rather than a Jedi or PvP code change.

**[AS-BUILT] P.7.3 Channel Force (2026-07-06, compiled clean, PENDING
RESTART+VERIFY).** Two parts: (1) `ChannelForceCommand` made AI-aware, mirroring
the Drain/Transfer pattern — force pool read/written via `getCurrentForce()`/
`setCurrentForce()`/`getMaxForce()` for AiAgents vs the player ghost, plus the
AI bypass of the inline `isWearingArmor` check. **Verified: `ChannelForceBuff`
has ZERO ghost dependency** — it operates on `CreatureObject* creature` only
(HAM modifiers via `addMaxHAM`/`setAttributeModifier`, tick event
`ChannelForceBuffTickEvent`), so the HAM-drain-and-restore buff works unchanged
on NPCs. (2) Enhancer ladder row 8: fires when `curForce < getMaxForce()/5`,
cooldown past, and every HAM pool > 60% of max — self, non-combat, so it takes
the direct/synchronous path (combatCommand stays false). The ~250–350 force
refill lifts the agent back above 20% so it self-limits; 30s cooldown backstops.
Mutually exclusive with Transfer (needs self > 65%) and lower-priority than
Drain (drains a force-user enemy for less HAM risk), so Channel is the fallback
refill when there's no one to drain. §4 (deferred AI patch) is now DONE.

**[AS-BUILT] P.7.2 Transfer Force (2026-07-06, compiled clean, PENDING
RESTART+VERIFY).** Ally discovery reuses the AI heal pattern exactly: allies =
the current combat target's defender list (everyone fighting the same enemy),
scanned lock-free like `GetHealTarget`'s loop. Filters: skip self/dead/incap,
≤30m (`isInRange3d`; command range is 32 so 30 gives margin), not aggressive /
same side, and only force-users actually below 25% (AiAgent: jediArchetype or
healerType force + `getCurrentForce() < getMaxForce()/4`; player:
`getForcePower() < getForcePowerMax()/4`). First qualifying ally wins.
Execution: Transfer Force is beneficial (uses `isHealableBy`, never
`startCombat`), so it goes through the **direct/synchronous** path with the
self-buffs (not the combat queue) — its internal `Locker(target, agent)` is the
same blessed cross-lock the heal path uses. `combatCommand` bool now selects
queue vs direct (only Drain Force sets it), replacing the earlier
`castTarget != 0` test so a *targeted* non-combat power stays direct.

"Opponent is a force user" = follow target is an AiAgent with
`healerType=="force"` / maxForce > 0, or a player with `forcePower > 0` —
matches Drain Force's own target logic.

**Emergent force management:** early fight = Armor (+Shield vs Jedi) ≈ 300
force; mid fight = heals (200 each) with permanent resists protecting it; low force = Drain the
opposing Jedi (net +120..+370) or, once P.7.3 lands, Channel HAM→force when
safe. Defenders barely spend (Armor1 + rare AvoidIncap) — their kit is the
passive stat package, exactly the profession contrast the owner described.

### 2.4 Execution path **[AS-BUILT, revised by P.7.1a]**

- **Self-buffs (non-combat, target 0):** resolved via
  `objectController->getQueueCommand(crc)` and called **synchronously** with
  `doQueueCommand(agent, 0, "")` — the HealTarget-style direct application.
  This is the fix for the P.7.1a attack-stall regression. Command logic, force
  cost, buff, and client effect all run; nothing enters the AI command queue.
- **Drain Force (targeted combat command):** stays on
  `enqueueCommand(crc, 0, targetID, "")` — a combat/attack-type action that
  belongs in the combat queue and paces with normal attacks.
- The original all-`enqueueCommand` approach broke combat because non-combat
  self-buffs in the shared queue stall the AI's own queued attacks. See the
  P.7.1a note at the top.

### 2.5 Behavior-tree wiring (mirrors the healer precedent)

- Two new leaves, registered via `_REGISTERLEAF` in `AiMap.h`:
  - `CheckJediForceChance` (Checks.cpp): fast gate — `jediArchetype != 0 &&
    (knocked down || cooldown "jedi_force_window" past)`. Exact force
    requirements belong to each selected action; there is no blanket minimum,
    so Channel Force and low-cost state recovery remain reachable. One byte
    compare for the other 99% of NPCs.
  - `ManageJediForce` (SimpleActions.h): calls
    `agent->runJediForceManagement()`, always returns SUCCESS.
- One new branch in `rootDefault` (default.lua), inserted into the top
  Selector **before** the combat/heal sequence, wrapped in `AlwaysFail` so the
  selector always falls through to heals/attacks (the exact idiom the MOVE
  branch uses at `default.lua:292`):
  ```
  AlwaysFail → Sequence → If CheckIsInCombat
                        → Not CheckPosture KNOCKEDDOWN
                        → If CheckJediForceChance
                        → ManageJediForce
  ```
- v1 ticks **in combat only** (same gate as heals). Fine: Armor/Shield last
  30 min, so after the first fight the Jedi walks around pre-buffed anyway.
  Out-of-combat pre-buff on AWARE is a P.7.3 polish option.

### 2.6 Phasing

- **P.7.1 (this proposal):** archetype roll + stat packages + ladder rows
  1–6. Zero command-class changes.
- **P.7.2 — SHIPPED 2026-07-06** (compiled clean, pending restart+verify).
  Transfer Force with an ally scan; reused the enemy-defender-list ally pool
  from `GetHealTarget` rather than a fresh CloseObjects scan (owner's steer:
  "AI already heals friendlies in the area, reuse that"). See ladder row 7
  AS-BUILT note.
- **P.7.3 — SHIPPED 2026-07-06** (compiled clean, pending restart+verify).
  Channel Force AI branch (§4 done — see ladder row 8 AS-BUILT note).
- **P.7.7 — IMPLEMENTED 2026-07-13** (source/static-verified, full build/live
  pending). The formerly optional Armor/Shield per-hit force drain is complete,
  together with the broader accounting/rank audit documented in
  `ai-jedi-frs-rank-design.md` §6.9. Remaining optional polish is AWARE-slot
  pre-buffing and live tuning of ladder thresholds.

---

## 3. Files touched (P.7.1)

| File | Change |
|---|---|
| `src/server/zone/objects/creature/ai/CreatureTemplate.h/.cpp` | `jediArchetype` string field + getter + parse (mirror `healerType`) |
| `src/server/zone/objects/creature/ai/AiAgent.idl` | `byte jediArchetype` + getter/setter; declare `runJediForceManagement()` |
| `src/server/zone/objects/creature/ai/AiAgentImplementation.cpp` | archetype roll + stat package in `loadTemplateData`; `runJediForceManagement()` ladder |
| `src/server/zone/objects/creature/ai/bt/leaf/Checks.h/.cpp` | `CheckJediForceChance` |
| `src/server/zone/objects/creature/ai/bt/leaf/SimpleActions.h` | `ManageJediForce` |
| `src/server/zone/managers/creature/AiMap.h` | 2 × `_REGISTERLEAF` |
| `bin/scripts/ai/default.lua` | new `AlwaysFail` branch in `rootDefault` |
| `bin/scripts/mobile/thug/dark_jedi_sentinel.lua`, `bin/scripts/mobile/faction/rebel/light_jedi_sentinel.lua` | `jediArchetype = "random"` |

## 4. Channel Force AI patch (P.7.3) — DONE 2026-07-06

Mirrored the owner's Drain/Transfer adaptation in `ChannelForceCommand.h`:
branch on `isPlayerCreature()/isAiAgent()` for the force read/write
(`ghost->get/setForcePower` vs `agent->get/setCurrentForce` + `getMaxForce`),
plus the AI bypass of the inline `isWearingArmor` check. **VERIFIED before
shipping: `ChannelForceBuff` has NO ghost dependency** — `ChannelForceBuffImpl`
operates entirely on `CreatureObject* creature` (HAM via `addMaxHAM` /
`setAttributeModifier`, tick via `ChannelForceBuffTickEvent`, no
`getPlayerObject()` anywhere), so the 180s HAM-drain-then-restore mechanic works
unchanged on NPCs. Ladder wiring: row 8 (§2.3).

## 5. Safety analysis

- **No object lifecycle operations.** No create/destroy/transferObject — the
  vehicle-mount lesson respected. Everything is buffs, skillmods, and integer
  pool math on already-existing creatures.
- **Locking:** BT leaves run with the agent locked (HealTarget precedent).
  `enqueueCommand` is the standard thread-safe player path; cross-creature
  commands (Drain) do their own `Locker clocker(target, creature)` internally.
  No new locks, no lock-order changes.
- **Gated & reversible:** dormant unless a template sets `jediArchetype`;
  removing the field fully reverts. The BT branch costs one byte-compare per
  combat tick for non-Jedi.
- **Simulation-safe:** no economy/inventory/market/persistence interaction.
  `jediArchetype` on AiAgent is transient (re-rolled per spawn) — nothing
  persisted.
- **NPC-only messaging:** buff start/end system messages are no-ops on NPCs;
  client effects broadcast normally.

## 6. Risks / open questions

- **R1 — RESOLVED at implementation:** `ObjectController::activateCommand`'s
  `characterAbility` gate is inside `if (object->isPlayerCreature())`
  (`ObjectControllerImplementation.cpp:85`) — NPCs pass straight through.
  `enqueueCommand` shipped as the execution path.
- **R2 — MITIGATED (shipped):** `doCommonJediSelfChecks` now skips
  `isWearingArmor()` for AiAgents (`JediQueueCommand.h`) — NPC outfit pieces
  are cosmetic and would have silently blocked every self-buff.
- **R3 — RESOLVED P.7.7:** Force Armor and Force Shield per-hit handlers now
  support PlayerObject and AiAgent pools, use the exact manipulation-adjusted
  ratio, and remove the buff without overdrawing at depletion.
- **R4 — RESOLVED at implementation:** worse than double-count — skillModList
  values MASK template statistics entirely in the custom getSkillMod. Handled
  by baking `statistic + bonus` (§2.2 AS-BUILT note).
- **R5 (pre-existing, separate decision):** every AiAgent in the galaxy gets
  6850 force + a perpetual regen task from `initializeTransientMembers`. Works,
  but thousands of 10s tasks tick for NPCs that never spend force. Optional
  cleanup: pool only when `healerType=="force"` or `jediArchetype` set (regen
  event already no-ops when maxForce ≤ 0). Owner's call — not required for P.7.
- **R6 (accepted):** a failed cast (stunned, dizzy, etc.) still closes the
  6–10s window — enqueueCommand can't report failure back. Self-correcting
  next window; guarantees no machine-gun retry.

## 7. Verification checklist (post-restart) — P.7.1 shipped, run this next

1. ~~Build `-Werror` clean; lua valid~~ DONE 2026-07-06 (3m49s incremental,
   first try; luac -p clean on default.lua + both sentinel templates).
2. Spawn ~6 dark_jedi_sentinel → debug log `initializeJediArchetype: rolled`
   shows a mix of defender/enhancer (or infer from behavior: armor-2 casts =
   enhancer).
3. Enhancer fight: `pl_force_armor_self.cef` fires ONCE, buff persists 30 min,
   force drops by cost, no recast while buffed (anti-toggle proven), casts
   ≥6s apart (window proven), Drain Force fires vs a Jedi opponent when low.
   Console shows a `[JediForceAccounting]` line for every Force mutation; the
   next line's `before` equals the prior line's `after` for that NPC.
4. Defender fight: drop it below 35% HP → Avoid Incap effect
   (`pl_force_avoid_incap_self.cef`) + incap actually avoided; not renewed
   more than ~once/45s.
5. Force pool: watch regen (enhancer visibly faster); heals still fire and
   still refuse below 200 force.
6. Regression: non-Jedi NPC combat unchanged; a dark-vs-light sentinel fight
   (both archetypes); no crash across several kill/respawn cycles.
7. **P.7.2 Transfer Force:** two allied Jedi vs one enemy — drive one below 25%
   force; a partner above 65% should Transfer to it (transfer anim + combat
   spam, low Jedi's force jumps), neither one's attacks pausing; ≤ once/20s.
8. **P.7.3 Channel Force:** drive an Enhancer below 20% force while its HAM is
   healthy and it has no force-user enemy to drain → it Channels (HAM drops,
   force jumps ~250–350, `channelforcebuff` applied, HAM restores over 180s);
   does not re-fire until force bottoms out again; ≤ once/30s. Confirm HAM
   restore actually happens on the NPC (buff tick has no ghost dependency).
9. **P.7.7 accounting/ranks:** run the cross-council, mitigation-depletion,
   Absorb, Force-power, saber-special, and low-force recovery checks in
   `ai-jedi-frs-rank-design.md` §6.9. Confirm the dashboard's current/max pool,
   control/power/manipulation, effective regen, and live buff values match the
   tested rank table.
10. **P.7.8 endurance:** an Imperial Dark Jedi Master reports rank 11, 8800
    maximum Force, 45 regen/tick, and saber cost 10. In a saber-only fight it
    does not use Shield/Feedback/Absorb. After a real Force hit it reacts with
    those defenses and obeys the 120s/180s Feedback/Absorb recast cooldowns.
11. **P.7.9 complete Force ledger:** filter one spawned NPC's log on its
    `agent=<object id>` and `[JediForceAccounting]`. Begin at
    `event=snapshot source=spawn_ready`, then confirm every `before` matches
    the preceding `after` and final Force equals the snapshot plus all
    `actualDelta` values.
    The ledger covers activation
    and Force-attack costs, saber specials, Armor/Shield per-hit costs, heals,
    state recovery, regen, Absorb, Drain/Transfer/Channel, insufficient-cost
    blocks, and explicit zero-cost `defaultattack` actions. A rank-0 NPC Knight
    starts at 6940 Force and regenerates 11 (Defender) or 26 (Enhancer) every
    two seconds.
12. **P.7.10 last stand:** below 20% Force, confirm the NPC continues using
    every affordable attack skill. Current Force must be strictly greater than
    a saber special's calculated cost; Force attacks may equal their cost.
    Only when every candidate is unaffordable should the ledger show
    `source=defaultattack`.

## 8. P.7.1–P.7.3 as-built file list (historically compiled clean `-Werror`)

| File | Change |
|---|---|
| `ai/CreatureTemplate.h/.cpp` | `jediArchetype` string field + getter + lowercase parse |
| `ai/AiAgent.idl` | `JEDI_ARCHETYPE_NONE/DEFENDER/ENHANCER` consts; `int jediArchetype` member (+ctor init); get/set; native `initializeJediArchetype()` + `runJediForceManagement()` |
| `ai/AiAgentImplementation.cpp` | +`SkillModManager.h` include; `initializeJediArchetype()` called at end of `loadTemplateData(CreatureTemplate*)`; both methods implemented after the force-regen block (~:715). Ladder rows 1–8 incl. Transfer Force ally scan (P.7.2) + Channel Force (P.7.3); `combatCommand` flag selects queue (Drain) vs direct (all else) execution (P.7.1a) |
| `commands/ChannelForceCommand.h` | **(P.7.3)** AI-aware force read/write (AiAgent pool vs player ghost) + AI `isWearingArmor` bypass |
| `ai/bt/leaf/Checks.h/.cpp` | `CHECK_JEDIFORCECHANCE` + `CheckJediForceChance`; P.7.7 removed the blanket force ≥250 gate and lets KD recovery bypass the global window |
| `ai/bt/leaf/SimpleActions.h` | `ManageJediForce` leaf → `runJediForceManagement()`, always SUCCESS |
| `managers/creature/AiMap.h` | 2 × `_REGISTERLEAF` |
| `bin/scripts/ai/default.lua` | `rootDefault`: AlwaysFail-wrapped branch, node ids 3811110001–3811110010, before the heal/combat sequence |
| `commands/JediQueueCommand.h` | AiAgent bypass of `isWearingArmor` in `doCommonJediSelfChecks` (R2) |
| `mobile/thug/dark_jedi_sentinel.lua`, `mobile/faction/rebel/light_jedi_sentinel.lua` | `jediArchetype = "random"` |
