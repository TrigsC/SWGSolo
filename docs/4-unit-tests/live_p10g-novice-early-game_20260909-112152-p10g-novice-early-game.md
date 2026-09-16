# Live verification — F_0.9.6 (P.10g novice early-game loop)

**Run ID**: `20260909-112152-p10g-novice-early-game`
**Plan**: `docs/1-plans/F_0.9.6_p10g-novice-early-game-loop.plan.md`
**Branch**: `feat/p10g-novice-early-game-loop`
**Verdict**: **LIVE_VERIFICATION_FAIL** — no receipt recorded.

> **Superseded — read `live_p10g-novice-early-game_20260913-171447-p10g-novice-early-game.md`.**
> "Defect 2" below is wrong as written. The deterministic kill failures came from
> the verification config: `playerBotProgression.awardKillXp` was `false` in every
> run, including both A/B arms, so no kill XP was ever awarded and
> `awaitKillTargetDeath` could never settle. It was not a pre-existing combat
> defect and not world state. The 2026-09-13 run found a separate,
> *intermittent* kill-scenario failure **with the gate on**. There the bot
> demonstrably killed the target, but the harness's exact-XP settlement did not
> complete.

## Summary

Two of this chunk's headline assertions are **verified on the live server**: the
canonical template derives exactly 63 boxes / 250 skill points, and all three
Fencer/Medic branch identities are confirmed from granted skill mods and
commands rather than assumed. Verification nevertheless **fails**: one of this
chunk's six new parity scenarios carries an incorrect expectation, and two
**pre-existing** real-combat scenarios are broken on this server independently
of F_0.9.6 (proven by an A/B against HEAD).

## Changed subsystems

`SimPlayerManager.{h,cpp}`, `SimHunterController.{h,cpp}`,
`CombatManager.{h,cpp}` (one new read-only estimator),
`sim_player_manager.lua`, plus three docs. 2499 insertions / 106 deletions.

## Static gate

| Check | Result |
|---|---|
| Incremental build, `-Werror` | **PASS** — warning-clean, 0 warnings/errors |
| `luac -p` on the one changed Lua file | **PASS** |
| Affected GoogleTest filters | **N/A** — no suite covers these files (see COVERAGE-DEBT) |

## Restart / readiness

Seven guarded restarts through `server-cycle.sh`; readiness 7-19 s each; 0
connected players each time; no crash, no GDB backtrace, no new fatal signature.

## Assertions

| # | Assertion | Result | Evidence |
|---|---|---|---|
| C1 | Clean startup, process alive, no crash/backtrace | **PASS** | 7 restarts, fresh `who.json` 7-19 s |
| B1 | `pve_melee_medic` boxCount == 63 | **PASS** | dashboard `training.plans` |
| B2 | `pve_melee_medic` totalPoints == 250 | **PASS** | exactly the 250 cap, zero slack |
| B3 | `pve_melee_medic` loadError empty | **PASS** | `(none)` |
| B4 | `goalSkillDetails` present for all 6 goals | **PASS** | xpType/skillMods/commands emitted |
| B5a | Fencer `3xxx` grants melee+ranged defense | **PASS** | `combat_1hsword_accuracy_03` → `melee_defense:12`, `ranged_defense:12` |
| B5b | Fencer `x2xx` grants knockdown defense | **PASS** | `combat_1hsword_speed_02` → `knockdown_defense:20`, `posture_change_down_defense:20` |
| B5c | Medic `2xxx` grants First Aid | **PASS** | `science_medic_injury_02` → command `firstAid` |
| A2a | `multi_goal_plan_topology_and_scale` | **PASS** | oracle independently re-derives the closure, no duplicates, 63/250 |
| A2b | `scalar_goal_skill_backward_compatibility` | **PASS** | scalar `goalSkill` still loads |
| A2c | `deferred_box_blocks_when_early_game_is_off` | **FAIL** | `cached_next_step_mismatch` — see Defect 1 |
| A2d | `deferred_box_skips_and_mobile_damage_when_on` | **NOT RUN** | blocked by A2c |
| A2e | `real_weapon_damage_template_value_when_on` | **NOT RUN** | blocked by A2c |
| A2f | `fundable_graduation_and_grind_order_guard` | **NOT RUN** | blocked by A2c |
| A3 | No pre-existing scenario regresses | **PASS (with 2 exclusions)** | 53/53 runnable pre-existing scenarios PASS; 2 excluded, see Defect 2 |
| A4 | `orphanRecords == 0`, `rosterWithoutRecord == 0` | **PASS** | 0 / 0 at baseline and during the run |
| C3-C9 | Live novice grind loop (movement, TTK, real kill, demand independence) | **NOT RUN** | Part C never started; the matrix never went green |

