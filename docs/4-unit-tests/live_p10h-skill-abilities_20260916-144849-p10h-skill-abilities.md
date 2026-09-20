# Live verification — F_0.9.7 P.10h skill-derived combat abilities

- **Run id**: `20260916-144849-p10h-skill-abilities`
- **Plan**: `docs/1-plans/F_0.9.7_p10h-skill-derived-combat-abilities.plan.md`
- **Branch**: `feat/p10h-skill-derived-combat-abilities`
- **Contract**: `bin/log/trip-verify-20260916-144849-p10h-skill-abilities-contract.md` (v1, amended during the run — see Deviations)
- **Final binary**: `9bb1be83c8444c007454e6e5cef146e1`; final clean config md5 `83db5d29867e13a8692adb4f51678958`
- **Subsystems**: skill→ability derivation (`SimPlayerManager`), attack installation and selection (`AiAgent`, `SelectAttack` leaf), player-parity HAM cost (`CombatManager`), parity harness, REST inspect endpoint, dashboard drawer, `/server playerinfo`.

## Build and unit tests

- `build` warning-clean on every iteration (final: `build_evidence.txt`, rc 0).
- `luac -p` clean on every deployed `sim_player_manager.lua`.
- `core3 runUnitTests --gtest_filter="SimBotAbilityTiers*:CombatSpecialCost*"` → **7 passed, 7 new**.
- Codex code review: APPROVED (findings 1-10 resolved across rounds; see `docs/3-code-review/`).

## Runs and readiness

| Run | Config md5 | Purpose | Readiness |
|---|---|---|---|
| P (×2) | `123281c3`, `97595f82` | live-derivation probe | 8 s / 8 s |
| A phase A | `9a939812` | full parity matrix, 64 scenarios | 7 s |
| A phase B | `ccefb273` | restart probe second boot | 7 s |
| N | `4d993e96` | 47-minute gated novice grind | 8 s |
| Repro/stress | `2687a8fe`, `86ae8250` | crash reproduction (9 boots) | 7-12 s |
| Final | `83db5d29` (clean) | restore | 19 s |

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| U1 | Clean startup each boot | fresh `who.json` | every boot ready in 7-19 s | PASS |
| U2 | One live non-zombie core3 | 1 | 1 on every boot | PASS |
| U3 | No crash marker in gdb.log delta | 0 | `gdb.log` 964519 unchanged across all post-crash boots (17 boots) | PASS* |
| U4 | Dashboard/endpoint polls 200 | all | 726 N polls + matrix/probe polls, 0 non-200 | PASS |
| U5 | No `RESTServer ERROR` in core3_api.log delta | 0 | 0 | PASS |
| P1 | Inspect endpoint derives abilities gate-off | 200, `derived` non-empty, `notInstalledReason=capability_gate_off`, `attackSource=template` | exactly that for probe identities | PASS |
| P2 | Real tier chain observed | ≥1 chain with `replacedBy` | brawler master: `…lunge1→…lunge2` ×4, `warcry1→warcry2`, `intimidate1→intimidate2`; +TKA: `unarmedhit1`/`unarmedhit2→unarmedhit3`, `unarmedknockdown1→2`, `unarmedspinattack1→2`, `unarmedcombo1→2`; marksman: `headshot/bodyshot/legshot/overchargeshot 1→2` | PASS |
| P3 | Endpoint negative paths | non-200 | unknown identity 404, missing bearer 403 | PASS |
| P4 | Dashboard exposes the capability | `combatAbilities` present, `gate=false` | present, gate false | PASS |
| A1 | Phase A all PASS except restart probe | 63 PASS + AWAITING_RESTART, 0 FAIL | exactly that | PASS |
| A2 | Phase B all PASS | 64/64 | 64/64 | PASS |
| A3 | Four F_0.9.7 scenarios PASS | 4/4 | `abilities_from_trained_box`, `abilities_highest_tier_only`, `abilities_gate_off_template`, `abilities_used_in_kill` all PASS | PASS |
| A4 | Existing real-kill scenarios PASS | 2/2 | `harness_bot_kills_nuna_and_earns_xp`, `unarmed_novice_kills_nuna` PASS | PASS |
| A5 | Store integrity | orphanRecords 0, rosterWithoutRecord 0 | 0 / 0 | PASS |
| A6 | No harness override leak | production `combatAbilities.gate=false`, `bodiesWithAbilities=0` after cleanup | false / 0 | PASS |
| N1 | Gate live, bodies carry abilities | gate true, ≥1 body | gate true, 2 bodies | PASS |
| N2 | Subject uses skill-derived attacks | `attackSource=skill`, `intimidate1` active | 171/171 live-body inspect samples: source `skill`, `intimidate1` active (4 non-live samples correctly `template` + `no_live_body`) | PASS |
| N3 | Skill-derived selection traced and Intimidate used | ≥1 `SPECIAL_SELECT source=skill_derived`, `intimidate1` recorded special | 1307 skill-derived selections, 608 free-state selections, `intimidate1` ×23 | PASS |
| N4 | Ability lands and the fight continues | ≥1 use against an INTIMIDATED target | 1569 recorded uses with INTIMIDATED in `targetStatesBefore` | PASS |
| N5 | Specials are not spammed | `defaults > specials > 0` | defaults 139, specials 127 | PASS |
| N6 | Grind still works | ≥3 kills with kill awards, no stall | 68 kills, 46 awards, `lastAwardSource=kill`, both identities `grind` throughout; identity 84 trained a third box mid-run | PASS |
| N7 | Reserve telemetry published, no breach | counters present | `reserveSkips` 0, `reserveRefusedAtExecution` 0 | PASS |
| N8 | Superseded tiers never installed | `active ∩ superseded = ∅` | 0 overlaps in 175 inspect samples | PASS |
| O1 | Admin `/server playerinfo` on a SimBot | SimBot SUI; wild creature unchanged | owner-confirmed working | PASS |
| O2 | Player HAM costs unchanged | Intimidate free; costed special normal | owner-confirmed working | PASS |

