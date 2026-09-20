# Code Review: F_0.9.7 P.10h — Skill-Derived Combat Abilities and SimBot Inspection

**Review Date**: 2026-09-19  
**Version**: 0.9.7  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/bin/web/aieconomy-dashboard/styles.css`
- `MMOCoreORB/src/server/web/APIProxyAiEconomyManager.cpp`
- `MMOCoreORB/src/server/web/APIProxyAiEconomyManager.h`
- `MMOCoreORB/src/server/web/RESTServer.cpp`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.cpp`
- `MMOCoreORB/src/server/zone/managers/combat/CombatManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgent.idl`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/bt/leaf/SimpleActions.h`
- `MMOCoreORB/src/server/zone/objects/creature/commands/PlayerInfoCommand.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `docs/ai-pve-playerbot-design.md`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAbilityTiers.h` (new)
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAbilityUsageLog.h` (new)
- `MMOCoreORB/src/tests/SimBotAbilityTiersTest.cpp`, `MMOCoreORB/src/tests/CombatSpecialCostTest.cpp` (new, gitignored by `*test*`)

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md`

---

## Executive Summary

This change derives combat abilities from trained SimBot skills, installs a gated skill-derived attack source, applies player-parity HAM and selection rules, and exposes the resulting state through inspection, dashboard, admin, telemetry, and parity-harness surfaces. All correctness, safety, harness, and documentation findings were addressed; lint, warning-free compilation, seven affected tests, a 64/64 parity matrix, and a 47-minute gated grind were reported successful.

APPROVED

---

## Changes Overview

The implementation adds ability derivation and tier collapsing, identity-scoped harness controls, configurable combat policy, bounded usage telemetry, and player-parity special-attack costs. It also adds REST/dashboard and in-game inspection, harness assertions, and defensive target teardown. The unrelated dirty `engine3` submodule and database backup were excluded from substantive findings.

---

## Findings

### Critical Issues

1. **Verification configuration enabled in the release working tree** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1159`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1163`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1803`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1809`  
   During live verification, gate-on scratch configurations appeared in place of the shipped configuration. This was accepted as an explicitly temporary verification override while runs were active. **Disposition: addressed** — the final on-disk configuration restores the parity harness, progression foundation, and combat-ability gate to default-off.

### Major Issues

1. **Harness combat-ability overrides affected production identities** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10473`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10499`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:13531`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15309`  
   Gate and chance overrides initially had global effect. **Disposition: addressed** — overrides now apply only to scoped identity IDs, production callers pass identities, and cleanup reapplies affected bodies after clearing scope.

2. **Gate-off inspection hid derived capability state** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15803`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15808`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15820`  
   Inspection could not distinguish absent derivation from a disabled or unapplied capability. **Disposition: addressed** — inspection now emits gate-independent `abilities.derived` and actionable `notInstalledReason` values.

3. **Camel-case skill commands failed exact registry lookup** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8177`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8182`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8188`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8532`  
   `skills.iff` names such as `unarmedHit1` did not match lowercase registry keys. **Disposition: addressed** — derived commands and configured exclusions are normalized before lookup and installation.

4. **Tier scenario leaked the global production-training gate** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1716`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1717`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1722`  
   A temporary `setTrainingGate` step was unnecessary because `freeGrant` bypasses training when the novice gate is armed. **Disposition: addressed** — the step was removed and the scenario uses only its scoped novice-gate setup.

5. **`awaitKillTargetDeath` incorrectly required `identityRef`** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14080`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14098`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15170`  
   The identity-required operation list rejected an existing harness scenario before the await operation could resolve its stored target identity. **Disposition: addressed** — only target spawning requires the scenario identity.

6. **Damaging zero-cost specials qualified as free-state controls** — `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3023`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3033`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3038`  
   Zero-cost unarmed attacks caused the forced-special path to dominate every swing. **Disposition: addressed** — free-state candidates must also be non-damaging control abilities.

7. **Free-state forcing selected a different command and retried every tick** — `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:2453`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:2459`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:2980`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:2992`  
   Passing control back to the general scorer selected `unarmedlunge1`, while a resisted or expired control could be re-forced continuously. **Disposition: addressed** — the qualifying command is selected exactly, and a per-target retry interval prevents immediate repetition.

8. **Free-state forcing ran only after a failed special roll** — `MMOCoreORB/src/server/zone/objects/creature/ai/bt/leaf/SimpleActions.h:271`, `MMOCoreORB/src/server/zone/objects/creature/ai/bt/leaf/SimpleActions.h:277`, `MMOCoreORB/src/server/zone/objects/creature/ai/bt/leaf/SimpleActions.h:285`  
   A 100% special chance could prevent Intimidate from ever entering the forced-control path. **Disposition: addressed** — free-state selection now runs before the configured special roll.

9. **HAM reserve rejected specials based on pools they did not spend** — `MMOCoreORB/src/server/zone/managers/combat/CombatManager.cpp:3356`, `MMOCoreORB/src/server/zone/managers/combat/CombatManager.cpp:3360`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3276`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3278`  
   Hurt bots refused genuinely free unarmed specials because unrelated low pools were tested against the reserve. **Disposition: addressed** — selection and execution enforce reserves only for pools with a non-zero command cost.

10. **State-invalid specials livelocked attack selection** — `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3298`, `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp:3305`  
    The scorer repeatedly selected lunge commands that the later `CheckAttackIsValid` stage rejected. **Disposition: addressed** — skill-derived candidates are prefiltered through `validateStateAttack`.

11. **Harness target destruction could race a pending despawn task** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12823`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12830`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12836`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12839`  
    A live run crashed in `notifyDespawn` after harness target teardown. **Disposition: addressed defensively** — teardown holds managed references and cancels/removes the pending despawn task before database destruction. The precise crash cause remained unconfirmed, but no recurrence was observed in later verification.

12. **Usage-ring eviction caused false-negative ability assertions** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAbilityUsageLog.h:37`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAbilityUsageLog.h:182`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotAbilityUsageLog.h:192`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14861`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14883`  
    An early Intimidate could leave the 12-entry ring during a longer fight, incorrectly failing `assertAbilityUse`. **Disposition: addressed** — per-body lifetime command counts and accumulated target-state masks survive ring eviction; combined command/state assertions still require a correlated ring entry.