## Defect 1 — harness authoring error (this chunk, blocking)

`deferred_box_blocks_when_early_game_is_off` asserts the cached next box is
`science_medic_novice` with xpType `medical`. Both assumptions are wrong, and a
live probe proves it:

```
combat_brawler_novice   xpType=(none)  xpCost=0  skillPoints=15
science_medic_novice    xpType=(none)  xpCost=0  skillPoints=15
```

1. Both ladder roots are **free** — xpCost 0 and **no xpType at all**. The
   Kahn tie-break therefore falls through to name, and
   `combat_brawler_novice` sorts before `science_medic_novice`.
2. `science_medic_novice` does not require `medical` XP in the first place.
   Only `science_medic_injury_01/02` do, so this scenario could not have
   exercised blocked-vs-skipped even with the correct box name.

**The production code is not implicated.** Skip-tolerant derivation is gated on
`earlyGameGateEnabled` and keys off `deferredXpTypes`, which is correct; the
scenario simply asserts a state the ladder never enters. The fix belongs in
`TRIP-2-implement`: give the scenario its own single-goal plan
(`goalSkills = {"science_medic_injury_01"}`), free-grant `science_medic_novice`,
and then assert that the next box is `science_medic_injury_01` / `medical` with
the gate off, and is skipped with the gate on.

An incidental architectural fact worth keeping: **master boxes and novice boxes
carry an empty xpType and zero xpCost** — they are paid for in skill points
only. `medical` therefore enters the template solely through the two
`science_medic_injury_*` boxes.

## Defect 2 — sim-bot combat is broken on this server (PRE-EXISTING, out of scope)

Both real-combat scenarios fail with `step_budget_exceeded` and zero damage:

- `unarmed_novice_kills_nuna` (brawler, stock creature weapon)
- `harness_bot_kills_nuna_and_earns_xp` (rifle bot, F_0.9.1's call-site proof)

The bot engages successfully — `CombatManager::startCombat` returns true, the
target spawns 3 m away, `activateAiBehavior(true)` fires — and then lands **no
damage for 10 minutes** (verified by raising the step budget from 180 s to
600 s; `creatureKillsTotal` stayed 0 throughout).

**Attribution by A/B, not by argument.** The working tree was reverted to HEAD,
rebuilt (binary linked 19:19:13), and the original 54-scenario matrix run in the
same environment with the same `maxHunters`: **17 PASS, then
`unarmed_novice_kills_nuna` FAIL, `kills=0` — identical.** F_0.9.6 is therefore
not the cause. F_0.9.5's receipt (`live_p10f-skill-training_20260906-223926`)
records both scenarios passing with `kills=1`, `totalAwarded=93`, so something
in the world/server state has changed since 2026-09-06.

This matters beyond the harness: **the entire premise of F_0.9.6 is novices
earning XP from real kills.** Until this is found, the early-game loop cannot
function in production even with its gates on. It deserves its own
investigation chunk.

Two hypotheses were tested and **disproved** rather than assumed:

- *Body-slot contention* — `attachedBodies` sat exactly at `maxHunters`, and the
  roster carried leftovers from the previous verification run. Raising
  `maxHunters` to 12 freed the slots and the scenario **still failed**.
- *`creatureKillsTotal == 0` as evidence* — the pre-change baseline is **also 0**.
  That is just the saturated-demand state this chunk exists to fix.

Also ruled out by inspection: the new equip postcondition (a failed
postcondition destroys the body at `SimPlayerManager.cpp:15391`, and
`respawnBody` PASSed); the early-game branch in `runActiveTick`
(`SimBotActivityDecision` defaults to `activity="hunt"`, `graduated=true`, so it
is not taken with the gate off); `governPvePopulation` (no diff hunk touches
it); and `applyProgressionToBody` (unchanged — the diff hunk is pure addition
after it).

## Defect 2 — attribution re-verified 2026-09-11 (owner challenge)

The owner correctly challenged the pre-existing claim: these scenarios passed in
F_0.9.5's receipt on 2026-09-06, so "it broke after F_0.9.6 was written" is the
right default assumption. The original A/B was re-run with the verification that
was missing the first time — **proving what the binary actually contained**.

| Build | F_0.9.6 string markers in `bin/core3` | Size | Scenario result |
|---|---|---|---|
| F_0.9.6 | `EARLY_GAME_GRIND_PREEMPT`, `goalSkillDetails`, `stalledBoxes`, `earlyGameEnabled`, `estimateDamageAfterMitigation` all **present** | 340,214,416 | FAIL, `kills=0` |
| **HEAD (verified)** | all five **absent (0)** | 339,858,816 | **FAIL, `kills=0`** |

The re-run used a configuration trimmed to **that one scenario only**, so no
earlier scenario could contaminate its state. Step trace on HEAD:
`setNoviceGate PASS → mintIdentity PASS → assertPlanOrder PASS → assertSkill PASS
→ respawnBody PASS → spawnKillTarget PASS → awaitKillTargetDeath FAIL
(step_budget_exceeded)`.

The first A/B is retroactively vindicated: its binary was 339,858,816 bytes —
byte-identical in size to this verified HEAD build. The 2-second build time that
cast doubt on it was ccache reusing pre-change objects, not a skipped rebuild.
The conclusion was right; the evidence presented for it was not strong enough,
and should have included this fingerprint from the start.

`git log` confirms **no commit since `a9cbfd2221` (F_0.9.5) touches**
`managers/combat`, `objects/creature/ai`, `scripts/mobile`, or
`scripts/object/weapon`. HEAD is code-identical to the tree that passed on
2026-09-06. The delta is therefore **runtime/world/database state, not source**.

Root cause remains **unknown** and is not diagnosed by this report. Candidates
worth checking in a dedicated chunk: leftover roster identities from F_0.9.5's
17 verification runs (82/83 harness, 84 brawler persist in `simbot_identities`);
persisted progression rows; and world/creature spawn state. Note the failure is
now **deterministic** across four runs on two different binaries, which points at
persistent state rather than flakiness.

## Scenario exclusions

To reach this chunk's own scenarios — all six sit after both blockers in the
matrix — the two pre-existing combat scenarios were temporarily removed for one
run. Both excluded blocks are preserved verbatim at
`scratchpad/p96/excluded_scenario.txt` and `excluded_scenario2.txt`. The
exclusion is documented, evidence-backed, and **was not used to claim a pass**:
the verdict is FAIL regardless.

## Environment note

Contrary to the standing assumption in `CLAUDE.md`, **the `/srv` host mount was
live for this session** — host and container paths are the same files (md5s
matched at the start). Every config `docker cp` into the container therefore
also rewrote the git worktree. No work was lost, but it means the mid-run A/B
backup was a *polluted* copy; the clean configuration was restored from a
snapshot captured **before** any deployment.

## Cleanup confirmation

`sim_player_manager.lua` restored and verified by md5
(`d01b284b9adc7e313f2ba12fc783c28b`) in both the container and the worktree:
all gates default-off, harness disabled, `maxHunters` back to 6, all 86 scenario
entries present. Server restarted onto the restored configuration. No database,
persistent roster, or unrelated log was cleared. The seven changed source files
were restored from backup after the A/B and verified by md5
(`a63507d337888e122be458fef611bdeb` for `SimPlayerManager.cpp`); the working
tree is back to the intended 2499-insertion F_0.9.6 state.

## Verdict

**LIVE_VERIFICATION_FAIL** — return to `TRIP-2-implement` for Defect 1. Defect 2
is pre-existing, independently reproduced on HEAD, and needs its own chunk.

## Re-run 2026-09-11 after the harness correction — 58/58 PASS

The four scenarios rebuilt against tiny dedicated ladders were re-run on the
F_0.9.6 binary with the two pre-existing combat scenarios excluded.

| | Result |
|---|---|
| Matrix | **58/58 PASS**, both boot phases (phase A 57 PASS + restart probe `AWAITING_RESTART`, phase B 58/58) |
| `multi_goal_plan_topology_and_scale` | PASS |
| `scalar_goal_skill_backward_compatibility` | PASS |
| `deferred_box_blocks_when_early_game_is_off` | **PASS** |
| `deferred_box_skips_and_mobile_damage_when_on` | **PASS** |
| `real_weapon_damage_template_value_when_on` | **PASS** |
| `fundable_graduation_and_grind_order_guard` | **PASS** |
| `orphanRecords` / `rosterWithoutRecord` | 0 / 0 |
| Build / `luac -p` | clean, 0 warnings |
| Codex code review of the fix | APPROVED, no new findings |

No pre-existing scenario regressed, which also clears the `identityIndex` drift
risk the rewrite introduced (the rewrite changed how many `mintIdentity` calls
run, and F_0.9.5's report documents that drifting positional indices in older
scenarios).

Defect 1 is therefore **closed**.

## Verdict stands at FAIL — blocked by Defect 2, not by this chunk

Every assertion this chunk can prove without a real kill now passes. The plan's
behavioural acceptance (Part C: leave city → TTK-selected target → **real kill**
→ XP rises → next box trains autonomously) is **unverifiable** while sim-bot
combat produces no damage, because C5 (real kill) and C6 (no cheat kills) both
require a kill to occur. **No receipt is recorded.**

F_0.9.6 should not be released until Defect 2 is resolved: its entire premise is
novices earning XP from real kills, so shipping it would ship a loop that cannot
run even with its gates on.

## Defect 2 — investigation 2026-09-11, verdict OBSERVABILITY_GAP

The characterisation sharpened materially. It is **not** "the bot cannot
attack" — **neither side attacks**. A novice stood 3 m from a `dwarf_nuna` for
ten minutes with `hunterDeathsTotal = 0`, while F_0.9.5's notes record that same
creature killing a weak novice in ~17 s. That is consistent with one failure:
the bot never lands the first hit, so nothing escalates and the creature never
retaliates.

Eliminated with evidence, not argument:

| Suspect | How it was ruled out |
|---|---|
| F_0.9.6 code | Fails on a **marker-verified** HEAD binary (all five F_0.9.6 strings absent), twice, once standalone |
| AI template files | `simHunter.lua`, `simParityTest.lua` and siblings all present and `luac -p` clean |
| Wrong AI map assigned | `spawnPveIdentityBody:15427` correctly assigns `simParityTest` (harness) / `simHunter` (roster) |
| Global AI disabled | No such knob set in `config-local.lua`; `Core3.AiAgent.Verbose` merely gates logging |
| Server stuck `LOADING` | `serverState = ONLINE` at boot; REST serving, miners sampling |
| Load saturation | 10 miners, 27 PvP bots — modest |
| Commands not loaded | 771 + 5 + 47 commands loaded cleanly |
| Body / equip failure | Bodies spawn (`attachedBodies` 6 and 15); a failed equip would destroy the body at `:15391` and `respawnBody` PASSes |
| Weapon misconfiguration | `hunterWeaponTemplate = object/weapon/ranged/rifle/rifle_t21.iff` |
| Body-slot contention | Reproduced with `maxHunters` raised to 12 |

**Leading mechanism.** Both sim AI templates park IDLE in a **3600-second
`Wait`**, and the design depends on `activateAiBehavior(true)` waking the tree
so the root selector reaches a combat branch. Independently,
`AiAgentImplementation::selectDefaultAttack` enqueues an attack **only when
`getFollowObject()` is non-null** (`:3449`). Either condition failing leaves an
agent in combat state that never swings, with no error surfaced.

**Why this stops here.** The decision path cannot be observed:
`DEBUG_AI_ATTACK` is commented out at `AiAgentImplementation.cpp:112`, the
per-agent `aiDebug` blackboard trace has no external setter, and `pveActivity`
exposes no per-agent `followObject`, defender count or command-queue depth.
That is TRIP-verify's `OBSERVABILITY_GAP` condition.

**Recommended next chunk** (default-off, run-scoped): a Lua-gated AI-combat
trace that re-enables the attack-selection decision log and publishes per-bot
`followObject` / defender count / command-queue depth on the dashboard, then one
`spawnKillTarget` run to read the actual decision path. Note the failure is
**deterministic across five runs on two binaries**, which points at persistent
state rather than flakiness.