\* U3 records **no new crash**; the crash that opened this investigation is documented below and is unresolved as a root cause.

## Defects found by this verification and fixed

1. **camelCase command names never resolved** (`SimPlayerManager`). `skills.iff` spells commands `unarmedHit1`; the `ObjectController` registry is lower case and `getQueueCommand` hashes the exact string, so every hit/lunge/stun/spin was excluded as `unknown_command` and only `intimidate1`/`taunt`/`warcry1`/`aim` survived. Derivation now lower-cases before exclusion, lookup and installation. Found by the live probe (P1/P2).
2. **HAM reserve tested pools the special does not spend** (`CombatManager`, selection filter). A hurt bot refused free unarmed specials 186 times and dealt no damage. The reserve now applies only to pools with a non-zero cost.
3. **Invalid special livelock** (`selectSpecialAttack`). The tree's `CheckAttackIsValid` aborts a tick whose special cannot apply its state; the scorer's heuristics do not mirror `validateStateAttack` (lunge is scored as knockdown but carries POSTUREDOWN), so a bot re-selected the same invalid lunge forever. Skill-derived scoring now skips candidates `validateStateAttack` would reject.
4. **Free-state forcing chose the wrong ability and never backed off**. It called the general scorer (which preferred a damaging lunge) and re-fired every tick. It now selects the qualifying non-damaging control ability **by name**, before the special roll, with a per-target `freeStateRetrySeconds` window.
5. **Area-aggro abilities pulled bystanders**. A forced-special novice spent a whole 600 s kill budget fighting creatures pulled by `warcry`/`taunt` (15 kills, 14 non-roster). Owner decision: ship `excludedCommands = {taunt, warcry1, warcry2}`, Lua-tunable.
6. **Harness regression from this branch**: `awaitKillTargetDeath` had been added to the identity-required op list, breaking the pre-existing `harness_bot_kills_nuna_and_earns_xp`. Reverted.
7. **Harness assertion blind spot**: `assertAbilityUse` read only the bounded ring, so an early Intimidate in a 22 s fight scrolled out. The usage log now keeps lifetime per-command counts and an accumulated target-state mask, and the assertion falls back to them.
8. **Harness diagnosability**: `kill_target_award_mismatch` now reports expected vs actual direct/general XP, baseXp and tier.