### Minor Issues

1. **Harness cleanup could clear production telemetry** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:13549`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15315`  
   Disabling the shared log cleared histories and counters. **Disposition: addressed** — harness operations only enable logging, while cleanup owns the guarded disable transition.

2. **A scoped false override could disable production logging during refresh** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8688`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8692`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:13551`  
   Treating the armed harness value as authoritative could erase production telemetry. **Disposition: addressed** — logging is computed as configured-enabled or armed-true, and cleanup independently preserves configured production logging.

3. **Free-state redesign was missing from the plan and design documentation** — `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:72`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:248`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:595`, `docs/ai-pve-playerbot-design.md:662`, `docs/ai-pve-playerbot-design.md:683`  
   The documentation still described the obsolete boolean helper and omitted exact selection, retry state, and the new configuration knob. **Disposition: addressed**.

4. **HAM and state-validation prefilters were missing from the plan** — `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:82`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:257`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:264`  
   The plan did not record the two live-verification-driven selection filters or the lunge-livelock rationale. **Disposition: addressed**.

5. **Lifetime assertion evidence and memory bounds were undocumented** — `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:336`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:341`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:378`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:381`, `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md:605`  
   The plan still described only the ring and understated memory after lifetime aggregates were added. **Disposition: addressed** — it now documents aggregate semantics, ring-only correlation, fallback rules, eviction rationale, and the complete bound.

6. **Kill-award mismatch failures lacked actionable values** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12749`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12753`  
   Harness failures did not show the expected and actual direct/general awards. **Disposition: addressed** — the failure reason now includes both values and base XP.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — passed
- [x] 2. Code Quality — passed
- [x] 3. Architectural Compliance — passed
- [x] 4. Distributed Object / IDL Discipline — passed
- [x] 5. Lua/C++ Boundary — passed
- [x] 6. AI-Economy / Simulation Safety — passed; final shipped gates are default-off and live verification completed
- [x] 7. Error Handling — passed
- [x] 8. Security — passed
- [x] 9. Performance — passed; telemetry and per-tick work are bounded and cleanup removes per-body history

---

## Verdict

**APPROVED**

All review findings are closed. Temporary gate-on verification configurations were accepted only while live runs were active and were restored to the default-off release configuration. The unconfirmed harness despawn crash received engine-pattern defensive hardening and did not recur; final evidence reports clean lint, a warning-free build, seven passing affected tests, a 64/64 parity matrix, and a stable 47-minute gated novice grind.


---

## Release note (added at promotion, 2026-09-19)

**Live verification**: `LIVE_VERIFICATION_PASS`, receipt 27/27 —
`docs/4-unit-tests/live_p10h-skill-abilities_20260916-144849-p10h-skill-abilities.md`
(binary `9bb1be83`, clean config `83db5d29`).

**Gates ship default-off**: `playerBotProgression.combatAbilitiesEnabled` plus the
`combatAbilities` policy block (`specialAttackChancePercent` 35,
`preferFreeStateAbilities` true, `hamReserveFraction` 0.40, `freeStateRetrySeconds` 10,
`excludedCommands` = taunt/warcry1/warcry2, `usageHistorySize` 12). With the gate off every
agent — wild NPC, PvP bot, legacy hunter — takes the pre-existing code paths unchanged.

**Eight defects were found by live verification itself**, not by review: camelCase command
names that silently dropped nearly every derived ability; a HAM reserve that refused free
specials from a hurt bot; an invalid-special livelock against `CheckAttackIsValid`; free-state
forcing that picked the wrong ability and never backed off; area-aggro abilities pulling
bystanders; a harness regression in the identity-required op list; a harness assertion that
read only the bounded ring; and an undiagnosable award-mismatch message.

**Known open item — unreproduced SIGSEGV.** The first Run A attempt crashed in
`AiAgentImplementation::notifyDespawn` on a harness kill-target corpse whose `_this` weak
reference pointed at freed memory. Two defensive harness fixes ship (cancel and deregister the
pending corpse `DespawnCreatureTask` before `destroyObjectFromDatabase`; hold
`ManagedReference`s across kill-target spawn setup), but the crash did **not** reproduce in 9
boots and ~400 harness kills, including 6 full-matrix and 3 stress boots on a binary with both
fixes reverted. Root cause is unconfirmed. It is outside the F_0.9.7 ability path: the victim
was a wild creature with no skill-derived attacks and `simPlayerBot` false. Forensics:
`bin/log/trip-verify-20260916-144849-p10h-skill-abilities.crash1-*`.

**Deliberately unfixed behaviours**: the scorer's own heuristics are unchanged (F_0.9.7 only
adds pre-filters); `aim` and other self-target commands stay derivable; and the free-state
retry window is a flat per-target timer, not a per-command one.