## Unresolved: SIGSEGV during the first Run A attempt

At boot second 172 of the first Run A phase-A attempt, `core3` took a SIGSEGV in
`AiAgentImplementation::notifyDespawn` (`:4823`, `Locker clocker(inventory, asAiAgent())`)
while a corpse `DespawnCreatureTask` ran for the harness kill target (`dwarf_nuna`, OID
281475048084649). Forensics from the frozen process: the implementation's `_this` weak
reference pointed at a freed reference-count block (strong count garbage, object pointer
null) and the stub's counter pointer had been reused by another allocation, i.e. a
use-after-free. The same victim had died 9 s earlier with `getBaseXp()` 0 while its template
carries 85, so the object was already damaged at death. No other thread was in AI or
despawn code at crash time.

Two harness defects of that class were fixed defensively (cancel and deregister the pending
corpse `DespawnCreatureTask` before `destroyObjectFromDatabase`; hold `ManagedReference`s
across kill-target spawn setup), but **the crash was not reproduced**: 9 boots and ~400
harness kills, including 6 full-matrix boots and 3 stress boots (48 kills each) on a binary
with those two fixes reverted, all clean. The root cause is therefore **unconfirmed**; it is
not in the F_0.9.7 ability path (the victim was a wild creature with no skill-derived
attacks and `simPlayerBot` false), and it did not recur in 17 subsequent boots. Evidence:
`bin/log/trip-verify-20260916-144849-p10h-skill-abilities.crash1-{gdb,forensics,screen,core3}.txt`.

## Deviations from the contract

- **Contract v1 §P2** assumed `abilities_highest_tier_only` would use the `intimidate1→intimidate2`
  chain; the probe confirmed that chain exists, so the scenario was unchanged.
- **A6** was rewritten from "every production novice body reports `attackSource == template`"
  to the dashboard equivalent (`gate=false`, `bodiesWithAbilities=0`): the A config leaves
  `novice.enabled` off, so no production novice body exists during the matrix.
- **Repro loops** (configs `2687a8fe`, `86ae8250`) are additional to the contract, run at the
  owner's direction to chase the SIGSEGV.
- Verification configs enable gates by design; the shipped configuration is default-off and
  was restored before this report (md5 `83db5d29`, confirmed on the final boot).

## Cleanup

- Clean config restored and verified on a final boot: `progression.enabled=false`,
  `combatAbilities.gate=false`, harness disabled, `records=0`, `orphanRecords=0`,
  `rosterWithoutRecord=0`.
- Harness/probe identities removed (`probe_identity_cleanup` scenarios for 466-468, 471-473,
  531, 779); no leaked roster identities remain.
- No database cleared, no unrelated log touched.

## Evidence

- `bin/log/trip-verify-20260916-144849-p10h-skill-abilities-contract.md`
- `bin/log/trip-verify-20260916-144849-p10h-skill-abilities-probe-inspect.json`
- `bin/log/trip-verify-20260916-144849-p10h-skill-abilities-novice-samples.jsonl` (550 samples, 5 s cadence)
- `bin/log/trip-verify-20260916-144849-p10h-skill-abilities-novice-simcombat.delta` (45 986 lines)
- `bin/log/trip-verify-20260916-144849-p10h-skill-abilities.crash1-*` (crash forensics)

## Verdict

`LIVE_VERIFICATION_PASS` — all contract assertions pass on binary `9bb1be83`, including both
owner-observed in-game checks. The unreproduced SIGSEGV is recorded above as open, unrelated
to the ability path, with its hardening shipped.
